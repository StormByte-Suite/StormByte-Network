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

#include <StormByte/network/typedefs.hxx>
#include <StormByte/network/visibility.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <string_view>

namespace StormByte::Network::Detail {
	/**
	 * @class WorkerPool
	 * @brief Bounded private executor for packet handlers.
	 */
	class STORMBYTE_NETWORK_PRIVATE WorkerPool final {
		public:
			enum class CompletionReason: unsigned short { Success, NullHandler, Error }; ///< Handler outcome.

			struct Task {
				std::string uuid; ///< Client UUID.
				PacketPointer packet; ///< Request packet.
				std::function<void()> operation; ///< Optional bounded internal operation, such as remote-file I/O.
			};

			struct Completion {
				std::string uuid; ///< Client UUID.
				PacketPointer packet; ///< Response packet, or null.
				CompletionReason reason; ///< Handler outcome.
			};

			using CompletionCallback = std::function<void(Completion)>; ///< Completion sink.
			using HandlerCallback = std::function<PacketPointer(std::string_view, PacketPointer)>; ///< Packet handler.

			/**
			 * @brief Starts a bounded worker pool.
			 * @param worker_count Number of workers.
			 * @param queue_capacity Maximum queued tasks.
			 * @param on_completion Completion sink.
			 */
			WorkerPool(std::size_t worker_count, std::size_t queue_capacity,
				HandlerCallback handler, CompletionCallback on_completion);

			/** @brief Stops accepting tasks. */
			void Stop() noexcept;

			/** @brief Joins all workers. */
			void Join() noexcept;

			/**
			 * @brief Attempts to enqueue one task without blocking.
			 * @param task Task to execute.
			 * @return true when queued.
			 */
			bool Submit(Task task) noexcept;

			/** @brief Whether another task can be accepted without blocking. */
			bool HasCapacity() const noexcept;

			/**
			 * @brief Whether the calling thread belongs to this pool.
			 * @return true for a pool worker.
			 */
			bool IsWorkerThread() const noexcept;

			/** @brief Destructor stops and joins workers. */
			~WorkerPool() noexcept;

		private:
			void Run() noexcept;

			const std::size_t m_queue_capacity; ///< Maximum queued tasks.
			CompletionCallback m_on_completion; ///< Completion sink.
			HandlerCallback m_handler; ///< Packet handler.
			mutable std::mutex m_mutex; ///< Task queue lock.
			std::condition_variable m_condition; ///< Worker wakeup.
			std::deque<Task> m_tasks; ///< Bounded task queue.
			std::vector<std::thread> m_workers; ///< Worker threads.
			bool m_stopping = false; ///< Stop flag.
	};
}
