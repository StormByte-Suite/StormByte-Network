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

#include <StormByte/network/detail/server_state.hxx>
#include <StormByte/network/visibility.h>
#include <StormByte/safe/condition_variable.hxx>
#include <StormByte/safe/deque.hxx>
#include <StormByte/safe/function.hxx>
#include <StormByte/safe/heap.hxx>
#include <StormByte/safe/optional.hxx>
#include <StormByte/safe/string.hxx>
#include <StormByte/safe/thread.hxx>
#include <StormByte/safe/vector.hxx>
#include <StormByte/size.hxx>
#include <StormByte/type_traits.hxx>

#include <new>
#include <type_traits>
#include <utility>

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
		 * @brief Private implementation details of the Network module.
		 */
		namespace Detail {
			/**
			 * @brief Network-provider factory for explicitly owned Safe callbacks.
			 * @tparam Signature Typed callback signature.
			 */
			template<class Signature>
			struct WorkerCallbackFactory;

			/**
			 * @brief Construct callbacks with Network clone and release operations.
			 * @tparam Return Callback result type, including void.
			 * @tparam Args Callback argument types.
			 * @details Instantiate only inside Network. Base owns the allocation;
			 * Network constructs, clones and destroys the captured callable. Both
			 * providers must remain loaded until every callback is released.
			 */
			template<class Return, class... Args>
			struct WorkerCallbackFactory<Return(Args...)> final {
				/**
				 * @brief Capture a callable in provider-owned context.
				 * @tparam Callable Captured callable type.
				 * @param callable Callable copied or moved into the context.
				 * @return Callback carrying Network invocation and lifetime operations.
				 */
				template<class Callable>
				static Safe::Function<Return(Args...)> Make(Callable callable) {
					void* context = Allocate(std::move(callable));
					try {
						if constexpr (Type::SameAs<Return, void>) {
							return {context, [](void* raw, Args... args) {
								(*static_cast<Callable*>(raw))(std::forward<Args>(args)...);
								return Safe::Status::Success;
							}, &Clone<Callable>, &Release<Callable>};
						}
						else {
							return {context, [](void* raw, Return* output, Args... args) {
								*output = (*static_cast<Callable*>(raw))(std::forward<Args>(args)...);
								return Safe::Status::Success;
							}, &Clone<Callable>, &Release<Callable>};
						}
					}
					catch (...) {
						Release<Callable>(context);
						throw;
					}
				}

				private:
					/**
					 * @brief Construct a callable in a Base-owned block.
					 * @tparam Callable Source callable type.
					 * @param callable Callable copied or moved into the block.
					 * @return Constructed context.
					 */
					template<class Callable>
					static void* Allocate(Callable&& callable) {
						using Context = std::remove_cvref_t<Callable>;
						void* raw = Safe::Heap::Allocate(sizeof(Context));
						try {
							return ::new (raw) Context(std::forward<Callable>(callable));
						}
						catch (...) {
							Safe::Heap::Free(raw);
							throw;
						}
					}

					/**
					 * @brief Clone context inside Network, reporting failure as null.
					 * @tparam Callable Concrete captured callable.
					 * @param raw Context to clone.
					 * @return Independent context, or null on failure.
					 */
					template<class Callable>
					static void* Clone(const void* raw) noexcept {
						try {
							return Allocate(*static_cast<const Callable*>(raw));
						}
						catch (...) {
							return nullptr;
						}
					}

					/**
					 * @brief Destroy context in Network and release its Base allocation.
					 * @tparam Callable Concrete captured callable.
					 * @param raw Context to release.
					 */
					template<class Callable>
					static void Release(void* raw) noexcept {
						static_cast<Callable*>(raw)->~Callable();
						Safe::Heap::Free(raw);
					}
			};

			/**
			 * @class WorkerPool
			 * @brief Bounded private executor for packet handlers.
			 * @note Stop drains accepted tasks. Join and destruction require an external
			 * owner; a worker may request Stop or Join but must not destroy its pool.
			 * Lifecycle calls must not race with Join or destruction.
			 */
			class STORMBYTE_NETWORK_PRIVATE WorkerPool final {
				public:
					/**
					 * @brief Result of a packet handler invocation.
					 */
					using CompletionReason = Detail::CompletionReason;

					/**
					 * @brief One owned request or internal operation.
					 */
					using Task = Detail::WorkerTask;

					/**
					 * @brief Owned packet handler result delivered to the sink.
					 */
					using Completion = Detail::Completion;

					/**
					 * @brief Provider-owned completion sink.
					 */
					using CompletionCallback = Safe::Function<void(Completion)>;

					/**
					 * @brief Packet handler with a synchronous borrowed UUID.
					 */
					using HandlerCallback = Safe::Function<PacketPointer(const Safe::String&, PacketPointer)>;

					/**
					 * @brief Start a bounded worker pool.
					 * @param worker_count Number of workers; zero selects four.
					 * @param queue_capacity Maximum queued tasks; zero accepts none.
					 * @param handler Provider-owned packet handler.
					 * @param on_completion Provider-owned sink, invoked concurrently by workers.
					 * @throws StormByte::Exception Allocation or thread startup failed.
					 */
					WorkerPool(Size worker_count, Size queue_capacity,
						HandlerCallback handler, CompletionCallback on_completion);

					/**
					 * @brief Copy construction is not supported.
					 * @param other Pool that cannot be copied.
					 */
					WorkerPool(const WorkerPool& other) = delete;

					/**
					 * @brief Movement is not supported because workers borrow this pool.
					 * @param other Pool that cannot be moved.
					 */
					WorkerPool(WorkerPool&& other) = delete;

					/**
					 * @brief Stop and join workers before releasing provider-owned state.
					 */
					~WorkerPool() noexcept;

					/**
					 * @brief Copy assignment is not supported.
					 * @param other Pool that cannot be copied.
					 * @return This pool; operation is deleted.
					 */
					WorkerPool& operator=(const WorkerPool& other) = delete;

					/**
					 * @brief Move assignment is not supported.
					 * @param other Pool that cannot be moved.
					 * @return This pool; operation is deleted.
					 */
					WorkerPool& operator=(WorkerPool&& other) = delete;

					/**
					 * @brief Reject new tasks and wake workers to drain accepted tasks.
					 */
					void Stop() noexcept;

					/**
					 * @brief Join all workers from an external owner; return on a worker.
					 * @note Call Stop first. Repeated calls after joining are harmless.
					 */
					void Join() noexcept;

					/**
					 * @brief Attempt to enqueue without waiting for queue capacity.
					 * @param task Owned task transferred into the queue on success.
					 * @return True when accepted; false on stop, capacity or allocation failure.
					 */
					bool Submit(Task task) noexcept;

					/**
					 * @brief Observe available queue capacity under its lock.
					 * @return True when a task could be accepted at this instant.
					 */
					bool HasCapacity() const noexcept;

					/**
					 * @brief Determine whether the caller belongs to this pool.
					 * @return True on this pool's worker executions.
					 */
					bool IsWorkerThread() const noexcept;

				private:
					/**
					 * @brief Drain tasks, isolating handler and completion exceptions.
					 */
					void Run() noexcept;

					const Size m_queue_capacity;				///< Maximum queued tasks.
					CompletionCallback m_on_completion;			///< Provider-owned completion sink.
					HandlerCallback m_handler;				///< Provider-owned packet handler.
					mutable Safe::Mutex m_mutex;				///< Task queue lock.
					Safe::ConditionVariable m_condition;			///< Worker wakeup signal.
					Safe::Deque<Task> m_tasks;				///< Bounded FIFO queue on Base's heap.
					Safe::Vector<WorkerThread> m_workers;			///< Exact registered execution records, joined in Network.
					bool m_stopping = false;					///< Stop flag protected by the queue lock.
			};
		}
	}
}

STORMBYTE_DECLARE_MAYBE_SAFE(StormByte::Network::Detail::WorkerPool);
