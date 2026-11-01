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

#include <StormByte/network/socket/tls.hxx>

#include <array>
#include <exception>

using namespace StormByte;
using namespace StormByte::Network::Socket;
using CryptoSession = Crypto::TLS::Session;
using CryptoState = Crypto::TLS::State;

namespace {
	/**
	 * @brief Close a borrowed stream if a Crypto allocation throws mid-operation.
	 */
	struct ExceptionGuard {
		Client& socket;	///< Borrowed socket.
		CryptoSession& session;	///< Borrowed TLS session.
		int exceptions = std::uncaught_exceptions();	///< Entry exception depth.

		/**
		 * @brief Abandon irreversibly consumed ciphertext during stack unwinding.
		 */
		~ExceptionGuard() noexcept {
			if (std::uncaught_exceptions() > exceptions) {
				socket.Disconnect();
				session = CryptoSession{};
			}
		}
	};

	/**
	 * @brief Abandon a stream after irreversible partial TLS or socket progress.
	 * @param socket Borrowed socket to close.
	 * @param session Borrowed session to empty.
	 * @param result Explicit failure to propagate.
	 * @return The supplied failure.
	 */
	TLS::Result Abort(Client& socket, CryptoSession& session, TLS::Result result) {
		socket.Disconnect();
		session = CryptoSession{};
		return result;
	}

	/**
	 * @brief Convert a Crypto error to a transport failure.
	 * @param error Typed Crypto failure.
	 * @return Explicit TLS error.
	 */
	TLS::Result Failure(Crypto::TLS::SessionError error) {
		return {OperationState::Error, ByteSize{0}, 0, true, error};
	}

	/**
	 * @brief Flush all currently pending ciphertext without resetting the deadline.
	 * @param socket Borrowed connected socket.
	 * @param session Borrowed Crypto session.
	 * @param control Total operation controls.
	 * @return Complete or a socket/Crypto failure.
	 */
	TLS::Result Flush(Client& socket, CryptoSession& session, const OperationControl& control) {
		std::array<std::byte, 16384> encrypted{};
		for (;;) {
			const auto state = control.Check();
			if (state != OperationState::Complete)
				return {state};
			auto drained = session.DrainEncrypted(encrypted);
			if (!drained)
				return Failure(*drained.error());
			if (*drained == 0)
				return {};
			const auto sent = socket.Send(std::span(encrypted).first(*drained), control);
			if (sent.state != OperationState::Complete)
				return {sent.state, ByteSize{0}, sent.native_error};
		}
	}

	/**
	 * @brief Receive and feed a bounded ciphertext chunk, preserving partial consumption.
	 * @param socket Borrowed connected socket.
	 * @param session Borrowed session.
	 * @param control Total operation controls.
	 * @return Complete or an explicit failure, including truncated TCP EOF.
	 */
	TLS::Result Receive(Client& socket, CryptoSession& session, const OperationControl& control) {
		std::array<std::byte, 16384> encrypted{};
		OperationResult received;
		for (;;) {
			const auto state = control.Check();
			if (state != OperationState::Complete)
				return {state};
			received = socket.ReadSome(encrypted);
			if (received.state != OperationState::WouldBlock)
				break;
			const auto ready = WaitReadable(socket.Handle(), control);
			if (ready.state != OperationState::Complete)
				return {ready.state, ByteSize{0}, ready.native_error};
		}
		if (received.state == OperationState::PeerClosed)
			return {OperationState::Error, ByteSize{0}, 0, false,
				Crypto::TLS::SessionError::InvalidState, true};
		if (received.state != OperationState::Complete)
			return {received.state, ByteSize{0}, received.native_error};
		auto input = std::span<const std::byte>(encrypted).first(static_cast<std::size_t>(received.bytes));
		while (!input.empty()) {
			const auto state = control.Check();
			if (state != OperationState::Complete)
				return {state};
			auto fed = session.FeedEncrypted(input);
			if (!fed)
				return Failure(*fed.error());
			if (*fed == 0)
				return Failure(Crypto::TLS::SessionError::BufferLimit);
			input = input.subspan(*fed);
		}
		return {};
	}
}

