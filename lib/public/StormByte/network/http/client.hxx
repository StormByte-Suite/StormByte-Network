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

#pragma once

#include <StormByte/crypto/tls/session.hxx>
#include <StormByte/expected.hxx>
#include <StormByte/network/visibility.h>
#include <StormByte/safe/atomic.hxx>
#include <StormByte/safe/binary.hxx>
#include <StormByte/safe/pointers.hxx>
#include <StormByte/safe/string.hxx>
#include <StormByte/safe/vector.hxx>

#include <chrono>
#include <cstddef>
#include <span>
#include <string_view>

/**
 * @namespace StormByte
 * @brief Root namespace of the StormByte suite.
 */
namespace StormByte {
	/**
	 * @namespace StormByte::Network
	 * @brief Network module.
	 */
	namespace Network {
		/**
		 * @namespace StormByte::Network::Connection
		 * @brief Private connection implementation.
		 */
		namespace Connection {
			/**
			 * @brief Forward declaration of the provider-owned managed DNS slot.
			 */
			class Resolver;
		}
		/**
		 * @namespace StormByte::Network::Detail
		 * @brief Private provider-owned implementation.
		 */
		namespace Detail {
			/**
			 * @brief Forward declaration of immutable copied Crypto inputs.
			 */
			class TLSConfiguration;
		}
		/**
		 * @namespace StormByte::Network::HTTP
		 * @brief Bounded HTTP client API; no HTTP server or HTTP/3.
		 */
		namespace HTTP {
			/**
			 * @brief Typed terminal failures; no failed request is replayed.
			 */
			enum class Error {
				InvalidURL,	///< Invalid or unsupported absolute URL.
				InvalidRequest,	///< Invalid method, field or unsupported request feature.
				InvalidOptions,	///< Invalid limits, deadline, protocol or moved-from client.
				LimitExceeded,	///< An explicit request/response bound was exceeded.
				TimedOut,	///< Total request deadline expired.
				Cancelled,	///< Borrowed cancellation flag was set.
				ResolverBusy,	///< Previous managed DNS work is still running.
				Transport,	///< DNS, connect or native I/O failed.
				TLS,	///< TLS configuration, authentication or engine failure.
				Protocol,	///< Invalid HTTP semantics, framing or required ALPN.
				Truncated,	///< Incomplete message or bare TCP EOF on authenticated TLS.
				HTTP2,	///< HTTP/2 engine, frame, reset or GOAWAY failure.
				Allocation,	///< Provider-side allocation or callback construction failed.
				Rejected	///< The streaming Receiver declined the response or failed.
			};

			/**
			 * @brief Protocol selection. Automatic offers h2/http1.1 over HTTPS,
			 * and uses HTTP/1.1 for cleartext. HTTP2 requires h2 ALPN over HTTPS
			 * or uses explicit prior knowledge over cleartext, without fallback.
			 */
			enum class Protocol {
				Automatic,	///< HTTP/1 cleartext, h2/http1.1 HTTPS ALPN.
				HTTP1,	///< HTTP/1.1 only.
				HTTP2	///< Required h2 ALPN or cleartext prior knowledge.
			};

			/**
			 * @brief One header occurrence, preserving duplicates and received order.
			 * @details Lifecycle operations execute in Network; text is Base-owned.
			 */
			struct STORMBYTE_NETWORK_PUBLIC Header {
				/**
				 * @brief Copy borrowed header text into provider-owned storage.
				 * @param name Field name.
				 * @param value Field value.
				 * @throws StormByte::Safe::AllocationError Text storage cannot be allocated.
				 */
				Header(std::string_view name = {}, std::string_view value = {});
				/**
				 * @brief Independently copy both fields.
				 * @param other Source header.
				 */
				Header(const Header& other);
				/**
				 * @brief Transfer both fields, leaving empty source text.
				 * @param other Source header.
				 */
				Header(Header&& other) noexcept;
				/**
				 * @brief Release provider-owned text.
				 */
				~Header() noexcept;
				/**
				 * @brief Independently replace both fields.
				 * @param other Source header.
				 * @return This header.
				 */
				Header& operator=(const Header& other);
				/**
				 * @brief Transfer both fields.
				 * @param other Source header.
				 * @return This header.
				 */
				Header& operator=(Header&& other) noexcept;
				Safe::String name;	///< Field name, without colon.
				Safe::String value;	///< Value, without CR/LF.
			};
		}
	}
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::HTTP::Header);

