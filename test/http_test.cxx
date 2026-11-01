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

#include <StormByte/network/http/client.hxx>
#include <StormByte/network/socket/server.hxx>
#include <StormByte/network/socket/tls.hxx>

#include "tls_fixture.hxx"

#include <nghttp2/nghttp2.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace StormByte;
using namespace StormByte::Network;
using namespace std::chrono_literals;
namespace HT = StormByte::Network::HTTP;
namespace CT = StormByte::Crypto::TLS;
namespace X509 = StormByte::Crypto::X509;

namespace {
/**
 * @brief Preserve peer-thread errors as test diagnostics.
 * @param condition Required invariant.
 * @param message Diagnostic.
 */
void Require(bool condition, const char* message) {
	if (!condition)
		throw std::runtime_error(message);
}

/**
 * @brief Compare provider-owned body bytes with binary text.
 * @param body Actual bytes.
 * @param text Expected bytes.
 * @return Whether byte-exact.
 */
bool Equal(const Safe::Binary& body, std::string_view text) {
	const auto bytes = std::as_bytes(std::span{text});
	return std::ranges::equal(body.span(), bytes);
}

/**
 * @brief Check a typed HTTP failure.
 * @param result Actual outcome.
 * @param error Required failure.
 */
void Failure(const Expected<HT::Response, HT::Error>& result, HT::Error error) {
	if (result)
		throw std::runtime_error("unexpected HTTP success");
	if (*result.error() != error)
		throw std::runtime_error("wrong HTTP error: " + std::to_string(static_cast<int>(*result.error()))
			+ ", expected " + std::to_string(static_cast<int>(error)));
}

/**
 * @brief Offline custom CA and optionally mutual identity, copied by the public API.
 * @param client Client under test.
 * @param identity Expected DNS SAN.
 * @param current Whether to refresh time at each request.
 * @param invalid_time Whether to select an expired fixed time.
 * @param mutual Whether to supply the offline client identity.
 */
void Configure(HT::Client& client, std::string_view identity = "localhost", bool current = false,
	bool invalid_time = false, bool mutual = false) {
	auto root = X509::Load(TLSFixture::Load("ca.pem").span()).value();
	auto leaf = X509::Load(TLSFixture::Load("server.pem").span()).value();
	auto chain = TLSFixture::Load(mutual ? "client.pem" : "server.pem");
	auto key = TLSFixture::Load(mutual ? "client-test-key.pem" : "server-test-key.pem");
	CT::ClientConfiguration configuration;
	configuration.identity = X509::SubjectAltName(X509::SubjectAltNameType::DNS,
		Safe::Binary{std::as_bytes(std::span{identity})});
	configuration.trust_source = CT::TrustSource::Custom;
	configuration.trust_anchors = std::span{&root, 1};
	configuration.validation_time = invalid_time ? leaf.NotAfter() + 1s : leaf.NotBefore() + 60s;
	if (mutual) {
		configuration.certificate_chain_pem = chain.span();
		configuration.private_key_pem = key.span();
	}
	Require(client.ConfigureTLS(configuration, current), "HTTP custom TLS configuration");
}

/**
 * @brief Bounded test-side plain/TLS channel built on Network, not OpenSSL.
 */
struct Peer {
	Socket::Client& socket;	///< Accepted test socket.
	CT::Session tls;	///< Test-owned Crypto session.
	bool encrypted{false};	///< Test wire mode.
	Socket::OperationControl control{std::chrono::steady_clock::now() + 5s};	///< Peer bound.

	/**
	 * @brief Always close accepted sockets, including failing fixture paths.
	 */
	~Peer() {
		socket.Disconnect();
	}

	/**
	 * @brief Transmit exact plaintext.
	 * @param bytes Test bytes.
	 */
	void Write(std::span<const std::byte> bytes) {
		if (encrypted)
			Require(Socket::TLS::Write(socket, tls, bytes, control).state == Socket::OperationState::Complete, "peer TLS write");
		else
			Require(socket.Send(bytes, control).state == Socket::OperationState::Complete, "peer write");
	}

	/**
	 * @brief Transmit test text.
	 * @param text Text or binary string view.
	 */
	void Write(std::string_view text) {
		Write(std::as_bytes(std::span{text}));
	}

	/**
	 * @brief Read one bounded chunk.
	 * @param output Writable bytes.
	 * @return Received count.
	 */
	std::size_t Read(std::span<std::byte> output) {
		if (encrypted) {
			const auto result = Socket::TLS::Read(socket, tls, output, control);
			if (result.state == Socket::OperationState::PeerClosed)
				return 0;
			Require(result.state == Socket::OperationState::Complete, "peer TLS read");
			return static_cast<std::size_t>(result.bytes);
		}
		for (;;) {
			Require(control.Check() == Socket::OperationState::Complete, "peer read deadline");
			const auto result = socket.ReadSome(output);
			if (result.state == Socket::OperationState::PeerClosed)
				return 0;
			if (result.state == Socket::OperationState::WouldBlock) {
				Require(Socket::WaitReadable(socket.Handle(), control).state == Socket::OperationState::Complete, "peer read readiness");
				continue;
			}
			Require(result.state == Socket::OperationState::Complete, "peer read");
			return static_cast<std::size_t>(result.bytes);
		}
	}

