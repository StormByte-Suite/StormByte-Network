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

#include <StormByte/network/tls_configuration.hxx>

#include <vector>

using namespace StormByte;
using namespace StormByte::Network::Detail;

TLSConfiguration::TLSConfiguration(const Crypto::TLS::ClientConfiguration& configuration,
	bool refresh_validation_time):
	m_role(Crypto::TLS::Role::Client), m_identity(configuration.identity),
	m_trust_source(configuration.trust_source),
	m_roots(configuration.trust_anchors.begin(), configuration.trust_anchors.end()),
	m_time(configuration.validation_time), m_refresh_validation_time(refresh_validation_time),
	m_require_revocation(configuration.require_revocation),
	m_chain(configuration.certificate_chain_pem), m_key(configuration.private_key_pem),
	m_password(configuration.private_key_password), m_policy(configuration.policy),
	m_alpn(configuration.alpn_wire) {
	for (const auto& evidence: configuration.revocation_evidence) {
		m_evidence_formats.push_back(evidence.format);
		m_evidence.emplace_back(evidence.der);
	}
}

TLSConfiguration::TLSConfiguration(const Crypto::TLS::ServerConfiguration& configuration,
	bool refresh_validation_time):
	m_role(Crypto::TLS::Role::Server),
	m_roots(configuration.client_trust_anchors.begin(), configuration.client_trust_anchors.end()),
	m_time(configuration.validation_time), m_refresh_validation_time(refresh_validation_time),
	m_chain(configuration.certificate_chain_pem),
	m_key(configuration.private_key_pem), m_password(configuration.private_key_password),
	m_require_client(configuration.require_client_certificate), m_policy(configuration.policy),
	m_alpn(configuration.alpn_wire) {}

TLSConfiguration::~TLSConfiguration() noexcept = default;

Expected<Crypto::TLS::Session, Crypto::TLS::SessionError> TLSConfiguration::Create(
	std::span<const std::byte> alpn) const {
	if (m_role == Crypto::TLS::Role::Client) {
		Crypto::TLS::ClientConfiguration configuration;
		configuration.identity = m_identity;
		configuration.trust_source = m_trust_source;
		configuration.trust_anchors = static_cast<std::span<const Crypto::X509::Certificate>>(m_roots);
		configuration.validation_time = m_refresh_validation_time
			? std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()) : m_time;
		configuration.require_revocation = m_require_revocation;
		std::vector<Crypto::X509::RevocationEvidence> evidence;
		for (std::size_t index = 0; index < static_cast<std::size_t>(m_evidence.size()); ++index)
			evidence.push_back({m_evidence_formats[index], m_evidence[index].span()});
		configuration.revocation_evidence = evidence;
		configuration.certificate_chain_pem = m_chain.span();
		configuration.private_key_pem = m_key.span();
		configuration.private_key_password = m_password.span();
		configuration.policy = m_policy;
		configuration.alpn_wire = alpn.empty() ? m_alpn.span() : alpn;
		return Crypto::TLS::Session::CreateClient(configuration);
	}
	Crypto::TLS::ServerConfiguration configuration;
	configuration.certificate_chain_pem = m_chain.span();
	configuration.private_key_pem = m_key.span();
	configuration.private_key_password = m_password.span();
	configuration.require_client_certificate = m_require_client;
	configuration.client_trust_anchors = static_cast<std::span<const Crypto::X509::Certificate>>(m_roots);
	configuration.validation_time = m_refresh_validation_time
		? std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()) : m_time;
	configuration.policy = m_policy;
	configuration.alpn_wire = alpn.empty() ? m_alpn.span() : alpn;
	return Crypto::TLS::Session::CreateServer(configuration);
}