/**
 * @namespace StormByte
 * @brief Root namespace of the StormByte suite.
 */
namespace StormByte {
	/**
	 * @namespace StormByte::Network
	 * @brief Network module.
	 */
	namespace Network {
		/**
		 * @namespace StormByte::Network::HTTP
		 * @brief Bounded HTTP client API.
		 */
		namespace HTTP {

			/**
			 * @brief Fully buffered request. Host, framing and connection headers are
			 * owned by the client; CONNECT, Upgrade and Expect are not supported.
			 * @details Lifecycle executes in Network; copies are independent, moved
			 * values have empty owning fields and can be reassigned.
			 */
			struct STORMBYTE_NETWORK_PUBLIC Request {
				/**
				 * @brief Construct a GET request for a borrowed absolute URL.
				 * @param url Absolute http or https URL.
				 * @throws StormByte::Safe::AllocationError Request text cannot be allocated.
				 */
				explicit Request(std::string_view url = {});
				/**
				 * @brief Independently copy the request.
				 * @param other Source request.
				 */
				Request(const Request& other);
				/**
				 * @brief Transfer request ownership.
				 * @param other Source request.
				 */
				Request(Request&& other) noexcept;
				/**
				 * @brief Release all provider-owned fields.
				 */
				~Request() noexcept;
				/**
				 * @brief Independently replace the request.
				 * @param other Source request.
				 * @return This request.
				 */
				Request& operator=(const Request& other);
				/**
				 * @brief Transfer request ownership.
				 * @param other Source request.
				 * @return This request.
				 */
				Request& operator=(Request&& other) noexcept;
				Safe::String url;	///< Absolute ASCII URL; fragments are rejected.
				Safe::String method;	///< Case-sensitive method token; defaults to GET.
				Safe::Vector<Header> headers;	///< Ordered duplicate-preserving user headers.
				Safe::Binary body;	///< Byte-exact request body, including NUL bytes.
			};

			/**
			 * @brief Fully buffered final response; informational responses are
			 * consumed, 3xx is returned unchanged, and body bytes are not decompressed.
			 * @details Independent copies and provider-side lifecycle; moved owning
			 * fields are empty. Network and Base must remain loaded until release.
			 */
			struct STORMBYTE_NETWORK_PUBLIC Response {
				/**
				 * @brief Construct an empty response.
				 */
				Response();
				/**
				 * @brief Independently copy the response.
				 * @param other Source response.
				 */
				Response(const Response& other);
				/**
				 * @brief Transfer response ownership.
				 * @param other Source response.
				 */
				Response(Response&& other) noexcept;
				/**
				 * @brief Release provider-owned fields.
				 */
				~Response() noexcept;
				/**
				 * @brief Independently replace the response.
				 * @param other Source response.
				 * @return This response.
				 */
				Response& operator=(const Response& other);
				/**
				 * @brief Transfer response ownership.
				 * @param other Source response.
				 * @return This response.
				 */
				Response& operator=(Response&& other) noexcept;
				unsigned short status{0};	///< Final three-digit status.
				Protocol protocol{Protocol::HTTP1};	///< Actual HTTP wire protocol.
				Safe::Vector<Header> headers;	///< Final fields, preserving duplicates.
				Safe::Vector<Header> trailers;	///< Validated trailing fields.
				Safe::Binary body;	///< Byte-exact entity body after transfer framing removal.
			};

