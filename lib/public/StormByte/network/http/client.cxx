/*
 * Copyright (C) 2024-2026 David C. Manuelda (StormBytePP)
 *
 * This file is part of StormByte-Network.
 *
 * StormByte-Network original source is dual-licensed:
 *
 * 1. GNU Lesser General Public License v3.0 (or later)
 *    You may redistribute and/or modify this file under the terms of the
 *    GNU Lesser General Public License as published by the Free Software
 *    Foundation, either version 3 of the License, or (at your option)
 *    any later version.
 *
 * 2. Commercial license
 *    Alternatively, this file may be used under the terms of a commercial
 *    license agreement with the copyright holder
 *    (David C. Manuelda <StormByte@gmail.com>).
 *
 * Both licenses apply only to original StormByte-Network source in this
 * repository. They do not cover other StormByte modules or any third-party
 * material shipped with this repository (including everything under
 * thirdparty/, and in particular the bundled StormByte Buffer tree), which
 * remains under its own license.
 *
 * Neither license grants any patent rights. Any patent licenses required
 * to use this software or third-party components must be obtained separately
 * from the patent holders.
 *
 * StormByte-Network is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 3 along with StormByte-Network. If not, see
 * <https://www.gnu.org/licenses/lgpl-3.0.html>.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later OR LicenseRef-StormByte-Commercial
 */

#include <StormByte/network/connection/resolver.hxx>
#include <StormByte/network/http/client.hxx>
#include <StormByte/network/socket/client.hxx>
#include <StormByte/network/socket/tls.hxx>
#include <StormByte/network/tls_configuration.hxx>

#define NGHTTP2_NO_SSIZE_T
#include <nghttp2/nghttp2.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifdef UNIX
#include <arpa/inet.h>
#else
#include <ws2tcpip.h>
#endif

using namespace StormByte;
using namespace StormByte::Network;
using namespace StormByte::Network::HTTP;

Header::Header(std::string_view name, std::string_view value): name(name), value(value) {}

Header::Header(const Header& other) = default;

Header::Header(Header&& other) noexcept = default;

Header::~Header() noexcept = default;

Header& Header::operator=(const Header& other) = default;

Header& Header::operator=(Header&& other) noexcept = default;

Request::Request(std::string_view url): url(url), method("GET") {}

Request::Request(const Request& other) = default;

Request::Request(Request&& other) noexcept = default;

Request::~Request() noexcept = default;

Request& Request::operator=(const Request& other) = default;

Request& Request::operator=(Request&& other) noexcept = default;

Response::Response() = default;

Response::Response(const Response& other) = default;

Response::Response(Response&& other) noexcept = default;

Response::~Response() noexcept = default;

Response& Response::operator=(const Response& other) = default;

Response& Response::operator=(Response&& other) noexcept = default;

HTTP::Client::Client(): m_resolver(Safe::MakeShared<Connection::Resolver>()) {}

Receiver::~Receiver() noexcept = default;

HTTP::Client::Client(Client&& other) noexcept = default;

HTTP::Client::~Client() noexcept = default;

HTTP::Client& HTTP::Client::operator=(Client&& other) noexcept = default;

namespace {
	/**
	 * @brief Narrow checked byte/count conversions at protocol boundaries.
	 * @param value Provider-owned size.
	 * @return Native element count.
	 */
	template<class T>
	std::size_t Count(T value) {
		return static_cast<std::size_t>(value);
	}

	/**
	 * @brief Account bytes without arithmetic overflow.
	 * @param used Running count.
	 * @param extra Additional bytes.
	 * @param limit Inclusive limit.
	 */
	void Account(std::size_t& used, std::size_t extra, std::size_t limit) {
		if (used > limit || extra > limit - used)
			throw Error::LimitExceeded;
		used += extra;
	}

	/**
	 * @brief Convert a native stop reason into an HTTP error.
	 * @param state Transport state.
	 */
	void Check(Socket::OperationState state) {
		switch (state) {
			case Socket::OperationState::Complete:
				return;
			case Socket::OperationState::Cancelled:
				throw Error::Cancelled;
			case Socket::OperationState::TimedOut:
				throw Error::TimedOut;
			case Socket::OperationState::Busy:
				throw Error::ResolverBusy;
			default:
				throw Error::Transport;
		}
	}

	/**
	 * @brief Offer the final response to an optional streaming receiver.
	 * @param receiver Borrowed receiver or null.
	 * @param status Final status.
	 * @param headers Final fields.
	 */
	void Offer(Receiver* receiver, unsigned short status, const Safe::Vector<Header>& headers) {
		if (!receiver)
			return;
		bool accepted = false;
		try {
			accepted = receiver->Begin(status, headers);
		}
		catch (...) {
			throw Error::Rejected;
		}
		if (!accepted)
			throw Error::Rejected;
	}

	/**
	 * @brief Stream one nonempty body chunk to the receiver.
	 * @param receiver Borrowed receiver.
	 * @param bytes Borrowed chunk.
	 */
	void Deliver(Receiver& receiver, std::span<const std::byte> bytes) {
		if (bytes.empty())
			return;
		bool accepted = false;
		try {
			accepted = receiver.Write(bytes);
		}
		catch (...) {
			throw Error::Rejected;
		}
		if (!accepted)
			throw Error::Rejected;
	}

	/**
	 * @brief Compare ASCII case-insensitively, independent of locale.
	 * @param text Input.
	 * @return Lowercase text.
	 */
	std::string Lower(std::string_view text) {
		std::string result(text);
		for (auto& byte: result)
			if (byte >= 'A' && byte <= 'Z')
				byte += 'a' - 'A';
		return result;
	}

	/**
	 * @brief Validate one RFC token character.
	 * @param byte ASCII input.
	 * @return Whether allowed in an HTTP token.
	 */
	bool TokenByte(unsigned char byte) {
		return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z')
			|| (byte >= '0' && byte <= '9') || std::string_view("!#$%&'*+-.^_`|~").find(byte) != std::string_view::npos;
	}

