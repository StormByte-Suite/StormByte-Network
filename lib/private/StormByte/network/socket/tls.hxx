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
#include <StormByte/network/socket/client.hxx>

/**
 * @namespace StormByte::Network::Socket::TLS
 * @brief Private socket scheduling for Crypto's buffer-driven TLS engine.
 *
 * Socket and session are synchronous borrows and must be externally serialized.
 * Every non-complete result closes the socket and empties the session, except
 * an authenticated PeerClosed on read. Failed operations cannot be retried on
 * the same TLS stream: drained ciphertext and accepted plaintext are not replayable.
 */
namespace StormByte::Network::Socket::TLS {
	/**
	 * @brief Transport outcome with an optional explicit Crypto failure.
	 */
	struct Result {
		OperationState state = OperationState::Complete;	///< Operation outcome.
		StormByte::ByteSize bytes{0};	///< Plaintext accepted or delivered.
		int native_error = 0;	///< Captured OS error, if any.
		bool tls_failure = false;	///< Whether error is meaningful.
		Crypto::TLS::SessionError error = Crypto::TLS::SessionError::InvalidState;	///< Crypto error.
		bool truncated = false;	///< TCP EOF without an authenticated TLS close-notify.
	};

	/**
	 * @brief Complete and authenticate a TLS handshake on an already connected socket.
	 * @param socket Borrowed connected socket.
	 * @param session Borrowed configured Crypto client or server session.
	 * @param control Total deadline, including preceding DNS and connect.
	 * @return Authenticated completion or an explicit terminal failure.
	 */
	STORMBYTE_NETWORK_PRIVATE Result Handshake(Client& socket,
		Crypto::TLS::Session& session, const OperationControl& control);

	/**
	 * @brief Encrypt and transmit the entire plaintext span.
	 * @param socket Borrowed connected socket.
	 * @param session Borrowed authenticated session.
	 * @param plaintext Borrowed application bytes.
	 * @param control Caller-owned total operation deadline and cancellation.
	 * @return Plaintext accepted before completion or failure; never replay on failure.
	 */
	STORMBYTE_NETWORK_PRIVATE Result Write(Client& socket, Crypto::TLS::Session& session,
		std::span<const std::byte> plaintext, const OperationControl& control);

	/**
	 * @brief Receive at least one plaintext byte, or authenticated TLS EOF.
	 * @param socket Borrowed connected socket.
	 * @param session Borrowed authenticated session.
	 * @param output Nonempty borrowed destination; discard its contents on failure.
	 * @param control Caller-owned total operation deadline and cancellation.
	 * @return Delivered bytes, authenticated PeerClosed, or terminal failure.
	 * @details Bare TCP EOF without TLS close-notify is an error, not successful EOF.
	 */
	STORMBYTE_NETWORK_PRIVATE Result Read(Client& socket, Crypto::TLS::Session& session,
		std::span<std::byte> output, const OperationControl& control);

	/**
	 * @brief Exchange TLS close-notify and then close the socket.
	 * @param socket Borrowed connected socket.
	 * @param session Borrowed authenticated session.
	 * @param control Caller-owned total shutdown deadline and cancellation.
	 * @return Orderly shutdown or terminal failure.
	 */
	STORMBYTE_NETWORK_PRIVATE Result Shutdown(Client& socket,
		Crypto::TLS::Session& session, const OperationControl& control);
}