			/**
			 * @brief Borrowed streaming consumer of the final response body.
			 * @details When Options::receiver is set, Begin is called once with the final
			 * status and fields before any body byte, including for bodyless responses.
			 * Every body byte is then passed to Write in order instead of being stored in
			 * Response::body, which stays empty; trailers are still returned. body_bytes
			 * bounds the total streamed body, so large downloads must raise body_bytes,
			 * wire_bytes and timeout explicitly. Returning false or throwing stops the
			 * exchange with Error::Rejected and closes the connection; bytes already
			 * written are not retracted. Callbacks run synchronously on the Execute
			 * thread and must not use the same client.
			 */
			class STORMBYTE_NETWORK_PUBLIC Receiver {
				public:
					/**
					 * @brief Construct a stateless receiver base.
					 */
					Receiver() noexcept = default;
					/**
					 * @brief Copy the stateless base.
					 * @param other Source receiver.
					 */
					Receiver(const Receiver& other) noexcept = default;
					/**
					 * @brief Move the stateless base.
					 * @param other Source receiver.
					 */
					Receiver(Receiver&& other) noexcept = default;
					/**
					 * @brief Destroy the receiver.
					 */
					virtual ~Receiver() noexcept;
					/**
					 * @brief Copy the stateless base.
					 * @param other Source receiver.
					 * @return This receiver.
					 */
					Receiver& operator=(const Receiver& other) noexcept = default;
					/**
					 * @brief Move the stateless base.
					 * @param other Source receiver.
					 * @return This receiver.
					 */
					Receiver& operator=(Receiver&& other) noexcept = default;
					/**
					 * @brief Accept or decline the final response before its body.
					 * @param status Final three-digit status.
					 * @param headers Final fields, preserving duplicates; borrowed for this call.
					 * @return Whether to continue receiving the body.
					 */
					virtual bool Begin(unsigned short status, const Safe::Vector<Header>& headers) = 0;
					/**
					 * @brief Consume the next nonempty body chunk.
					 * @param bytes Chunk borrowed for this call only.
					 * @return Whether to continue receiving.
					 */
					virtual bool Write(std::span<const std::byte> bytes) = 0;
			};

			/**
			 * @brief Scalar request limits. All bounds include informational responses
			 * and trailers; wire_bytes counts received HTTP plaintext, including framing.
			 * Cancellation is synchronously borrowed, checked during every phase, and
			 * wins over timeout. Timeout must be positive and at most 24 hours.
			 */
			struct Options {
				/**
				 * @brief Construct default scalar limits without allocation.
				 */
				Options() noexcept = default;

				/**
				 * @brief Copy limits and the synchronously borrowed cancellation pointer.
				 * @param other Source options.
				 */
				Options(const Options& other) noexcept = default;

				/**
				 * @brief Copy scalar controls; the source remains valid.
				 * @param other Source options.
				 */
				Options(Options&& other) noexcept = default;

				/**
				 * @brief Destroy scalar controls without releasing borrowed cancellation.
				 */
				~Options() noexcept = default;

				/**
				 * @brief Copy scalar controls.
				 * @param other Source options.
				 * @return This options value.
				 */
				Options& operator=(const Options& other) noexcept = default;

				/**
				 * @brief Copy scalar controls; the source remains valid.
				 * @param other Source options.
				 * @return This options value.
				 */
				Options& operator=(Options&& other) noexcept = default;

				Protocol protocol{Protocol::Automatic};	///< Protocol policy.
				std::chrono::milliseconds timeout{30000};	///< One total DNS-to-body deadline.
				const Safe::Atomic<bool>* cancellation{nullptr};	///< Borrowed until Execute returns.
				ByteSize header_bytes{65536};	///< Aggregate decoded headers, status and trailer bytes.
				Size header_fields{256};	///< Aggregate field count.
				Receiver* receiver{nullptr};	///< Borrowed until Execute returns; streams the final body.
				ByteSize body_bytes{16 * 1024 * 1024};	///< Maximum buffered or streamed body in either direction.
				ByteSize wire_bytes{32 * 1024 * 1024};	///< Maximum incoming wire plaintext.
				ByteSize http2_bytes{1024 * 1024};	///< Maximum live native HTTP/2 session allocation.
				ByteSize url_bytes{8192};	///< Maximum URL bytes.
				Size informational_responses{16};	///< Maximum interim responses.
			};