	/**
	 * @brief Read a complete test request with a known body size.
	 * @param body_size Expected upload size.
	 * @return Entire request wire text.
	 */
	std::string Request(std::size_t body_size = 0) {
		std::array<std::byte, 16384> bytes{};
		std::string request;
		for (;;) {
			const auto count = Read(bytes);
			Require(count != 0, "request EOF");
			request.append(reinterpret_cast<const char*>(bytes.data()), count);
			const auto end = request.find("\r\n\r\n");
			if (end != std::string::npos && request.size() >= end + 4 + body_size)
				return request;
		}
	}
};

/**
 * @brief Ephemeral loopback server with fully contained and joined peer execution.
 */
struct Fixture {
	Socket::Server server{Connection::Protocol::IPv4, {}};	///< Provider-side listener.
	bool secure;	///< HTTPS mode.
	std::string alpn;	///< Test protocol preferences.
	bool mutual;	///< Require a client certificate.
	std::exception_ptr error;	///< Worker failure.
	std::jthread worker;	///< Joined before listener destruction.

	/**
	 * @brief Bind a loopback endpoint without fixed ports.
	 * @param secure Whether to handshake TLS.
	 * @param alpn Length-prefixed server protocol preferences.
	 * @param mutual Whether mTLS is required.
	 * @param ipv6 Whether to bind IPv6.
	 */
	Fixture(bool secure = false, std::string alpn = std::string("\x08http/1.1", 9),
		bool mutual = false, bool ipv6 = false):
		server(ipv6 ? Connection::Protocol::IPv6 : Connection::Protocol::IPv4, {}),
		secure(secure), alpn(std::move(alpn)), mutual(mutual) {
		Require(static_cast<bool>(server.Listen(ipv6 ? "::1" : "127.0.0.1", 0)), "HTTP loopback listen");
	}

	/**
	 * @brief Join worker before releasing listener and captured test state.
	 */
	~Fixture() {
		if (worker.joinable())
			worker.join();
	}

	/**
	 * @brief Absolute test URL.
	 * @param path Origin target.
	 * @param host Host name or bracketed address.
	 * @return Endpoint URL.
	 */
	std::string URL(std::string_view path = "/", std::string_view host = "127.0.0.1") const {
		return std::string(secure ? "https://" : "http://") + std::string(host) + ":"
			+ std::to_string(server.Port()) + std::string(path);
	}

	/**
	 * @brief Start one bounded server flow; no exceptions cross thread or C boundaries.
	 * @param action Self-contained synchronous peer flow.
	 */
	void Start(std::function<void(Peer&)> action) {
		worker = std::jthread([this, action = std::move(action)] {
			try {
				const Socket::OperationControl control{std::chrono::steady_clock::now() + 5s};
				Require(Socket::WaitReadable(server.Handle(), control).state == Socket::OperationState::Complete, "HTTP accept wait");
				auto accepted = server.Accept();
				Require(static_cast<bool>(accepted), "HTTP accept");
				Peer peer{**accepted};
				if (secure) {
					auto chain = TLSFixture::Load("server.pem");
					auto key = TLSFixture::Load("server-test-key.pem");
					auto root = X509::Load(TLSFixture::Load(mutual ? "client-ca.pem" : "ca.pem").span()).value();
					auto leaf = X509::Load(chain.span()).value();
					CT::ServerConfiguration config;
					config.certificate_chain_pem = chain.span();
					config.private_key_pem = key.span();
					config.alpn_wire = std::as_bytes(std::span{alpn});
					config.require_client_certificate = mutual;
					config.validation_time = mutual ? root.NotBefore() + 60s : leaf.NotBefore() + 60s;
					if (mutual)
						config.client_trust_anchors = std::span{&root, 1};
					auto session = CT::Session::CreateServer(config);
					Require(static_cast<bool>(session), "peer TLS configuration");
					peer.tls = std::move(*session);
					Require(Socket::TLS::Handshake(peer.socket, peer.tls, control).state == Socket::OperationState::Complete, "peer TLS handshake");
					peer.encrypted = true;
				}
				action(peer);
				peer.socket.Disconnect();
			}
			catch (...) {
				error = std::current_exception();
			}
		});
	}

	/**
	 * @brief Join and verify success-path peer errors.
	 * @param expected_failure Whether client intentionally aborted transport.
	 */
	void Finish(bool expected_failure = false) {
		worker.join();
		if (error && !expected_failure)
			std::rethrow_exception(error);
	}
};

/**
 * @brief Native nghttp2 server used only as a protocol fixture, not public server API.
 */
struct H2Peer {
	enum class Fault { None, Reset, Goaway, Length, Header, Trailer };	///< Injected failure.
	Peer& peer;	///< Test channel.
	std::string body;	///< Binary response.
	Fault fault;	///< Test scenario.
	nghttp2_session* session{nullptr};	///< Owned native fixture engine.
	nghttp2_session_callbacks* callbacks{nullptr};	///< Owned callbacks.
	std::vector<std::pair<std::string, std::string>> headers;	///< Decoded request headers.
	std::string upload;	///< Decoded request DATA.
	std::size_t sent{0};	///< Response prefix.
	bool replied{false};	///< Response submitted.
	bool closed{false};	///< End of exchange.
	bool failed{false};	///< Callback failure.
	unsigned short status{200};	///< Final fixture status.
	bool bodyless{false};	///< HEAD/204/304 fixture semantics.

	/**
	 * @brief Set fixture response.
	 * @param peer Borrowed channel.
	 * @param body Binary response.
	 * @param fault Error to inject.
	 */
	H2Peer(Peer& peer, std::string body, Fault fault = Fault::None): peer(peer), body(std::move(body)), fault(fault) {}

	/**
	 * @brief Release native fixture resources.
	 */
	~H2Peer() {
		nghttp2_session_del(session);
		nghttp2_session_callbacks_del(callbacks);
	}

	/**
	 * @brief Contain every callback exception.
	 * @param user Fixture context.
	 * @param action Borrowed callback operation.
	 * @return Native callback result.
	 */
	template<class F>
	static int Guard(void* user, F action) noexcept {
		auto& self = *static_cast<H2Peer*>(user);
		try {
			action(self);
			return 0;
		}
		catch (...) {
			self.failed = true;
			return NGHTTP2_ERR_CALLBACK_FAILURE;
		}
	}