	/**
	 * @brief Validate a nonempty HTTP token.
	 * @param text Input.
	 * @return Whether the entire token is valid.
	 */
	bool Token(std::string_view text) {
		return !text.empty() && std::ranges::all_of(text, TokenByte);
	}

	/**
	 * @brief Validate a field value, rejecting control/header injection.
	 * @param text Input.
	 * @return Whether the value is valid.
	 */
	bool Value(std::string_view text) {
		return std::ranges::all_of(text, [](unsigned char byte) {
			return byte == '\t' || (byte >= 32 && byte != 127);
		});
	}

	/**
	 * @brief Trim HTTP optional whitespace.
	 * @param text Borrowed input.
	 * @return Borrowed trimmed view.
	 */
	std::string_view Trim(std::string_view text) {
		const auto first = text.find_first_not_of(" \t");
		if (first == std::string_view::npos)
			return {};
		return text.substr(first, text.find_last_not_of(" \t") - first + 1);
	}

	/**
	 * @brief Parse a nonempty unsigned integer without signs or overflow.
	 * @param text Digits.
	 * @param base Decimal or hexadecimal.
	 * @return Parsed integer.
	 */
	std::size_t Number(std::string_view text, int base = 10) {
		if (text.empty())
			throw Error::Protocol;
		std::size_t value = 0;
		const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
		if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
			throw Error::Protocol;
		return value;
	}

	/**
	 * @brief Parsed URL; all owning standard values stay inside Network.
	 */
	struct URL {
		bool tls{false};	///< HTTPS selection.
		bool ip{false};	///< Literal address.
		int family{AF_INET};	///< Literal family.
		std::array<std::byte, 16> address{};	///< Binary literal SAN.
		std::string host;	///< Resolver input without brackets.
		std::string authority;	///< Host header and HTTP/2 authority.
		std::string target;	///< Origin-form path and query.
		unsigned short port{80};	///< Validated destination port.
	};