			/**
			 * @brief Synchronous single-request-per-connection client.
			 * @details No pooling, redirects, decompression, retries, proxy or server.
			 * Serialize Execute, ConfigureTLS, moves and destruction externally.
			 * Its managed resolver retains at most one DNS worker after timeout;
			 * subsequent calls may return ResolverBusy. Destruction joins that worker
			 * and can wait for the OS resolver independently of request deadlines.
			 * Network, Crypto, Base and logger providers must stay loaded until release.
			 */
			class STORMBYTE_NETWORK_PUBLIC Client final {
				public:
					/**
					 * @brief Allocate a managed resolver inside Network.
					 * @throws StormByte::Safe::AllocationError Resolver ownership cannot be allocated.
					 */
					Client();
					/**
					 * @brief Copying a resolver owner is disabled.
					 * @param other Source client.
					 */
					Client(const Client& other) = delete;
					/**
					 * @brief Transfer resolver and immutable TLS policy; moved-from
					 * clients can be reassigned but Execute returns InvalidOptions.
					 * @param other Source client.
					 */
					Client(Client&& other) noexcept;
					/**
					 * @brief Join retained DNS work and release provider-owned policy.
					 */
					~Client() noexcept;
					/**
					 * @brief Copy assignment is disabled.
					 * @param other Source client.
					 * @return No value; deleted.
					 */
					Client& operator=(const Client& other) = delete;
					/**
					 * @brief Transfer resolver and policy, joining this client's DNS work.
					 * @param other Source client.
					 * @return This client.
					 */
					Client& operator=(Client&& other) noexcept;
					/**
					 * @brief Deep-copy custom trust, identity, revocation and mTLS inputs.
					 * ALPN is controlled by HTTP Options, not the supplied alpn_wire.
					 * Failed configuration leaves the prior policy unchanged.
					 * @param configuration Synchronously borrowed Crypto inputs.
					 * @param refresh_validation_time Default true validates at each new session;
					 * false explicitly opts into the supplied fixed validation time.
					 * @return Whether the copied TLS configuration is valid.
					 */
					bool ConfigureTLS(const Crypto::TLS::ClientConfiguration& configuration,
						bool refresh_validation_time = true) noexcept;

					/**
					 * @brief Configure TLS with an owned explanation on failure.
					 * @param configuration Borrowed Crypto inputs.
					 * @param detail Output cleared on entry; copied category or allocation explanation on failure.
					 * @param refresh_validation_time Refresh validation time per session.
					 * @return Whether configuration succeeded; prior policy survives failure.
					 * @throws StormByte::Safe::AllocationError Explanation storage cannot be allocated.
					 */
					bool ConfigureTLS(const Crypto::TLS::ClientConfiguration& configuration,
						Safe::String& detail, bool refresh_validation_time = true);

					/**
					 * @brief Execute one bounded request, closing its connection on all exits.
					 * Default HTTPS verifies URL identity with bundled Crypto roots.
					 * Explicit identity configuration instead verifies that configured identity.
					 * @param request Synchronously borrowed immutable request.
					 * @param options Total deadline, protocol and buffer bounds.
					 * @return Final response or explicit failure; never partial success.
					 * With a Receiver, failure can follow bytes already delivered to it.
					 * @throws StormByte::Safe::AllocationError Error storage cannot be allocated.
					 */
					Expected<Response, Error> Execute(const Request& request, const Options& options = {});

					/**
					 * @brief Execute with an owned TLS failure explanation.
					 * @param request Borrowed request.
					 * @param options Total deadline, protocol and buffer bounds.
					 * @param detail Output cleared on entry; populated for TLS failures and unnotified TLS EOF.
					 * @return The same response or typed error as the existing overload.
					 * @throws StormByte::Safe::AllocationError Result or explanation storage cannot be allocated.
					 * @details The output survives session destruction. Non-TLS errors leave it empty.
					 * No class layout or error-enumeration changes are required.
					 */
					Expected<Response, Error> Execute(const Request& request, const Options& options, Safe::String& detail);

				private:
					Safe::Shared<Connection::Resolver> m_resolver;	///< Managed DNS slot.
					Safe::Shared<const Detail::TLSConfiguration> m_tls;	///< Immutable copied Crypto inputs.
			};
		}
	}
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::HTTP::Request);
STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::HTTP::Response);
STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::HTTP::Options);
STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::HTTP::Client);
