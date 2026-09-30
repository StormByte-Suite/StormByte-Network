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

#include <StormByte/network/worker_pool.hxx>

#include <algorithm>

namespace StormByte::Network::Detail {
	namespace {
		thread_local WorkerPool* current_pool = nullptr;
	}

	WorkerPool::WorkerPool(std::size_t worker_count, std::size_t queue_capacity,
		HandlerCallback handler, CompletionCallback on_completion):
	m_queue_capacity(queue_capacity), m_on_completion(std::move(on_completion)), m_handler(std::move(handler)) {
		if (worker_count == 0) {
			worker_count = 4;
		}

		m_workers.reserve(worker_count);
		for (std::size_t index = 0; index < worker_count; ++index) {
			m_workers.emplace_back(&WorkerPool::Run, this);
		}
	}

	WorkerPool::~WorkerPool() noexcept {
		Stop();
		Join();
	}

	void WorkerPool::Stop() noexcept {
		{
			std::scoped_lock lock(m_mutex);
			m_stopping = true;
		}

		m_condition.notify_all();
	}

	void WorkerPool::Join() noexcept {
		for (auto& worker: m_workers) {
			if (worker.joinable() && worker.get_id() != std::this_thread::get_id()) {
				worker.join();
			}
		}
	}

	bool WorkerPool::Submit(Task task) noexcept {
		std::scoped_lock lock(m_mutex);
		if (m_stopping || m_tasks.size() >= m_queue_capacity) {
			return false;
		}

		m_tasks.push_back(std::move(task));
		m_condition.notify_one();
		return true;
	}

	bool WorkerPool::HasCapacity() const noexcept {
		std::scoped_lock lock(m_mutex);
		return !m_stopping && m_tasks.size() < m_queue_capacity;
	}

	bool WorkerPool::IsWorkerThread() const noexcept {
		return current_pool == this;
	}

	void WorkerPool::Run() noexcept {
		current_pool = this;
		while (true) {
			Task task;
			{
				std::unique_lock lock(m_mutex);
				m_condition.wait(lock, [this]() { return m_stopping || !m_tasks.empty(); });
				if (m_tasks.empty()) {
					if (m_stopping) {
						break;
					}

					continue;
				}

				task = std::move(m_tasks.front());
				m_tasks.pop_front();
			}

			if (task.operation) {
				try {
					task.operation();
				} catch (...) {
				}
				continue;
			}

			Completion completion{ task.uuid, nullptr, CompletionReason::Error };
			try {
				completion.packet = m_handler(task.uuid, std::move(task.packet));
				completion.reason = completion.packet ? CompletionReason::Success : CompletionReason::NullHandler;
			} catch (...) {
				completion.reason = CompletionReason::Error;
			}

			m_on_completion(Completion{ std::move(task.uuid), std::move(completion.packet), completion.reason });
		}

		current_pool = nullptr;
	}
}
