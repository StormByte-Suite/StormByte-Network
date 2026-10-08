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

#include <StormByte/network/connection/client.hxx>
#include <StormByte/safe/unique_lock.hxx>

#include <utility>

using namespace StormByte::Network::Connection;

Client::Client(StormByte::Safe::Shared<Network::Socket::Client> socket, Buffer::Pipeline in_pipeline, Buffer::Pipeline out_pipeline) noexcept:
	m_socket(std::move(socket)),
	m_in_pipeline(std::move(in_pipeline)),
	m_out_pipeline(std::move(out_pipeline))
{}

Client::Client(Client&& other) noexcept:
	m_socket(std::move(other.m_socket)),
	m_in_pipeline(std::move(other.m_in_pipeline)),
	m_out_pipeline(std::move(other.m_out_pipeline)),
	m_file_input(std::move(other.m_file_input)),
	m_file_output(std::move(other.m_file_output)),
	m_configured(std::exchange(other.m_configured, false))
{}

Client::~Client() noexcept = default;

Client& Client::operator=(Client&& other) noexcept {
	if (this != &other) {
		m_socket = std::move(other.m_socket);
		m_in_pipeline = std::move(other.m_in_pipeline);
		m_out_pipeline = std::move(other.m_out_pipeline);
		m_file_input = std::move(other.m_file_input);
		m_file_output = std::move(other.m_file_output);
		m_configured = std::exchange(other.m_configured, false);
	}
	return *this;
}

Client::Pointer Client::Create(StormByte::Safe::Shared<Network::Socket::Client> socket, Buffer::Pipeline input, Buffer::Pipeline output) noexcept {
	return StormByte::Safe::MakeShared<Client>(std::move(socket), std::move(input), std::move(output));
}

bool Client::ConfigurePipelines(Buffer::Pipeline input, Buffer::Pipeline output) {
	StormByte::Safe::UniqueLock lock(m_configuration_mutex);
	if (m_configured)
		return false;
	Buffer::Pipeline file_input(input);
	Buffer::Pipeline file_output(output);
	m_in_pipeline = std::move(input);
	m_out_pipeline = std::move(output);
	m_file_input = std::move(file_input);
	m_file_output = std::move(file_output);
	m_configured = true;
	return true;
}

StormByte::Safe::Pair<StormByte::Buffer::Pipeline, StormByte::Buffer::Pipeline> Client::FilePipelines() {
	StormByte::Safe::UniqueLock lock(m_configuration_mutex);
	StormByte::Safe::Pair<StormByte::Buffer::Pipeline, StormByte::Buffer::Pipeline> result{m_file_input, m_file_output};
	m_configured = true;
	return result;
}

StormByte::Buffer::Pipeline& Client::InputPipeline() noexcept {
	return m_in_pipeline;
}

StormByte::Buffer::Pipeline& Client::OutputPipeline() noexcept {
	return m_out_pipeline;
}

StormByte::Safe::Shared<StormByte::Network::Socket::Client>& Client::Socket() noexcept {
	return m_socket;
}

StormByte::Network::Connection::Status Client::Status() const noexcept {
	return m_socket ? m_socket->Status() : Connection::Status::Disconnected;
}

bool Client::Send(Transport::Frame&& frame, StormByte::Safe::Shared<Logger::Log> logger) noexcept {
	if (!m_socket)
		return false;
	ExpectedVoid result = m_socket->Send(frame.ProcessOutput(m_out_pipeline, logger));
	if (!result) {
		logger << Logger::Level::Error << "Failed to send frame to socket: " << result.error()->what();
		return false;
	}

	return true;
}

StormByte::Network::Transport::Frame Client::Receive(StormByte::Safe::Shared<Logger::Log> logger) noexcept {
	return Transport::Frame::ProcessInput(m_socket, m_in_pipeline, logger);
}