	/**
	 * @brief Parse an absolute ASCII URL without decoding reserved delimiters.
	 * @param input Borrowed URL.
	 * @return Validated parsed URL.
	 */
	URL ParseURL(std::string_view input) {
		URL url;
		for (std::size_t index = 0; index < input.size(); ++index) {
			const auto byte = static_cast<unsigned char>(input[index]);
			if (byte <= 32 || byte >= 127 || std::string_view("\\#\"<>^`{|}").find(byte) != std::string_view::npos)
				throw Error::InvalidURL;
			if (byte == '%') {
				if (input.size() - index < 3)
					throw Error::InvalidURL;
				const auto hex = [](unsigned char digit) {
					return (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f') || (digit >= 'A' && digit <= 'F');
				};
				if (!hex(input[index + 1]) || !hex(input[index + 2]))
					throw Error::InvalidURL;
			}
		}
		const auto scheme_end = input.find("://");
		if (scheme_end == std::string_view::npos)
			throw Error::InvalidURL;
		const auto scheme = Lower(input.substr(0, scheme_end));
		if (scheme != "http" && scheme != "https")
			throw Error::InvalidURL;
		url.tls = scheme == "https";
		url.port = url.tls ? 443 : 80;
		input.remove_prefix(scheme_end + 3);
		const auto end = input.find_first_of("/?");
		auto authority = input.substr(0, end);
		if (authority.empty() || authority.find('@') != std::string_view::npos)
			throw Error::InvalidURL;
		url.authority = authority;
		auto target = end == std::string_view::npos ? std::string_view{} : input.substr(end);
		if (!std::ranges::all_of(target, [](unsigned char byte) {
			return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z')
				|| (byte >= '0' && byte <= '9') || std::string_view("-._~!$&'()*+,;=:@/?%").find(byte) != std::string_view::npos;
		}))
			throw Error::InvalidURL;
		url.target = target.empty() ? "/" : target.front() == '?' ? "/" + std::string(target) : std::string(target);
		std::string_view port;
		bool explicit_port = false;
		if (authority.front() == '[') {
			const auto close = authority.find(']');
			if (close == std::string_view::npos)
				throw Error::InvalidURL;
			url.host = authority.substr(1, close - 1);
			url.family = AF_INET6;
			url.ip = true;
			if (::inet_pton(AF_INET6, url.host.c_str(), url.address.data()) != 1)
				throw Error::InvalidURL;
			const auto suffix = authority.substr(close + 1);
			if (!suffix.empty()) {
				if (suffix.front() != ':')
					throw Error::InvalidURL;
				explicit_port = true;
				port = suffix.substr(1);
			}
		}
		else {
			const auto colon = authority.find(':');
			if (colon != std::string_view::npos) {
				explicit_port = true;
				port = authority.substr(colon + 1);
				authority = authority.substr(0, colon);
			}
			if (authority.empty() || authority.size() > 253)
				throw Error::InvalidURL;
			url.host = Lower(authority);
			if (::inet_pton(AF_INET, url.host.c_str(), url.address.data()) == 1)
				url.ip = true;
			else {
				auto dns = authority;
				if (dns.back() == '.')
					dns.remove_suffix(1);
				if (dns.empty())
					throw Error::InvalidURL;
				while (!dns.empty()) {
					const auto dot = dns.find('.');
					const auto label = dns.substr(0, dot);
					if (label.empty() || label.size() > 63 || label.front() == '-' || label.back() == '-'
						|| !std::ranges::all_of(label, [](unsigned char byte) {
							return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z')
								|| (byte >= '0' && byte <= '9') || byte == '-';
						}))
						throw Error::InvalidURL;
					if (dot == std::string_view::npos)
						break;
					dns.remove_prefix(dot + 1);
					if (dns.empty())
						throw Error::InvalidURL;
				}
			}
		}
		if (explicit_port) {
			try {
				const auto parsed = Number(port);
				if (parsed == 0 || parsed > 65535)
					throw Error::InvalidURL;
				url.port = static_cast<unsigned short>(parsed);
			}
			catch (Error) {
				throw Error::InvalidURL;
			}
		}
		return url;
	}

	/**
	 * @brief Reject application-supplied framing and hop-by-hop fields.
	 * @param name Lowercase field name.
	 * @return Whether reserved.
	 */
	bool Reserved(std::string_view name) {
		return name == "host" || name == "content-length" || name == "transfer-encoding"
			|| name == "connection" || name == "proxy-connection" || name == "keep-alive"
			|| name == "upgrade" || name == "expect" || name == "te" || name == "trailer"
			|| name == "http2-settings";
	}

	/**
	 * @brief Validate trailing fields that cannot change message interpretation.
	 * @param name Lowercase field name.
	 * @return Whether forbidden in trailers.
	 */
	bool ForbiddenTrailer(std::string_view name) {
		return Reserved(name) || name == "content-encoding" || name == "content-type"
			|| name == "content-range" || name == "authorization" || name == "www-authenticate"
			|| name == "proxy-authenticate" || name == "proxy-authorization"
			|| name == "location" || name == "retry-after" || name == "cache-control"
			|| name == "age" || name == "date" || name == "expires" || name == "vary"
			|| name == "set-cookie" || name == "content-disposition" || name == "content-language";
	}

	/**
	 * @brief Validate and bound all user request fields before connecting.
	 * @param request Borrowed request.
	 * @param options Borrowed bounds.
	 * @param url Parsed authority and target.
	 */
	void ValidateRequest(const Request& request, const Options& options, const URL& url) {
		const auto method = std::string_view(request.method);
		if (!Token(method) || method == "CONNECT")
			throw Error::InvalidRequest;
		if (Count(request.body.size()) > Count(options.body_bytes))
			throw Error::LimitExceeded;
		std::size_t fields = 3;
		std::size_t bytes = method.size() + url.target.size() + url.authority.size() + 80;
		Account(bytes, 0, Count(options.header_bytes));
		Account(fields, Count(request.headers.size()), Count(options.header_fields));
		for (const auto& header: request.headers) {
			const auto name = std::string_view(header.name);
			const auto value = std::string_view(header.value);
			if (!Token(name) || !Value(value) || Reserved(Lower(name)))
				throw Error::InvalidRequest;
			Account(bytes, name.size(), Count(options.header_bytes));
			Account(bytes, value.size() + 4, Count(options.header_bytes));
		}
	}

	/**
	 * @brief One borrowed channel over Network DNS/socket and Crypto TLS scheduling.
	 * @details Stack-only Network state; its socket closes on every exit.
	 */
	struct Channel {
		Socket::Client socket{Connection::Protocol::IPv4, {}};	///< Native provider-owned socket.
		Crypto::TLS::Session tls;	///< Independent external engine.
		bool encrypted{false};	///< Use authenticated TLS I/O.
		const Socket::OperationControl& control;	///< Total deadline.
		const Options& options;	///< Limits.
		std::size_t wire{0};	///< Received plaintext bytes.

		/**
		 * @brief Translate authenticated transport results.
		 * @param result Crypto/socket outcome.
		 */
		void CheckTLS(const Socket::TLS::Result& result) {
			if (result.truncated)
				throw Error::Truncated;
			if (result.tls_failure)
				throw Error::TLS;
			Check(result.state);
		}

		/**
		 * @brief Transmit every byte without replay.
		 * @param bytes Borrowed plaintext.
		 */
		void Write(std::span<const std::byte> bytes) {
			Check(control.Check());
			if (encrypted)
				CheckTLS(Socket::TLS::Write(socket, tls, bytes, control));
			else
				Check(socket.Send(bytes, control).state);
		}

		/**
		 * @brief Transmit text as bytes.
		 * @param text Borrowed wire text.
		 */
		void Write(std::string_view text) {
			Write(std::as_bytes(std::span{text}));
		}

		/**
		 * @brief Read one bounded plaintext chunk; zero means authenticated EOF.
		 * @param output Borrowed writable storage.
		 * @return Bytes delivered.
		 */
		std::size_t Read(std::span<std::byte> output) {
			Check(control.Check());
			std::size_t bytes;
			if (encrypted) {
				const auto result = Socket::TLS::Read(socket, tls, output, control);
				if (result.state == Socket::OperationState::PeerClosed)
					return 0;
				CheckTLS(result);
				bytes = Count(result.bytes);
			}
			else {
				Socket::OperationResult result;
				for (;;) {
					Check(control.Check());
					result = socket.ReadSome(output);
					if (result.state != Socket::OperationState::WouldBlock)
						break;
					Check(Socket::WaitReadable(socket.Handle(), control).state);
				}
				if (result.state == Socket::OperationState::PeerClosed)
					return 0;
				Check(result.state);
				bytes = Count(result.bytes);
			}
			Account(wire, bytes, Count(options.wire_bytes));
			return bytes;
		}
	};

	/**
	 * @brief Buffered HTTP/1 reader; bounded lines and exact body copies.
	 */
	struct Reader {
		Channel& channel;	///< Borrowed channel.
		std::array<std::byte, 16384> buffer{};	///< Bounded input.
		std::size_t offset{0};	///< Consumed prefix.
		std::size_t available{0};	///< Retained byte count.
		std::size_t headers{0};	///< Aggregate status/header/trailer bytes.
		std::size_t fields{0};	///< Aggregate fields.
		std::size_t delivered{0};	///< Final body bytes buffered or streamed.

		/**
		 * @brief Refill only an exhausted buffer.
		 * @return Whether new bytes are available.
		 */
		bool Fill() {
			if (offset != available)
				return true;
			offset = 0;
			available = channel.Read(buffer);
			return available != 0;
		}

		/**
		 * @brief Read one CRLF line, rejecting bare CR/LF and unbounded text.
		 * @param header Whether to charge aggregate decoded header limit.
		 * @return Line without CRLF.
		 */
		std::string Line(bool header = true) {
			std::string line;
			for (;;) {
				Check(channel.control.Check());
				if (!Fill())
					throw Error::Truncated;
				const char byte = static_cast<char>(buffer[offset++]);
				if (header)
					Account(headers, 1, Count(channel.options.header_bytes));
				if (byte == '\n')
					throw Error::Protocol;
				if (byte == '\r') {
					if (!Fill())
						throw Error::Truncated;
					if (buffer[offset++] != std::byte{'\n'})
						throw Error::Protocol;
					if (header)
						Account(headers, 1, Count(channel.options.header_bytes));
					return line;
				}
				if (line.size() >= Count(channel.options.header_bytes))
					throw Error::LimitExceeded;
				line += byte;
			}
		}

		/**
		 * @brief Read a validated header block.
		 * @param trailers Whether this block is trailing fields.
		 * @return Ordered fields.
		 */
		Safe::Vector<Header> Headers(bool trailers = false) {
			Safe::Vector<Header> result;
			for (;;) {
				auto line = Line();
				if (line.empty())
					return result;
				const auto colon = line.find(':');
				if (colon == std::string::npos || !Token(std::string_view(line).substr(0, colon))
					|| !Value(std::string_view(line).substr(colon + 1)))
					throw Error::Protocol;
				const auto name = Lower(std::string_view(line).substr(0, colon));
				if (trailers && ForbiddenTrailer(name))
					throw Error::Protocol;
				Account(fields, 1, Count(channel.options.header_fields));
				result.emplace_back(name, Trim(std::string_view(line).substr(colon + 1)));
			}
		}

		/**
		 * @brief Copy an exact body prefix into Safe storage or the streaming receiver.
		 * @param body Buffered output, unused when streaming.
		 * @param length Bytes required.
		 */
		void Body(Safe::Binary& body, std::size_t length) {
			Account(delivered, length, Count(channel.options.body_bytes));
			auto* receiver = channel.options.receiver;
			while (length) {
				Check(channel.control.Check());
				if (!Fill())
					throw Error::Truncated;
				const auto part = std::min(length, available - offset);
				const auto bytes = std::span(buffer).subspan(offset, part);
				if (receiver)
					Deliver(*receiver, bytes);
				else
					body.append(bytes);
				offset += part;
				length -= part;
			}
		}
	};

	/**
	 * @brief Validate Content-Length occurrences, including comma-separated repeats.
	 * @param headers Ordered fields.
	 * @return Absent or identical validated decimal lengths.
	 */
	std::optional<std::size_t> ContentLength(const Safe::Vector<Header>& headers) {
		std::optional<std::size_t> length;
		for (const auto& header: headers) {
			if (std::string_view(header.name) != "content-length")
				continue;
			auto text = std::string_view(header.value);
			for (;;) {
				const auto comma = text.find(',');
				const auto value = Number(Trim(text.substr(0, comma)));
				if (length && *length != value)
					throw Error::Protocol;
				length = value;
				if (comma == std::string_view::npos)
					break;
				text.remove_prefix(comma + 1);
			}
		}
		return length;
	}

	/**
	 * @brief Determine bodyless final response semantics.
	 * @param method Request method.
	 * @param status Final status.
	 * @return Whether no entity body may follow.
	 */
	bool Bodyless(std::string_view method, unsigned short status) {
		return method == "HEAD" || status == 204 || status == 304;
	}

	/**
	 * @brief Validate a complete chunk extension grammar (tokens/quoted strings).
	 * @param text Extension portion, starting at the first semicolon.
	 */
	void Extensions(std::string_view text) {
		const auto skip = [&text] {
			while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
				text.remove_prefix(1);
		};
		while (!text.empty()) {
			skip();
			if (text.empty() || text.front() != ';')
				throw Error::Protocol;
			text.remove_prefix(1);
			skip();
			std::size_t token = 0;
			while (token < text.size() && TokenByte(static_cast<unsigned char>(text[token])))
				++token;
			if (token == 0)
				throw Error::Protocol;
			text.remove_prefix(token);
			skip();
			if (text.empty() || text.front() == ';')
				continue;
			if (text.front() != '=')
				throw Error::Protocol;
			text.remove_prefix(1);
			skip();
			if (text.empty())
				throw Error::Protocol;
			if (text.front() == '"') {
				text.remove_prefix(1);
				bool closed = false;
				while (!text.empty()) {
					const auto byte = static_cast<unsigned char>(text.front());
					text.remove_prefix(1);
					if (byte == '"') {
						closed = true;
						break;
					}
					if (byte == '\\') {
						if (text.empty() || !Value(text.substr(0, 1)))
							throw Error::Protocol;
						text.remove_prefix(1);
					}
					else if (!Value(std::string_view(reinterpret_cast<const char*>(&byte), 1)))
						throw Error::Protocol;
				}
				if (!closed)
					throw Error::Protocol;
			}
			else {
				token = 0;
				while (token < text.size() && TokenByte(static_cast<unsigned char>(text[token])))
					++token;
				if (!token)
					throw Error::Protocol;
				text.remove_prefix(token);
			}
			skip();
		}
	}

	/**
	 * @brief Send and parse a complete HTTP/1.1 exchange, accepting HTTP/1.0 responses.
	 * @param channel Borrowed connected channel.
	 * @param request Borrowed request.
	 * @param url Parsed URL.
	 * @return Complete response.
	 */
	Response HTTP1(Channel& channel, const Request& request, const URL& url) {
		std::string wire = std::string(std::string_view(request.method)) + " " + url.target + " HTTP/1.1\r\nHost: "
			+ url.authority + "\r\nConnection: close\r\nContent-Length: " + std::to_string(Count(request.body.size())) + "\r\n";
		for (const auto& header: request.headers)
			wire += std::string(std::string_view(header.name)) + ": " + std::string(std::string_view(header.value)) + "\r\n";
		wire += "\r\n";
		channel.Write(wire);
		channel.Write(request.body.span());
		Reader reader{channel};
		Response response;
		std::size_t interim = 0;
		bool http10 = false;
		for (;;) {
			const auto line = reader.Line();
			if (line.size() < 13 || (line.substr(0, 9) != "HTTP/1.1 " && line.substr(0, 9) != "HTTP/1.0 ")
				|| line[12] != ' ' || !Value(std::string_view(line).substr(13)))
				throw Error::Protocol;
			const auto status = Number(std::string_view(line).substr(9, 3));
			if (status < 100 || status > 599)
				throw Error::Protocol;
			http10 = line[7] == '0';
			response.status = static_cast<unsigned short>(status);
			response.headers = reader.Headers();
			if (status >= 200)
				break;
			if (status == 101 || ContentLength(response.headers))
				throw Error::Protocol;
			for (const auto& header: response.headers)
				if (std::string_view(header.name) == "transfer-encoding")
					throw Error::Protocol;
			Account(interim, 1, Count(channel.options.informational_responses));
		}
		const auto length = ContentLength(response.headers);
		bool chunked = false;
		for (const auto& header: response.headers) {
			if (std::string_view(header.name) == "transfer-encoding") {
				if (chunked || http10 || Lower(Trim(std::string_view(header.value))) != "chunked" || length)
					throw Error::Protocol;
				chunked = true;
			}
			if (std::string_view(header.name) == "upgrade")
				throw Error::Protocol;
		}
		if (response.status == 204 && (length || chunked))
			throw Error::Protocol;
		Offer(channel.options.receiver, response.status, response.headers);
		if (Bodyless(std::string_view(request.method), response.status))
			return response;
		if (chunked) {
			for (;;) {
				const auto line = reader.Line(false);
				const auto extension = line.find(';');
				const auto digits = std::string_view(line).substr(0, extension);
				if (digits.empty() || digits.front() == ' ' || digits.front() == '\t'
					|| (extension == std::string::npos && Trim(digits) != digits))
					throw Error::Protocol;
				const auto chunk = Number(Trim(digits), 16);
				if (extension != std::string::npos)
					Extensions(std::string_view(line).substr(extension));
				if (!chunk)
					break;
				reader.Body(response.body, chunk);
				if (!reader.Line(false).empty())
					throw Error::Protocol;
			}
			response.trailers = reader.Headers(true);
		}
		else if (length)
			reader.Body(response.body, *length);
		else
			while (reader.Fill())
				reader.Body(response.body, reader.available - reader.offset);
		if (response.status == 205 && reader.delivered)
			throw Error::Protocol;
		return response;
	}

	/**
	 * @brief nghttp2 callback state; native library resources never cross Network.
	 * @details All C callbacks contain exceptions and convert them to terminal errors.
	 */
	struct H2 {
		/**
		 * @brief Max-aligned provider allocation prefix for native engine accounting.
		 */
		struct alignas(std::max_align_t) Allocation {
			std::size_t bytes;	///< Full allocation size including this prefix.
		};

		Channel& channel;	///< Borrowed channel.
		const Request& request;	///< Borrowed immutable upload.
		Response response;	///< Buffered result.
		nghttp2_session* session{nullptr};	///< Owned native HTTP/2 session.
		nghttp2_session_callbacks* callbacks{nullptr};	///< Owned native callback table.
		std::optional<Error> error;	///< First terminal failure.
		Safe::Vector<Header> block;	///< Current decoded header block.
		std::size_t headers{0};	///< Decoded field bytes, including RFC overhead.
		std::size_t fields{0};	///< Aggregate field count.
		std::size_t interim{0};	///< Informational count.
		std::size_t sent{0};	///< Accepted upload prefix.
		std::size_t received{0};	///< Final body bytes buffered or streamed.
		std::optional<std::size_t> length;	///< Expected body length.
		int32_t stream{0};	///< Sole locally initiated stream.
		bool final{false};	///< Final response headers accepted.
		bool trailers{false};	///< Trailing block accepted.
		bool done{false};	///< Stream completed successfully.
		bool no_body{false};	///< HEAD/204/304 semantics.
		std::optional<unsigned short> status;	///< Current :status.
		bool regular{false};	///< Regular fields already observed.
		std::size_t allocated{0};	///< Live native allocations, including prefixes.
		std::size_t allocation_limit;	///< Explicit native engine byte bound.
		bool allocation_exceeded{false};	///< A native allocation exceeded its bound.

		/**
		 * @brief Construct stack-local exchange state.
		 * @param channel Borrowed channel.
		 * @param request Borrowed upload.
		 */
		H2(Channel& channel, const Request& request): channel(channel), request(request),
			allocation_limit(Count(channel.options.http2_bytes)) {
			response.protocol = Protocol::HTTP2;
		}

		/**
		 * @brief Native callback contexts and handles cannot be copied or moved.
		 * @param other Exchange whose address must remain stable.
		 */
		H2(const H2& other) = delete;

		/**
		 * @brief Native callback contexts cannot move.
		 * @param other Exchange whose address must remain stable.
		 */
		H2(H2&& other) = delete;

		/**
		 * @brief Native callback ownership cannot be replaced.
		 * @param other Source exchange.
		 * @return No value; deleted.
		 */
		H2& operator=(const H2& other) = delete;

		/**
		 * @brief Native callback ownership cannot be transferred.
		 * @param other Source exchange.
		 * @return No value; deleted.
		 */
		H2& operator=(H2&& other) = delete;

		/**
		 * @brief Delete native resources inside Network.
		 */
		~H2() {
			nghttp2_session_del(session);
			nghttp2_session_callbacks_del(callbacks);
		}

		/**
		 * @brief Allocate native state with a hard cap, including declared HPACK strings.
		 * @param size Requested bytes.
		 * @param user Stable exchange context.
		 * @return Max-aligned storage or null; never throws across C.
		 */
		static void* Allocate(size_t size, void* user) noexcept {
			auto& self = *static_cast<H2*>(user);
			if (size > std::numeric_limits<std::size_t>::max() - sizeof(Allocation)
				|| self.allocated > self.allocation_limit
				|| size + sizeof(Allocation) > self.allocation_limit - self.allocated) {
				self.allocation_exceeded = true;
				return nullptr;
			}
			const auto bytes = size + sizeof(Allocation);
			auto* block = static_cast<Allocation*>(std::malloc(bytes));
			if (!block)
				return nullptr;
			block->bytes = bytes;
			self.allocated += bytes;
			return block + 1;
		}

		/**
		 * @brief Free native state in the same provider that allocated it.
		 * @param pointer Previously allocated storage or null.
		 * @param user Stable exchange context.
		 */
		static void Release(void* pointer, void* user) noexcept {
			if (!pointer)
				return;
			auto& self = *static_cast<H2*>(user);
			auto* block = static_cast<Allocation*>(pointer) - 1;
			self.allocated -= block->bytes;
			std::free(block);
		}

		/**
		 * @brief Checked zero-filled native allocation.
		 * @param count Elements.
		 * @param size Element byte size.
		 * @param user Stable exchange context.
		 * @return Storage or null.
		 */
		static void* Zero(size_t count, size_t size, void* user) noexcept {
			if (size && count > std::numeric_limits<std::size_t>::max() / size) {
				static_cast<H2*>(user)->allocation_exceeded = true;
				return nullptr;
			}
			auto* pointer = Allocate(count * size, user);
			if (pointer)
				std::memset(pointer, 0, count * size);
			return pointer;
		}

		/**
		 * @brief Resize native storage without losing its old allocation on failure.
		 * @param pointer Existing allocation or null.
		 * @param size New requested byte size.
		 * @param user Stable exchange context.
		 * @return Resized storage or null.
		 */
		static void* Resize(void* pointer, size_t size, void* user) noexcept {
			if (!pointer)
				return Allocate(size, user);
			if (!size) {
				Release(pointer, user);
				return nullptr;
			}
			auto& self = *static_cast<H2*>(user);
			auto* block = static_cast<Allocation*>(pointer) - 1;
			const auto previous = block->bytes;
			const auto retained = self.allocated - previous;
			if (size > std::numeric_limits<std::size_t>::max() - sizeof(Allocation)
				|| retained > self.allocation_limit
				|| size + sizeof(Allocation) > self.allocation_limit - retained) {
				self.allocation_exceeded = true;
				return nullptr;
			}
			const auto bytes = size + sizeof(Allocation);
			auto* replacement = static_cast<Allocation*>(std::realloc(block, bytes));
			if (!replacement)
				return nullptr;
			replacement->bytes = bytes;
			self.allocated = retained + bytes;
			return replacement + 1;
		}

		/**
		 * @brief Contain callback exceptions and preserve the first error.
		 * @param user Borrowed native context.
		 * @param action Synchronous callback action.
		 * @return nghttp2 success or fatal callback failure.
		 */
		template<class F>
		static int Guard(void* user, F action) noexcept {
			auto& self = *static_cast<H2*>(user);
			try {
				Check(self.channel.control.Check());
				if (self.allocation_exceeded)
					throw Error::LimitExceeded;
				action(self);
				return 0;
			}
			catch (Error failure) {
				if (!self.error)
					self.error = failure;
			}
			catch (...) {
				if (!self.error)
					self.error = Error::Allocation;
			}
			return NGHTTP2_ERR_CALLBACK_FAILURE;
		}

		/**
		 * @brief Start a response or trailer block.
		 */
		static int Begin(nghttp2_session*, const nghttp2_frame* frame, void* user) noexcept {
			return Guard(user, [frame](H2& self) {
				if (frame->hd.stream_id != self.stream || self.trailers)
					throw Error::Protocol;
				self.block.clear();
				self.status.reset();
				self.regular = false;
			});
		}

		/**
		 * @brief Collect bounded decoded HPACK fields, retaining duplicates.
		 */
		static int Field(nghttp2_session*, const nghttp2_frame*, const uint8_t* name, size_t name_size,
			const uint8_t* value, size_t value_size, uint8_t, void* user) noexcept {
			return Guard(user, [=](H2& self) {
				Account(self.headers, name_size, Count(self.channel.options.header_bytes));
				Account(self.headers, value_size, Count(self.channel.options.header_bytes));
				Account(self.headers, 32, Count(self.channel.options.header_bytes));
				Account(self.fields, 1, Count(self.channel.options.header_fields));
				const std::string_view key(reinterpret_cast<const char*>(name), name_size);
				const std::string_view text(reinterpret_cast<const char*>(value), value_size);
				if (!Value(text) || Trim(text) != text || Lower(key) != key)
					throw Error::Protocol;
				if (key == ":status") {
					if (self.final || self.regular || self.status || text.size() != 3)
						throw Error::Protocol;
					const auto status = Number(text);
					if (status < 100 || status > 599 || status == 101)
						throw Error::Protocol;
					self.status = static_cast<unsigned short>(status);
				}
				else {
					if (!Token(key) || key == "connection" || key == "proxy-connection" || key == "keep-alive"
						|| key == "transfer-encoding" || key == "upgrade" || key == "te"
						|| (self.final && ForbiddenTrailer(key)))
						throw Error::Protocol;
					self.regular = true;
					self.block.emplace_back(key, text);
				}
			});
		}

		/**
		 * @brief Validate headers, stream shutdown and GOAWAY without retries.
		 */
		static int Frame(nghttp2_session*, const nghttp2_frame* frame, void* user) noexcept {
			return Guard(user, [frame](H2& self) {
				if (frame->hd.type == NGHTTP2_GOAWAY) {
					if (frame->goaway.error_code != NGHTTP2_NO_ERROR || frame->goaway.last_stream_id < self.stream)
						throw Error::HTTP2;
				}
				if (frame->hd.type == NGHTTP2_PUSH_PROMISE
					|| (frame->hd.type == NGHTTP2_RST_STREAM && !self.done))
					throw Error::HTTP2;
				if (frame->hd.type != NGHTTP2_HEADERS)
					return;
				if (!self.final) {
					if (!self.status)
						throw Error::Protocol;
					if (*self.status < 200) {
						if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM || ContentLength(self.block))
							throw Error::Protocol;
						Account(self.interim, 1, Count(self.channel.options.informational_responses));
						return;
					}
					self.response.status = *self.status;
					self.response.headers = std::move(self.block);
					self.length = ContentLength(self.response.headers);
					if (self.response.status == 204 && self.length)
						throw Error::Protocol;
					self.no_body = Bodyless(std::string_view(self.request.method), self.response.status);
					if (!self.no_body && self.length && *self.length > Count(self.channel.options.body_bytes))
						throw Error::LimitExceeded;
					self.final = true;
					Offer(self.channel.options.receiver, self.response.status, self.response.headers);
				}
				else {
					if (self.status || !(frame->hd.flags & NGHTTP2_FLAG_END_STREAM))
						throw Error::Protocol;
					self.response.trailers = std::move(self.block);
					self.trailers = true;
				}
			});
		}

		/**
		 * @brief Receive one flow-controlled body chunk into bounded Safe storage
		 * or the streaming receiver.
		 */
		static int Data(nghttp2_session*, uint8_t, int32_t stream, const uint8_t* data, size_t size, void* user) noexcept {
			return Guard(user, [=](H2& self) {
				if (stream != self.stream || !self.final || self.trailers || (self.no_body && size))
					throw Error::Protocol;
				Account(self.received, size, Count(self.channel.options.body_bytes));
				if (self.length && !self.no_body && self.received > *self.length)
					throw Error::Protocol;
				if (self.response.status == 205 && size)
					throw Error::Protocol;
				const auto bytes = std::span(reinterpret_cast<const std::byte*>(data), size);
				if (auto* receiver = self.channel.options.receiver)
					Deliver(*receiver, bytes);
				else
					self.response.body.append(bytes);
			});
		}

		/**
		 * @brief Require a successful complete final stream and exact Content-Length.
		 */
		static int Close(nghttp2_session*, int32_t stream, uint32_t error, void* user) noexcept {
			return Guard(user, [=](H2& self) {
				if (stream != self.stream || error != NGHTTP2_NO_ERROR)
					throw Error::HTTP2;
				if (!self.final || (!self.no_body && self.length && self.received != *self.length))
					throw Error::Truncated;
				self.done = true;
			});
		}

		/**
		 * @brief Treat rejected inbound frames as terminal, never silently ignore.
		 */
		static int Invalid(nghttp2_session*, const nghttp2_frame*, int, void* user) noexcept {
			return Guard(user, [](H2&) { throw Error::HTTP2; });
		}

		/**
		 * @brief Never silently drop malformed regular header fields.
		 */
		static int InvalidField(nghttp2_session*, const nghttp2_frame*, const uint8_t*, size_t,
			const uint8_t*, size_t, uint8_t, void* user) noexcept {
			return Guard(user, [](H2&) { throw Error::Protocol; });
		}

		/**
		 * @brief Treat an unsent protocol frame as terminal.
		 */
		static int Unsent(nghttp2_session*, const nghttp2_frame*, int, void* user) noexcept {
			return Guard(user, [](H2&) { throw Error::HTTP2; });
		}

		/**
		 * @brief Feed upload bytes under nghttp2's send-window scheduling.
		 */
		static nghttp2_ssize Upload(nghttp2_session*, int32_t, uint8_t* output, size_t size,
			uint32_t* flags, nghttp2_data_source*, void* user) noexcept {
			std::size_t copied = 0;
			const auto result = Guard(user, [&](H2& self) {
				const auto body = self.request.body.span();
				copied = std::min(size, body.size() - self.sent);
				if (copied)
					std::memcpy(output, body.data() + self.sent, copied);
				self.sent += copied;
				if (self.sent == body.size())
					*flags |= NGHTTP2_DATA_FLAG_EOF;
			});
			return result ? NGHTTP2_ERR_CALLBACK_FAILURE : static_cast<nghttp2_ssize>(copied);
		}

		/**
		 * @brief Propagate library or callback errors before processing more bytes.
		 * @param result Library return code.
		 */
		void Result(int result) {
			if (allocation_exceeded)
				throw Error::LimitExceeded;
			if (error)
				throw *error;
			if (result == NGHTTP2_ERR_NOMEM)
				throw Error::Allocation;
			if (result < 0)
				throw Error::HTTP2;
		}

		/**
		 * @brief Run the actual nghttp2 client engine with SETTINGS and flow control.
		 * @param url Parsed URL.
		 * @return Complete buffered response.
		 */
		Response Run(const URL& url) {
			Result(nghttp2_session_callbacks_new(&callbacks));
			nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks, Begin);
			nghttp2_session_callbacks_set_on_header_callback(callbacks, Field);
			nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, Frame);
			nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, Data);
			nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, Close);
			nghttp2_session_callbacks_set_on_invalid_frame_recv_callback(callbacks, Invalid);
			nghttp2_session_callbacks_set_on_invalid_header_callback(callbacks, InvalidField);
			nghttp2_session_callbacks_set_on_frame_not_send_callback(callbacks, Unsent);
			nghttp2_mem memory{this, Allocate, Release, Zero, Resize};
			Result(nghttp2_session_client_new3(&session, callbacks, this, nullptr, &memory));
			const std::array<nghttp2_settings_entry, 4> settings{{
				{NGHTTP2_SETTINGS_ENABLE_PUSH, 0},
				{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 1},
				{NGHTTP2_SETTINGS_HEADER_TABLE_SIZE, 4096},
				{NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE, static_cast<uint32_t>(std::min(
					Count(channel.options.header_bytes), static_cast<std::size_t>(UINT32_MAX)))}
			}};
			Result(nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, settings.data(), settings.size()));
			std::vector<std::pair<std::string, std::string>> fields{
				{":method", std::string(std::string_view(request.method))},
				{":scheme", url.tls ? "https" : "http"},
				{":authority", url.authority}, {":path", url.target},
				{"content-length", std::to_string(Count(request.body.size()))}
			};
			for (const auto& header: request.headers)
				fields.emplace_back(Lower(std::string_view(header.name)), std::string(Trim(std::string_view(header.value))));
			std::vector<nghttp2_nv> headers;
			std::size_t field_bytes = 0;
			std::size_t field_count = 0;
			for (auto& [name, value]: fields) {
				Check(channel.control.Check());
				Account(field_bytes, name.size(), Count(channel.options.header_bytes));
				Account(field_bytes, value.size() + 32, Count(channel.options.header_bytes));
				Account(field_count, 1, Count(channel.options.header_fields));
				headers.push_back({reinterpret_cast<uint8_t*>(name.data()), reinterpret_cast<uint8_t*>(value.data()),
					name.size(), value.size(), NGHTTP2_NV_FLAG_NONE});
			}
			nghttp2_data_provider2 provider{};
			provider.read_callback = Upload;
			stream = nghttp2_submit_request2(session, nullptr, headers.data(), headers.size(),
				request.body.empty() ? nullptr : &provider, nullptr);
			Result(stream);
			std::array<std::byte, 16384> input{};
			while (!done) {
				Check(channel.control.Check());
				for (;;) {
					const uint8_t* output = nullptr;
					const auto bytes = nghttp2_session_mem_send2(session, &output);
					if (bytes < 0)
						Result(static_cast<int>(bytes));
					Result(0);
					if (!bytes)
						break;
					channel.Write(std::span(reinterpret_cast<const std::byte*>(output), static_cast<std::size_t>(bytes)));
				}
				if (done)
					break;
				if (!nghttp2_session_want_read(session))
					throw Error::HTTP2;
				const auto bytes = channel.Read(input);
				if (!bytes)
					throw Error::Truncated;
				const auto consumed = nghttp2_session_mem_recv2(session,
					reinterpret_cast<const uint8_t*>(input.data()), bytes);
				if (consumed < 0)
					Result(static_cast<int>(consumed));
				Result(0);
				if (static_cast<std::size_t>(consumed) != bytes)
					throw Error::HTTP2;
			}
			return std::move(response);
		}
	};
}