	/**
	 * @brief Retain request headers.
	 */
	static int Header(nghttp2_session*, const nghttp2_frame*, const uint8_t* name, size_t namesize,
		const uint8_t* value, size_t valuesize, uint8_t, void* user) noexcept {
		return Guard(user, [=](H2Peer& self) {
			self.headers.emplace_back(std::string(reinterpret_cast<const char*>(name), namesize),
				std::string(reinterpret_cast<const char*>(value), valuesize));
		});
	}

	/**
	 * @brief Retain exact upload DATA.
	 */
	static int Data(nghttp2_session*, uint8_t, int32_t, const uint8_t* bytes, size_t count, void* user) noexcept {
		return Guard(user, [=](H2Peer& self) { self.upload.append(reinterpret_cast<const char*>(bytes), count); });
	}

	/**
	 * @brief Submit deterministic headers using nghttp2's actual HPACK encoder.
	 * @param fields Header list.
	 * @return Encoded native fields borrowing the list.
	 */
	static std::vector<nghttp2_nv> Fields(std::vector<std::pair<std::string, std::string>>& fields) {
		std::vector<nghttp2_nv> native;
		for (auto& [name, value]: fields)
			native.push_back({reinterpret_cast<uint8_t*>(name.data()), reinterpret_cast<uint8_t*>(value.data()),
				name.size(), value.size(), NGHTTP2_NV_FLAG_NONE});
		return native;
	}

	/**
	 * @brief Respond after the entire request has arrived, preserving upload checks.
	 */
	static int Frame(nghttp2_session*, const nghttp2_frame* frame, void* user) noexcept {
		return Guard(user, [frame](H2Peer& self) {
			if (self.replied || !(frame->hd.flags & NGHTTP2_FLAG_END_STREAM)
				|| (frame->hd.type != NGHTTP2_HEADERS && frame->hd.type != NGHTTP2_DATA))
				return;
			self.replied = true;
			if (self.fault == Fault::Reset) {
				Require(nghttp2_submit_rst_stream(self.session, 0, frame->hd.stream_id, NGHTTP2_CANCEL) == 0, "RST submit");
				return;
			}
			if (self.fault == Fault::Goaway) {
				Require(nghttp2_submit_goaway(self.session, 0, 0, NGHTTP2_NO_ERROR, nullptr, 0) == 0, "GOAWAY submit");
				return;
			}
			std::vector<std::pair<std::string, std::string>> early{{":status", "103"}, {"link", "</style.css>"}};
			auto native = Fields(early);
			Require(nghttp2_submit_headers(self.session, 0, frame->hd.stream_id, nullptr, native.data(), native.size(), nullptr) == 0,
				"interim submit");
			std::vector<std::pair<std::string, std::string>> fields{
				{":status", std::to_string(self.status)}, {"set-cookie", "a=1"}, {"set-cookie", "b=2"}
			};
			if (self.status != 204)
				fields.emplace_back("content-length", std::to_string(self.body.size() + (self.fault == Fault::Length ? 1 : 0)));
			if (self.fault == Fault::Header)
				fields.emplace_back("bad", "bad\rvalue");
			native = Fields(fields);
			nghttp2_data_provider provider{};
			provider.read_callback = Body;
			Require(nghttp2_submit_response(self.session, frame->hd.stream_id, native.data(), native.size(),
				self.bodyless ? nullptr : &provider) == 0,
				"response submit");
		});
	}

	/**
	 * @brief Flow-controlled large response and final trailers.
	 */
	static ssize_t Body(nghttp2_session*, int32_t stream, uint8_t* output, size_t size, uint32_t* flags,
		nghttp2_data_source*, void* user) noexcept {
		std::size_t count = 0;
		const auto result = Guard(user, [&](H2Peer& self) {
			count = std::min(size, self.body.size() - self.sent);
			if (count)
				std::memcpy(output, self.body.data() + self.sent, count);
			self.sent += count;
			if (self.sent == self.body.size()) {
				*flags |= NGHTTP2_DATA_FLAG_EOF | NGHTTP2_DATA_FLAG_NO_END_STREAM;
				std::vector<std::pair<std::string, std::string>> fields{
					{self.fault == Fault::Trailer ? "content-length" : "x-checksum", self.fault == Fault::Trailer ? "0" : "exact"}
				};
				const auto native = Fields(fields);
				Require(nghttp2_submit_trailer(self.session, stream, native.data(), native.size()) == 0, "trailer submit");
			}
		});
		return result ? NGHTTP2_ERR_CALLBACK_FAILURE : static_cast<ssize_t>(count);
	}

	/**
	 * @brief Observe fixture stream completion.
	 */
	static int Close(nghttp2_session*, int32_t, uint32_t, void* user) noexcept {
		static_cast<H2Peer*>(user)->closed = true;
		return 0;
	}

