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
#include <StormByte/system/this_thread.hxx>

#include <algorithm>
#include <mutex>
#include <utility>

/**
 * @brief One provider-owned native lookup; release/acquire publishes immutable results.
 */
struct StormByte::Network::Connection::ResolutionJob final {
	Safe::String hostname;	///< Copied request name.
	Info::Addresses addresses;	///< Provider-owned result snapshot.
	Safe::Shared<Exception> error;	///< Retained provider error, if available.
	Safe::Atomic<bool> done{false};	///< Published only after result ownership is settled.
	Safe::Atomic<bool> abandoned{false};	///< Caller no longer needs the native result.
	bool failed = true;	///< Published failure indicator.

	/**
	 * @brief Copy borrowed request storage in Network.
	 * @param host Borrowed request name.
	 */
	explicit ResolutionJob(std::string_view host): hostname(host) {}

	/**
	 * @brief Release provider-owned results in Network.
	 */
	~ResolutionJob() noexcept = default;

	/**
	 * @brief Work shared with a worker cannot be copied.
	 * @param other Source job.
	 */
	ResolutionJob(const ResolutionJob& other) = delete;

	/**
	 * @brief Work shared with a worker cannot be moved.
	 * @param other Source job.
	 */
	ResolutionJob(ResolutionJob&& other) = delete;

	/**
	 * @brief Shared work assignment is disabled.
	 * @param other Source job.
	 * @return No value; operation is deleted.
	 */
	ResolutionJob& operator=(const ResolutionJob& other) = delete;

	/**
	 * @brief Shared work move assignment is disabled.
	 * @param other Source job.
	 * @return No value; operation is deleted.
	 */
	ResolutionJob& operator=(ResolutionJob&& other) = delete;
};

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Connection::ResolutionJob);

using namespace StormByte;
using namespace StormByte::Network;
using namespace StormByte::Network::Connection;

Resolver::Resolver(Lookup lookup): m_lookup(lookup) {}

Resolver::~Resolver() noexcept {
	if (m_worker.joinable())
		m_worker.join();
}

StormByte::Expected<Info::Addresses, StormByte::Network::Exception> Resolver::Resolve(std::string_view hostname,
	unsigned short port, AddressPolicy policy, const Socket::OperationControl& control,
	Socket::OperationState& state) noexcept {
	state = control.Check();
	if (state != Socket::OperationState::Complete)
		return Unexpected<StormByte::Network::Exception>("Resolution stopped before admission");
	Safe::Shared<ResolutionJob> job;
	try {
		{
			std::unique_lock<Safe::Mutex> lock(m_gate, std::try_to_lock);
			if (!lock.owns_lock()) {
				state = Socket::OperationState::Busy;
				return Unexpected<StormByte::Network::Exception>("Resolver admission is busy");
			}
			if (m_job && !m_job->done.load(Safe::MemoryOrder::Acquire)) {
				state = Socket::OperationState::Busy;
				return Unexpected<StormByte::Network::Exception>("Resolver slot is busy");
			}
			if (m_worker.joinable())
				m_worker.join();
			state = control.Check();
			if (state != Socket::OperationState::Complete)
				return Unexpected<StormByte::Network::Exception>("Resolution stopped before launch");
			if (!m_lookup || hostname.find('\0') != std::string_view::npos) {
				state = Socket::OperationState::Error;
				return Unexpected<StormByte::Network::Exception>("Invalid resolution request");
			}
			job = Safe::MakeShared<ResolutionJob>(hostname);
			const auto lookup = m_lookup;
			m_worker = Safe::Thread([job, lookup, port, policy]() noexcept {
				try {
					auto result = lookup(std::string_view{job->hostname}, port, policy);
					if (!job->abandoned.load(Safe::MemoryOrder::Acquire)) {
						if (result) {
							job->addresses = std::move(result.value());
							job->failed = false;
						}
						else
							job->error = result.error();
					}
				} catch (...) {}
				job->done.store(true, Safe::MemoryOrder::Release);
			});
			m_job = job;
		}
		for (;;) {
			state = control.Check();
			if (state != Socket::OperationState::Complete) {
				job->abandoned.store(true, Safe::MemoryOrder::Release);
				return Unexpected<StormByte::Network::Exception>("Resolution caller stopped; native lookup remains managed");
			}
			if (job->done.load(Safe::MemoryOrder::Acquire)) {
				if (job->failed) {
					state = Socket::OperationState::Error;
					if (job->error)
						return Unexpected(job->error);
					return Unexpected<StormByte::Network::Exception>("Resolution failed");
				}
				return job->addresses;
			}
			auto wait = std::chrono::milliseconds(10);
			if (control.deadline != std::chrono::steady_clock::time_point::max())
				wait = std::min(wait, std::chrono::ceil<std::chrono::milliseconds>(
					control.deadline - std::chrono::steady_clock::now()));
			System::ThisThread::Sleep(std::max(wait, std::chrono::milliseconds(0)));
		}
	} catch (...) {
		state = Socket::OperationState::Error;
		return Unexpected<StormByte::Network::Exception>("Failed to allocate or start resolution work");
	}
}