bool HTTP::Client::ConfigureTLS(const Crypto::TLS::ClientConfiguration& configuration,
	bool refresh_validation_time) noexcept {
	try {
		auto input = configuration;
		input.alpn_wire = {};
		auto copied = Safe::MakeShared<Detail::TLSConfiguration>(input, refresh_validation_time);
		if (!copied->Create())
			return false;
		m_tls = std::move(copied);
		return true;
	}
	catch (...) {
		return false;
	}
}

Expected<Response, HTTP::Error> HTTP::Client::Execute(const Request& request, const Options& options) {
	try {
		if (!m_resolver || options.timeout.count() <= 0 || options.timeout > std::chrono::hours{24}
			|| Count(options.header_bytes) == 0 || Count(options.header_fields) == 0
			|| Count(options.wire_bytes) == 0 || Count(options.url_bytes) == 0
			|| Count(options.http2_bytes) == 0
			|| (options.protocol != Protocol::Automatic && options.protocol != Protocol::HTTP1 && options.protocol != Protocol::HTTP2))
			throw Error::InvalidOptions;
		const Socket::OperationControl control{std::chrono::steady_clock::now() + options.timeout, options.cancellation};
		Check(control.Check());
		if (Count(request.url.size()) > Count(options.url_bytes))
			throw Error::LimitExceeded;
		const auto url = ParseURL(std::string_view(request.url));
		ValidateRequest(request, options, url);
		Channel channel{{Connection::Protocol::IPv4, {}}, {}, false, control, options};
		Check(channel.socket.Connect(url.host, url.port, *m_resolver, control, Connection::AddressPolicy::SystemOrder).state);
		bool h2 = options.protocol == Protocol::HTTP2;
		if (url.tls) {
			const std::string_view protocols = options.protocol == Protocol::HTTP1 ? std::string_view("\x08http/1.1", 9)
				: options.protocol == Protocol::HTTP2 ? std::string_view("\x02h2", 3) : std::string_view("\x02h2\x08http/1.1", 12);
			const auto alpn = std::as_bytes(std::span{protocols});
			auto configured = [&]() -> Expected<Crypto::TLS::Session, Crypto::TLS::SessionError> {
				if (m_tls)
					return m_tls->Create(alpn);
				Crypto::TLS::ClientConfiguration configuration;
				configuration.identity = Crypto::X509::SubjectAltName(
					url.ip ? Crypto::X509::SubjectAltNameType::IP : Crypto::X509::SubjectAltNameType::DNS,
					url.ip ? Safe::Binary(std::span(url.address).first(url.family == AF_INET ? 4 : 16))
						: Safe::Binary(std::as_bytes(std::span{url.host})));
				configuration.alpn_wire = alpn;
				return Crypto::TLS::Session::CreateClient(configuration);
			}();
			if (!configured)
				throw Error::TLS;
			channel.tls = std::move(*configured);
			const auto handshake = Socket::TLS::Handshake(channel.socket, channel.tls, control);
			if (handshake.truncated)
				throw Error::TLS;
			channel.CheckTLS(handshake);
			channel.encrypted = true;
			const auto negotiated = channel.tls.NegotiatedALPN();
			if (negotiated == "h2")
				h2 = true;
			else if (negotiated == "http/1.1" || negotiated.empty()) {
				if (options.protocol == Protocol::HTTP2)
					throw Error::Protocol;
				h2 = false;
			}
			else
				throw Error::Protocol;
			if (h2 && options.protocol == Protocol::HTTP1)
				throw Error::Protocol;
		}
		auto response = h2 ? H2(channel, request).Run(url) : HTTP1(channel, request, url);
		Check(control.Check());
		return response;
	}
	catch (Error error) {
		return Unexpected(error);
	}
	catch (...) {
		return Unexpected(Error::Allocation);
	}
}