	/**
	 * @brief Drive nghttp2 SETTINGS, HPACK and automatic flow-control updates.
	 */
	void Run() {
		Require(nghttp2_session_callbacks_new(&callbacks) == 0, "server callbacks");
		nghttp2_session_callbacks_set_on_header_callback(callbacks, Header);
		nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, Data);
		nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, Frame);
		nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, Close);
		Require(nghttp2_session_server_new(&session, callbacks, this) == 0, "server session");
		const nghttp2_settings_entry setting{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 1};
		Require(nghttp2_submit_settings(session, 0, &setting, 1) == 0, "server SETTINGS");
		std::array<std::byte, 16384> input{};
		while (!closed) {
			for (;;) {
				const uint8_t* output;
				const auto count = nghttp2_session_mem_send(session, &output);
				Require(count >= 0 && !failed, "server mem send");
				if (!count)
					break;
				peer.Write(std::span(reinterpret_cast<const std::byte*>(output), static_cast<std::size_t>(count)));
			}
			if (closed)
				break;
			const auto count = peer.Read(input);
			Require(count != 0, "server HTTP/2 EOF");
			const auto consumed = nghttp2_session_mem_recv(session, reinterpret_cast<const uint8_t*>(input.data()), count);
			Require(consumed == static_cast<ssize_t>(count) && !failed, "server mem recv");
		}
		// A fresh-connection client closes after its final response. Drain its
		// remaining WINDOW_UPDATE/ACK bytes first; closing with unread TCP input
		// would reset the connection and race the final encrypted response.
		for (;;) {
			const auto result = peer.socket.ReadSome(input);
			if (result.state == Socket::OperationState::PeerClosed || result.state == Socket::OperationState::Error)
				break;
			if (result.state == Socket::OperationState::WouldBlock)
				Require(Socket::WaitReadable(peer.socket.Handle(), peer.control).state == Socket::OperationState::Complete,
					"HTTP/2 peer orderly completion wait");
		}
	}
};

/**
 * @brief Execute one raw response and verify a terminal failure.
 * @param response Wire bytes.
 * @param error Required failure.
 * @param options Request bounds.
 */
void RawFailure(std::string response, HT::Error error, HT::Options options = {}) {
	Fixture fixture;
	fixture.Start([response = std::move(response)](Peer& peer) {
		peer.Request();
		peer.Write(response);
	});
	HT::Client client;
	Failure(client.Execute(HT::Request(fixture.URL()), options), error);
	fixture.Finish(true);
}

// -------------------
// HTTP1
// -------------------
int test_http1_bodyless() {
	for (const auto& [method, response]: std::vector<std::pair<std::string, std::string>>{
		{"HEAD", "HTTP/1.1 200 OK\r\nContent-Length: 999999999\r\n\r\n"},
		{"GET", "HTTP/1.1 204 No Content\r\n\r\n"},
		{"GET", "HTTP/1.1 304 Not Modified\r\nContent-Length: 999999999\r\n\r\n"},
		{"GET", "HTTP/1.1 205 Reset Content\r\nContent-Length: 0\r\n\r\n"}
	}) {
		Fixture fixture;
		fixture.Start([response](Peer& peer) {
			peer.Request();
			peer.Write(response);
		});
		HT::Client client;
		HT::Request request(fixture.URL());
		request.method = method;
		const auto result = client.Execute(request);
		Require(result && result->body.empty(), "bodyless HTTP/1");
		fixture.Finish();
	}
	return 0;
}

int test_http1_chunked_informational_trailers() {
	Fixture fixture;
	fixture.Start([](Peer& peer) {
		peer.Request();
		const std::string response = "HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 103 Early Hints\r\nLink: </a>\r\n\r\n"
			"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nSet-Cookie: a\r\nSet-Cookie: b\r\n\r\n"
			"3; foo=\"a\\\"b\"\r\nabc\r\n2\r\n";
		for (const char byte: response)
			peer.Write(std::string_view(&byte, 1));
		peer.Write(std::string_view("\0z", 2));
		peer.Write("\r\n0\r\nX-Checksum: done\r\n\r\n");
	});
	HT::Client client;
	const auto result = client.Execute(HT::Request(fixture.URL()));
	Require(result && result->status == 200 && Equal(result->body, std::string_view("abc\0z", 5)), "chunked binary response");
	Require(result->headers.size() == 3 && result->trailers.size() == 1, "duplicate fields and trailers");
	fixture.Finish();
	return 0;
}

int test_http1_close_delimited_and_redirect() {
	Fixture fixture;
	fixture.Start([](Peer& peer) {
		peer.Request();
		peer.Write("HTTP/1.0 302 Found\r\nLocation: /next\r\n\r\nunchanged");
	});
	HT::Client client;
	const auto result = client.Execute(HT::Request(fixture.URL()));
	Require(result && result->status == 302 && Equal(result->body, "unchanged"), "HTTP/1.0 close delimited 3xx");
	fixture.Finish();
	return 0;
}

int test_http1_fixed_binary_and_lifecycle() {
	Fixture fixture;
	fixture.Start([](Peer& peer) {
		const auto wire = peer.Request(4);
		Require(wire.starts_with("POST /?a=b HTTP/1.1\r\n"), "default path and query");
		Require(wire.ends_with(std::string("a\0bc", 4)), "exact binary upload");
		Require(wire.find("X-Duplicate: a\r\nX-Duplicate: b\r\n") != std::string::npos, "duplicate request fields");
		peer.Write("HTTP/1.1 200 OK\r\nContent-Length: 4, 4\r\nContent-Length: 4\r\n\r\n");
		peer.Write(std::string_view("d\0ef", 4));
	});
	HT::Client original;
	HT::Client client(std::move(original));
	Failure(original.Execute(HT::Request("http://localhost/")), HT::Error::InvalidOptions);
	HT::Request source(fixture.URL("?a=b"));
	source.method = std::string_view("POST");
	source.headers.emplace_back("X-Duplicate", "a");
	source.headers.emplace_back("X-Duplicate", "b");
	source.body = Safe::Binary(std::as_bytes(std::span{std::string_view("a\0bc", 4)}));
	HT::Request copy(source);
	source.url.clear();
	HT::Request request(std::move(copy));
	auto result = client.Execute(request);
	Require(result && Equal(result->body, std::string_view("d\0ef", 4)), "fixed body bytes");
	HT::Response response(*result);
	result->body.clear();
	HT::Response moved(std::move(response));
	Require(Equal(moved.body, std::string_view("d\0ef", 4)), "independent response lifecycle");
	fixture.Finish();
	return 0;
}