TLS::Result TLS::Handshake(Client& socket, CryptoSession& session, const OperationControl& control) {
	const ExceptionGuard guard{socket, session};
	if (socket.Status() != Network::Connection::Status::Connected)
		return Abort(socket, session, {OperationState::Error});
	for (;;) {
		auto flushed = Flush(socket, session, control);
		if (flushed.state != OperationState::Complete)
			return Abort(socket, session, flushed);
		if (session.Status() == CryptoState::Open)
			return {};
		if (session.Status() != CryptoState::Handshaking)
			return Abort(socket, session, Failure(session.Failure()));
		auto received = Receive(socket, session, control);
		if (received.state != OperationState::Complete)
			return Abort(socket, session, received);
	}
}

TLS::Result TLS::Write(Client& socket, CryptoSession& session,
	std::span<const std::byte> plaintext, const OperationControl& control) {
	const ExceptionGuard guard{socket, session};
	ByteSize total{0};
	if (socket.Status() != Network::Connection::Status::Connected)
		return Abort(socket, session, {OperationState::Error});
	if (session.Status() != CryptoState::Open)
		return Abort(socket, session, Failure(Crypto::TLS::SessionError::InvalidState));
	do {
		const auto state = control.Check();
		if (state != OperationState::Complete)
			return Abort(socket, session, {state, total});
		auto written = session.WritePlaintext(plaintext);
		if (!written) {
			auto result = Failure(*written.error());
			result.bytes = total;
			return Abort(socket, session, result);
		}
		total += ByteSize{*written};
		plaintext = plaintext.subspan(*written);
		auto flushed = Flush(socket, session, control);
		if (flushed.state != OperationState::Complete) {
			flushed.bytes = total;
			return Abort(socket, session, flushed);
		}
		if (!plaintext.empty() && *written == 0) {
			auto received = Receive(socket, session, control);
			if (received.state != OperationState::Complete) {
				received.bytes = total;
				return Abort(socket, session, received);
			}
		}
	} while (!plaintext.empty());
	return {OperationState::Complete, total};
}

TLS::Result TLS::Read(Client& socket, CryptoSession& session,
	std::span<std::byte> output, const OperationControl& control) {
	const ExceptionGuard guard{socket, session};
	if (socket.Status() != Network::Connection::Status::Connected)
		return Abort(socket, session, {OperationState::Error});
	if (output.empty() || !session.HandshakeComplete())
		return Abort(socket, session, Failure(Crypto::TLS::SessionError::InvalidState));
	for (;;) {
		const auto state = control.Check();
		if (state != OperationState::Complete)
			return Abort(socket, session, {state});
		auto read = session.ReadPlaintext(output);
		if (!read)
			return Abort(socket, session, Failure(*read.error()));
		auto flushed = Flush(socket, session, control);
		if (flushed.state != OperationState::Complete)
			return Abort(socket, session, flushed);
		if (*read != 0)
			return {OperationState::Complete, ByteSize{*read}};
		if (session.Status() == CryptoState::Closing || session.Status() == CryptoState::Closed)
			return {OperationState::PeerClosed};
		auto received = Receive(socket, session, control);
		if (received.state != OperationState::Complete)
			return Abort(socket, session, received);
	}
}

TLS::Result TLS::Shutdown(Client& socket, CryptoSession& session, const OperationControl& control) {
	const ExceptionGuard guard{socket, session};
	if (socket.Status() != Network::Connection::Status::Connected)
		return Abort(socket, session, {OperationState::Error});
	for (;;) {
		const auto state = control.Check();
		if (state != OperationState::Complete)
			return Abort(socket, session, {state});
		auto shutdown = session.Shutdown();
		if (!shutdown)
			return Abort(socket, session, Failure(*shutdown.error()));
		auto flushed = Flush(socket, session, control);
		if (flushed.state != OperationState::Complete)
			return Abort(socket, session, flushed);
		if (session.Status() == CryptoState::Closed) {
			socket.Disconnect();
			return {};
		}
		auto received = Receive(socket, session, control);
		if (received.state != OperationState::Complete)
			return Abort(socket, session, received);
	}
}
