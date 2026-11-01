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
#include <StormByte/network/visibility.h>
#include <StormByte/safe/binary.hxx>
#include <StormByte/safe/vector.hxx>

/**
 * @namespace StormByte
 * @brief Root namespace of the StormByte suite.
 */
namespace StormByte {
	/**
	 * @namespace StormByte::Network
	 * @brief Network module of the StormByte suite.
	 */
	namespace Network {
		/**
		 * @namespace StormByte::Network::Detail
		 * @brief Network-owned implementation state.
		 */
		namespace Detail {
	/**
	 * @class TLSConfiguration
	 * @brief Immutable, deep-copied TLS construction inputs shared by a peer's channels.
	 * @details Sessions are never shared. Lifecycle operations run in Network.
	 */
	class STORMBYTE_NETWORK_PRIVATE TLSConfiguration final {
		public:
			/**
			 * @brief Copy every borrowed client input into Safe storage.
			 * @param configuration Synchronously borrowed Crypto input.
			 * @param refresh_validation_time Whether to refresh time at every session construction.
			 */
			explicit TLSConfiguration(const Crypto::TLS::ClientConfiguration& configuration,
				bool refresh_validation_time);

			/**
			 * @brief Copy every borrowed server input into Safe storage.
			 * @param configuration Synchronously borrowed Crypto input.
			 * @param refresh_validation_time Whether to refresh time at every session construction.
			 */
			explicit TLSConfiguration(const Crypto::TLS::ServerConfiguration& configuration,
				bool refresh_validation_time);

			/**
			 * @brief Share immutable snapshots instead of copying their owning object.
			 * @param other Snapshot that cannot be copied.
			 */
			TLSConfiguration(const TLSConfiguration& other) = delete;

			/**
			 * @brief Keep snapshots at their provider-allocated address.
			 * @param other Snapshot that cannot be moved.
			 */
			TLSConfiguration(TLSConfiguration&& other) = delete;

			/**
			 * @brief Prevent replacement of published immutable inputs.
			 * @param other Snapshot that cannot be assigned.
			 * @return This snapshot; operation is deleted.
			 */
			TLSConfiguration& operator=(const TLSConfiguration& other) = delete;

			/**
			 * @brief Prevent transfer into a published immutable snapshot.
			 * @param other Snapshot that cannot be assigned.
			 * @return This snapshot; operation is deleted.
			 */
			TLSConfiguration& operator=(TLSConfiguration&& other) = delete;

			/**
			 * @brief Release all copied inputs through their providers.
			 */
			~TLSConfiguration() noexcept;

			/**
			 * @brief Construct one independent Crypto stream from the retained snapshot.
			 * @param alpn Optional nonempty protocol list overriding the copied ALPN.
			 * @return New session or explicit Crypto configuration failure.
			 */
			Expected<Crypto::TLS::Session, Crypto::TLS::SessionError> Create(
				std::span<const std::byte> alpn = {}) const;

		private:
			Crypto::TLS::Role m_role; ///< Endpoint role.
			Crypto::X509::SubjectAltName m_identity; ///< Expected server identity.
			Crypto::TLS::TrustSource m_trust_source{Crypto::TLS::TrustSource::Custom}; ///< Exclusive root selection.
			Safe::Vector<Crypto::X509::Certificate> m_roots; ///< Custom trust anchors.
			Crypto::X509::TimePoint m_time; ///< Explicit validation instant.
			bool m_refresh_validation_time; ///< Refresh certificate/evidence validation at session creation.
			bool m_require_revocation{false}; ///< Require revocation evidence.
			Safe::Vector<Crypto::X509::RevocationFormat> m_evidence_formats; ///< Evidence formats.
			Safe::Vector<Safe::Binary> m_evidence; ///< Copied DER evidence.
			Safe::Binary m_chain; ///< Identity certificate chain.
			Safe::Binary m_key; ///< Identity private key.
			Safe::Binary m_password; ///< Key password.
			bool m_require_client{false}; ///< Require mutual authentication.
			Crypto::TLS::Configuration m_policy; ///< Independent policy lists.
			Safe::Binary m_alpn; ///< Protocol preferences.
	};
		}
	}
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Detail::TLSConfiguration);