int test_http1_ipv6() {
	Fixture fixture(false, {}, false, true);
	fixture.Start([](Peer& peer) {
		peer.Request();
		peer.Write("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
	});
	HT::Client client;
	Require(static_cast<bool>(client.Execute(HT::Request(fixture.URL("/", "[::1]")))), "IPv6 URL");
	fixture.Finish();
	return 0;
}

int test_http1_limits() {
	HT::Options options;
	options.body_bytes = ByteSize{2};
	RawFailure("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc", HT::Error::LimitExceeded, options);
	RawFailure("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\n", HT::Error::LimitExceeded, options);
	RawFailure("HTTP/1.1 200 OK\r\n\r\nabc", HT::Error::LimitExceeded, options);
	options = {};
	options.header_bytes = ByteSize{128};
	RawFailure("HTTP/1.1 200 OK\r\nX: " + std::string(200, 'a') + "\r\n\r\n", HT::Error::LimitExceeded, options);
	options = {};
	options.header_fields = Size{3};
	RawFailure("HTTP/1.1 200 OK\r\nA: 1\r\nB: 2\r\nC: 3\r\nD: 4\r\n\r\n", HT::Error::LimitExceeded, options);
	options = {};
	options.informational_responses = Size{0};
	RawFailure("HTTP/1.1 100 Continue\r\n\r\n", HT::Error::LimitExceeded, options);
	options = {};
	options.wire_bytes = ByteSize{40};
	RawFailure("HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nabcd", HT::Error::LimitExceeded, options);
	return 0;
}

int test_http1_malformed() {
	for (const auto& response: {
		"HTTP/1.1 200 OK\n\n",
		"HTTP/1.1 20x OK\r\n\r\n",
		"HTTP/1.1 200 OK\r\nBad Header: x\r\n\r\n",
		"HTTP/1.1 200 OK\r\n Folded: x\r\n\r\n",
		"HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\nab",
		"HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n",
		"HTTP/1.1 200 OK\r\nContent-Length: 1,\r\n\r\n",
		"HTTP/1.1 200 OK\r\nContent-Length: 99999999999999999999999999999\r\n\r\n",
		"HTTP/1.1 200 OK\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n",
		"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nTransfer-Encoding: chunked\r\n\r\n",
		"HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n\r\n",
		"HTTP/1.0 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n",
		"HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n",
		"HTTP/1.1 101 Switching Protocols\r\n\r\n",
		"HTTP/1.1 100 Continue\r\nContent-Length: 0\r\n\r\n",
		"HTTP/1.1 205 Reset\r\nContent-Length: 1\r\n\r\nx",
		"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nZ\r\n",
		"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n 1\r\nx\r\n",
		"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1;foo=\"open\r\nx\r\n",
		"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nxBAD\r\n",
		"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nContent-Length: 0\r\n\r\n"
	})
		RawFailure(response, HT::Error::Protocol);
	return 0;
}

int test_http1_truncated() {
	for (const auto& response: {
		"", "HTTP/1.1 200", "HTTP/1.1 200 OK\r\n",
		"HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nab",
		"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nab",
		"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nx\r\n",
		"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nX: y\r\n"
	})
		RawFailure(response, HT::Error::Truncated);
	return 0;
}

// -------------------
// HTTP2
// -------------------
int test_http2_bodyless() {
	for (const auto& [method, status]: std::vector<std::pair<std::string, unsigned short>>{
		{"HEAD", 200}, {"GET", 204}, {"GET", 304}
	}) {
		Fixture fixture;
		fixture.Start([status](Peer& peer) {
			H2Peer server(peer, std::string(1000, 'x'));
			server.status = status;
			server.bodyless = true;
			server.Run();
		});
		HT::Client client;
		HT::Request request(fixture.URL());
		request.method = method;
		HT::Options options;
		options.protocol = HT::Protocol::HTTP2;
		options.body_bytes = ByteSize{0};
		const auto result = client.Execute(request, options);
		Require(result && result->status == status && result->body.empty(), "HTTP/2 bodyless response");
		fixture.Finish();
	}
	return 0;
}

int test_http2_closed_stream_reset() {
	Fixture fixture;
	fixture.Start([](Peer& peer) {
		std::array<std::byte, 16384> input{};
		Require(peer.Read(input) != 0, "HTTP/2 preface before final response");
		// An END_STREAM :status=200, immediately followed by RST_STREAM for
		// that already closed stream, is valid and must not undo the response.
		peer.Write(std::string_view("\0\0\0\x04\0\0\0\0\0"
			"\0\0\x01\x01\x05\0\0\0\x01\x88"
			"\0\0\x04\x03\0\0\0\0\x01\0\0\0\0", 32));
		while (peer.socket.ReadSome(input).state != Socket::OperationState::PeerClosed) {
			const auto ready = Socket::WaitReadable(peer.socket.Handle(), peer.control);
			Require(ready.state == Socket::OperationState::Complete, "closed-stream fixture completion");
		}
	});
	HT::Client client;
	HT::Options options;
	options.protocol = HT::Protocol::HTTP2;
	const auto result = client.Execute(HT::Request(fixture.URL()), options);
	Require(result && result->status == 200 && result->body.empty(), "closed HTTP/2 stream reset must be ignored");
	fixture.Finish();
	return 0;
}

int test_http2_errors_and_bounds() {
	for (const auto fault: {H2Peer::Fault::Reset, H2Peer::Fault::Goaway, H2Peer::Fault::Length,
		H2Peer::Fault::Header, H2Peer::Fault::Trailer}) {
		Fixture fixture;
		fixture.Start([fault](Peer& peer) {
			H2Peer server(peer, "body", fault);
			server.Run();
		});
		HT::Options options;
		options.protocol = HT::Protocol::HTTP2;
		HT::Client client;
		const auto result = client.Execute(HT::Request(fixture.URL()), options);
		Require(!result, "HTTP/2 protocol failure must not succeed");
		Require(*result.error() != HT::Error::TimedOut, "HTTP/2 errors must fail before deadline");
		fixture.Finish(true);
	}
	for (int limit = 0; limit < 3; ++limit) {
		Fixture fixture;
		fixture.Start([](Peer& peer) {
			H2Peer server(peer, std::string(10000, 'x'));
			server.Run();
		});
		HT::Options options;
		options.protocol = HT::Protocol::HTTP2;
		if (limit == 0)
			options.body_bytes = ByteSize{100};
		else if (limit == 1)
			options.header_bytes = ByteSize{250};
		else
			options.wire_bytes = ByteSize{200};
		HT::Client client;
		Failure(client.Execute(HT::Request(fixture.URL()), options), HT::Error::LimitExceeded);
		fixture.Finish(true);
	}
	return 0;
}

int test_http2_flow_control_clear_and_tls() {
	std::string response(500000, '\0');
	std::string upload(180000, '\0');
	for (std::size_t index = 0; index < response.size(); ++index)
		response[index] = static_cast<char>(index % 251);
	for (std::size_t index = 0; index < upload.size(); ++index)
		upload[index] = static_cast<char>((index * 7) % 251);
	for (const bool secure: {false, true}) {
		Fixture fixture(secure, std::string("\x02h2", 3));
		fixture.Start([&](Peer& peer) {
			if (secure)
				Require(peer.tls.NegotiatedALPN() == "h2", "actual server h2 ALPN");
			H2Peer server(peer, response);
			server.Run();
			Require(server.upload == upload, "HTTP/2 byte-exact flow-controlled upload");
			Require(std::ranges::find(server.headers, std::pair<std::string, std::string>{":path", "/upload?q=1"})
				!= server.headers.end(), "HTTP/2 path");
			Require(std::ranges::count(server.headers, std::pair<std::string, std::string>{"x-duplicate", "same"}) == 2,
				"HTTP/2 request duplicate fields");
		});
		HT::Client client;
		if (secure)
			Configure(client);
		HT::Request request(fixture.URL("/upload?q=1", "localhost"));
		request.method = std::string_view("POST");
		request.body = Safe::Binary(std::as_bytes(std::span{upload}));
		request.headers.emplace_back("X-Duplicate", "same");
		request.headers.emplace_back("X-Duplicate", "same");
		HT::Options options;
		options.protocol = secure ? HT::Protocol::Automatic : HT::Protocol::HTTP2;
		auto result = client.Execute(request, options);
		if (!result)
			throw std::runtime_error("HTTP/2 large response failed (" + std::to_string(static_cast<int>(*result.error()))
				+ "), secure=" + std::to_string(secure));
		Require(result->protocol == HT::Protocol::HTTP2 && result->status == 200 && Equal(result->body, response),
			"HTTP/2 response beyond initial 65535-byte window");
		Require(result->headers.size() == 3 && result->trailers.size() == 1
			&& std::string_view(result->trailers[0].value) == "exact", "HTTP/2 duplicate fields/trailers");
		fixture.Finish();
	}
	return 0;
}

int test_http2_native_allocation_bound() {
	Fixture fixture;
	Safe::Atomic<bool> received{false};
	fixture.Start([&](Peer& peer) {
		std::array<std::byte, 16384> input{};
		Require(peer.Read(input) != 0, "HTTP/2 request before malicious HPACK fixture");
		received.store(true);
		// SETTINGS, then :status=200 and a literal declaring a 65536-byte Huffman
		// value without supplying it. nghttp2 must not allocate beyond the cap.
		peer.Write(std::string_view("\0\0\0\x04\0\0\0\0\0", 9));
		peer.Write(std::string_view("\0\0\x08\x01\x04\0\0\0\x01", 9));
		peer.Write(std::string_view("\x88\0\x01x\xff\x81\xff\x03", 8));
		std::this_thread::sleep_for(100ms);
	});
	HT::Client client;
	HT::Options options;
	options.protocol = HT::Protocol::HTTP2;
	options.http2_bytes = ByteSize{65536};
	Failure(client.Execute(HT::Request(fixture.URL()), options), HT::Error::LimitExceeded);
	fixture.Finish(true);
	Require(received.load(), "HTTP/2 native allocation test must reach HPACK decoding");
	return 0;
}

// -------------------
// HTTPS
// -------------------
int test_https_alpn_and_current_time() {
	for (int mode = 0; mode < 3; ++mode) {
		Fixture fixture(true, mode == 2 ? std::string{} : std::string("\x08http/1.1", 9));
		fixture.Start([](Peer& peer) {
			peer.Request();
			peer.Write("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\n");
			peer.Write(std::string_view("a\0b", 3));
		});
		HT::Client client;
		Configure(client, "localhost", true);
		HT::Options options;
		if (mode == 1)
			options.protocol = HT::Protocol::HTTP1;
		const auto result = client.Execute(HT::Request(fixture.URL("/", "localhost")), options);
		Require(result && result->protocol == HT::Protocol::HTTP1 && Equal(result->body, std::string_view("a\0b", 3)),
			"authenticated HTTP/1 ALPN/current time");
		fixture.Finish();
	}
	Fixture fixture(true);
	fixture.Start([](Peer&) {});
	HT::Client client;
	Configure(client);
	HT::Options options;
	options.protocol = HT::Protocol::HTTP2;
	const auto result = client.Execute(HT::Request(fixture.URL()), options);
	Require(!result && *result.error() != HT::Error::TimedOut, "h2 policy cannot downgrade");
	fixture.Finish(true);
	return 0;
}

int test_https_authenticated_close_delimited() {
	Fixture fixture(true);
	fixture.Start([](Peer& peer) {
		peer.Request();
		peer.Write("HTTP/1.1 200 OK\r\n\r\nbody");
		Require(static_cast<bool>(peer.tls.Shutdown()), "queue server close-notify");
		std::array<std::byte, 16384> encrypted{};
		for (;;) {
			auto drained = peer.tls.DrainEncrypted(encrypted);
			Require(static_cast<bool>(drained), "drain server close-notify");
			if (!*drained)
				break;
			Require(peer.socket.Send(std::span(encrypted).first(*drained), peer.control).state == Socket::OperationState::Complete,
				"send server close-notify");
		}
	});
	HT::Client client;
	Configure(client);
	const auto result = client.Execute(HT::Request(fixture.URL()));
	Require(result && Equal(result->body, "body"), "authenticated TLS close-delimited body");
	fixture.Finish();
	return 0;
}

int test_https_mutual_authentication() {
	for (bool identity: {false, true}) {
		Fixture fixture(true, std::string("\x08http/1.1", 9), true);
		fixture.Start([](Peer& peer) {
			peer.Request();
			peer.Write("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
		});
		HT::Client client;
		Configure(client, "localhost", false, false, identity);
		const auto result = client.Execute(HT::Request(fixture.URL()));
		if (identity)
			Require(static_cast<bool>(result), "deep-copied mTLS identity");
		else
			Require(!result && (*result.error() == HT::Error::TLS || *result.error() == HT::Error::Truncated),
				"missing mTLS identity must fail authentication or detect unnotified TLS EOF");
		fixture.Finish(!identity);
	}
	return 0;
}

int test_https_truncated_close_delimited() {
	Fixture fixture(true);
	fixture.Start([](Peer& peer) {
		peer.Request();
		peer.Write("HTTP/1.1 200 OK\r\n\r\nbody");
	});
	HT::Client client;
	Configure(client);
	Failure(client.Execute(HT::Request(fixture.URL())), HT::Error::Truncated);
	fixture.Finish();
	return 0;
}

int test_https_trust_and_identity_failure() {
	for (int mode = 0; mode < 3; ++mode) {
		Fixture fixture(true);
		fixture.Start([](Peer& peer) { peer.Request(); });
		HT::Client client;
		if (mode == 1)
			Configure(client, "wrong.invalid");
		else if (mode == 2)
			Configure(client, "localhost", false, true);
		Failure(client.Execute(HT::Request(fixture.URL())), HT::Error::TLS);
		fixture.Finish(true);
	}
	HT::Client client;
	Configure(client);
	CT::ClientConfiguration invalid;
	invalid.trust_source = CT::TrustSource::Custom;
	Require(!client.ConfigureTLS(invalid), "invalid TLS policy must be rejected");
	Fixture fixture(true);
	fixture.Start([](Peer& peer) {
		peer.Request();
		peer.Write("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
	});
	Require(static_cast<bool>(client.Execute(HT::Request(fixture.URL()))), "failed TLS configure preserves previous policy");
	fixture.Finish();
	return 0;
}

// -------------------
// Validation
// -------------------
int test_validation_deadline_and_cancel() {
	HT::Client client;
	Safe::Atomic<bool> cancelled{true};
	HT::Options options;
	options.cancellation = &cancelled;
	Failure(client.Execute(HT::Request("http://localhost/"), options), HT::Error::Cancelled);
	for (const bool secure: {false, true}) {
		Fixture fixture(secure);
		fixture.Start([](Peer& peer) {
			peer.Request();
			std::this_thread::sleep_for(150ms);
		});
		HT::Client local;
		if (secure)
			Configure(local);
		options = {};
		options.timeout = 50ms;
		const auto start = std::chrono::steady_clock::now();
		Failure(local.Execute(HT::Request(fixture.URL()), options), HT::Error::TimedOut);
		Require(std::chrono::steady_clock::now() - start < 500ms, "total deadline must not restart");
		fixture.Finish(true);
	}
	Fixture fixture;
	fixture.Start([](Peer& peer) {
		peer.Request();
		std::this_thread::sleep_for(150ms);
	});
	cancelled.store(false);
	options = {};
	options.cancellation = &cancelled;
	std::jthread canceller([&] {
		std::this_thread::sleep_for(40ms);
		cancelled.store(true);
	});
	Failure(client.Execute(HT::Request(fixture.URL()), options), HT::Error::Cancelled);
	fixture.Finish(true);
	// A plain peer that never answers TLS demonstrates the same deadline in handshake.
	Fixture stall;
	stall.Start([](Peer&) { std::this_thread::sleep_for(150ms); });
	HT::Request tls_request("https://127.0.0.1:" + std::to_string(stall.server.Port()) + "/");
	options = {};
	options.timeout = 40ms;
	Failure(client.Execute(tls_request, options), HT::Error::TimedOut);
	stall.Finish(true);
	return 0;
}

int test_validation_invalid_options_and_requests() {
	HT::Client client;
	HT::Options options;
	options.timeout = 0ms;
	Failure(client.Execute(HT::Request("http://localhost/"), options), HT::Error::InvalidOptions);
	options = {};
	options.protocol = static_cast<HT::Protocol>(999);
	Failure(client.Execute(HT::Request("http://localhost/"), options), HT::Error::InvalidOptions);
	options = {};
	options.url_bytes = ByteSize{3};
	Failure(client.Execute(HT::Request("http://localhost/"), options), HT::Error::LimitExceeded);
	options = {};
	options.body_bytes = ByteSize{1};
	HT::Request oversized("http://localhost/");
	oversized.body.resize(ByteSize{2});
	Failure(client.Execute(oversized, options), HT::Error::LimitExceeded);
	options = {};
	options.header_fields = Size{3};
	HT::Request fields("http://localhost/");
	fields.headers.emplace_back("X-Test", "value");
	Failure(client.Execute(fields, options), HT::Error::LimitExceeded);
	for (const auto name: {"Host", "Content-Length", "Transfer-Encoding", "Connection", "Upgrade", "Expect",
		"TE", "Trailer", "Proxy-Connection", "bad name", "bad\r\nname"}) {
		HT::Request request("http://localhost/");
		request.headers.emplace_back(name, "value");
		Failure(client.Execute(request), HT::Error::InvalidRequest);
	}
	for (const auto value: {std::string("a\r\nInjected: x"), std::string("a\0b", 3), std::string("bad\x7f")}) {
		HT::Request request("http://localhost/");
		request.headers.emplace_back("X-Test", value);
		Failure(client.Execute(request), HT::Error::InvalidRequest);
	}
	for (const auto method: {"CONNECT", "", "bad method", "GET\r\n"}) {
		HT::Request request("http://localhost/");
		request.method = std::string_view(method);
		Failure(client.Execute(request), HT::Error::InvalidRequest);
	}
	return 0;
}

int test_validation_invalid_urls() {
	HT::Client client;
	for (const auto url: {"", "/", "ftp://localhost/", "http://", "http://u:p@localhost/", "http://localhost:0/",
		"http://localhost:65536/", "http://localhost:/", "http://localhost:-1/", "http://[::1/",
		"http://::1/", "http://[not-ip]/", "http://[::1]junk/", "http://[::1%lo]/", "http://localhost/%zz",
		"http://localhost/%", "http://localhost/#fragment", "http://localhost/\r\nInjected: yes",
		"http://local host/", "http://localhost\\other/", "http://-bad/", "http://bad..host/", "http://./",
		"http://localhost/<tag>", "http://localhost/{bad}"})
		Failure(client.Execute(HT::Request(url)), HT::Error::InvalidURL);
	Failure(client.Execute(HT::Request(std::string_view("http://localhost/\0x", 19))), HT::Error::InvalidURL);
	return 0;
}

int test_validation_upload_and_h2_controls() {
	for (const bool secure: {false, true}) {
		for (const bool cancel: {false, true}) {
			Fixture fixture(secure);
			fixture.Start([](Peer&) { std::this_thread::sleep_for(150ms); });
			HT::Client client;
			if (secure)
				Configure(client);
			HT::Request request(fixture.URL());
			request.method = std::string_view("POST");
			request.body.resize(ByteSize{16 * 1024 * 1024});
			Safe::Atomic<bool> cancelled{false};
			HT::Options options;
			options.timeout = cancel ? 2s : 40ms;
			options.cancellation = &cancelled;
			std::jthread canceller;
			if (cancel)
				canceller = std::jthread([&] {
					std::this_thread::sleep_for(40ms);
					cancelled.store(true);
				});
			Failure(client.Execute(request, options), cancel ? HT::Error::Cancelled : HT::Error::TimedOut);
			fixture.Finish(true);
		}
		Fixture fixture(secure, std::string("\x02h2", 3));
		fixture.Start([](Peer&) { std::this_thread::sleep_for(150ms); });
		HT::Client client;
		if (secure)
			Configure(client);
		HT::Options options;
		options.protocol = HT::Protocol::HTTP2;
		options.timeout = 40ms;
		Failure(client.Execute(HT::Request(fixture.URL()), options), HT::Error::TimedOut);
		fixture.Finish(true);
	}
	return 0;
}
}

/**
 * @brief Canonical test registration with stable per-case diagnostics.
 */
int main() {
	int failures = 0;
	int count = 0;
	const auto Run = [&](const char* name, int (*test)()) {
		++count;
		try {
			failures += test();
			std::cout << name << ": passed\n";
		}
		catch (const std::exception& error) {
			++failures;
			std::cerr << name << ": " << error.what() << '\n';
		}
	};
	// -------------------
	// HTTP1
	// -------------------
	Run("test_http1_bodyless", test_http1_bodyless);
	Run("test_http1_chunked_informational_trailers", test_http1_chunked_informational_trailers);
	Run("test_http1_close_delimited_and_redirect", test_http1_close_delimited_and_redirect);
	Run("test_http1_fixed_binary_and_lifecycle", test_http1_fixed_binary_and_lifecycle);
	Run("test_http1_ipv6", test_http1_ipv6);
	Run("test_http1_limits", test_http1_limits);
	Run("test_http1_malformed", test_http1_malformed);
	Run("test_http1_truncated", test_http1_truncated);
	// -------------------
	// HTTP2
	// -------------------
	Run("test_http2_bodyless", test_http2_bodyless);
	Run("test_http2_closed_stream_reset", test_http2_closed_stream_reset);
	Run("test_http2_errors_and_bounds", test_http2_errors_and_bounds);
	Run("test_http2_flow_control_clear_and_tls", test_http2_flow_control_clear_and_tls);
	Run("test_http2_native_allocation_bound", test_http2_native_allocation_bound);
	// -------------------
	// HTTPS
	// -------------------
	Run("test_https_alpn_and_current_time", test_https_alpn_and_current_time);
	Run("test_https_authenticated_close_delimited", test_https_authenticated_close_delimited);
	Run("test_https_mutual_authentication", test_https_mutual_authentication);
	Run("test_https_truncated_close_delimited", test_https_truncated_close_delimited);
	Run("test_https_trust_and_identity_failure", test_https_trust_and_identity_failure);
	// -------------------
	// Validation
	// -------------------
	Run("test_validation_deadline_and_cancel", test_validation_deadline_and_cancel);
	Run("test_validation_invalid_options_and_requests", test_validation_invalid_options_and_requests);
	Run("test_validation_invalid_urls", test_validation_invalid_urls);
	Run("test_validation_upload_and_h2_controls", test_validation_upload_and_h2_controls);
	std::cout << count << " HTTP cases, " << failures << " failures\n";
	return failures;
}
