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

#include <StormByte/network/client.hxx>
#include <StormByte/network/server.hxx>

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

/**
 * @namespace TLSFixture
 * @brief Disposable offline TLS material; never production identities.
 */
namespace TLSFixture {
	/**
	 * @brief Load a repository fixture into provider-owned bytes.
	 * @param name Fixture filename.
	 * @return PEM bytes.
	 */
	inline StormByte::Safe::Binary Load(const char* name) {
		std::ifstream file(std::string(TLS_FIXTURE_DIRECTORY) + "/" + name, std::ios::binary);
		if (!file)
			throw std::runtime_error("TLS fixture missing");
		const std::string text{std::istreambuf_iterator<char>(file), {}};
		return StormByte::Safe::Binary{std::as_bytes(std::span{text})};
	}

	/**
	 * @brief Configure a client, proving borrowed inputs need not outlive the call.
	 * @param client Application client.
	 * @param identity Expected DNS identity.
	 * @param refresh_validation_time Whether to exercise Network's current-time default.
	 * @param invalid_time Zero for fixture time, negative for before validity, positive for after expiry.
	 * @return Whether the deep-copied configuration was accepted.
	 */
	inline bool Configure(StormByte::Network::Client& client, std::string_view identity = "localhost",
		bool refresh_validation_time = false, int invalid_time = 0) {
		namespace X509 = StormByte::Crypto::X509;
		auto root = X509::Load(Load("ca.pem").span()).value();
		auto leaf = X509::Load(Load("server.pem").span()).value();
		StormByte::Crypto::TLS::ClientConfiguration configuration;
		configuration.identity = X509::SubjectAltName(X509::SubjectAltNameType::DNS,
			StormByte::Safe::Binary{std::as_bytes(std::span{identity})});
		configuration.trust_source = StormByte::Crypto::TLS::TrustSource::Custom;
		configuration.trust_anchors = std::span{&root, 1};
		configuration.validation_time = leaf.NotBefore() + std::chrono::seconds{60};
		if (invalid_time < 0)
			configuration.validation_time = leaf.NotBefore() - std::chrono::seconds{60};
		else if (invalid_time > 0)
			configuration.validation_time = leaf.NotAfter() + std::chrono::seconds{60};
		if (refresh_validation_time)
			return client.ConfigureTLS(configuration);
		return client.ConfigureTLS(configuration, false);
	}

	/**
	 * @brief Configure a server from disposable copied key and certificate bytes.
	 * @param server Application server.
	 * @param require_client Whether client certificate authentication is required.
	 * @return Whether identity and policy were accepted.
	 */
	inline bool Configure(StormByte::Network::Server& server, bool require_client = false) {
		auto chain = Load("server.pem");
		auto key = Load("server-test-key.pem");
		auto root = StormByte::Crypto::X509::Load(Load("ca.pem").span()).value();
		auto leaf = StormByte::Crypto::X509::Load(chain.span()).value();
		StormByte::Crypto::TLS::ServerConfiguration configuration;
		configuration.certificate_chain_pem = chain.span();
		configuration.private_key_pem = key.span();
		configuration.require_client_certificate = require_client;
		if (require_client)
			configuration.client_trust_anchors = std::span{&root, 1};
		configuration.validation_time = leaf.NotBefore() + std::chrono::seconds{60};
		return server.ConfigureTLS(configuration, false);
	}
}
