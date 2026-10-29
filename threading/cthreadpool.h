#pragma once

#include "cconsumerblockingqueue.h"

#include "assert/advanced_assert.h"
#include "compiler/compiler_warnings_control.h"
#include "math/math.hpp"
#include "utility/aligned_wrapper.hpp"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/function2/function2.hpp"
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

STORE_COMPILER_WARNINGS
DISABLE_MSVC_WARNING(4324) // structure was padded due to alignment specifier

using TaskType = fu2::function_base<true, false, fu2::capacity_fixed<16 + sizeof(std::promise<void>) + 64 /*arbitrary reserve for captures */>, true, false, void()>;

struct TaskTagState {
	size_t outstandingTaskCount = 0; // Queued + popped/running; registered before a task becomes visible to workers
	bool retired = false;
};

// A task bundled with owner-tag state so retire() can remove queued tasks and wait only for this owner's
// popped/running tasks. A null state is the untagged sentinel and has no tracking overhead.
struct TaggedTask {
	TaskTagState* tagState = nullptr;
	TaskType task;
};

// A pool of worker threads over per-thread task queues: enqueue() picks a queue round-robin (no shared point of
// contention between busy workers), and a worker that finds its own queue empty steals from the others,
// so one busy queue cannot hold tasks hostage while other workers idle.
// Idle workers all park on ONE shared condvar: since any of them can take any task, a producer wakes a
// stealer with a single unrouted notify - no tracking of who sleeps where.
class CThreadPool
{
public:
	enum class ShutdownPolicy {
		WaitForRunningTasks, // The destructor calls finishAllThreads(false)
		AbandonRunningTasks  // The destructor calls abandonAllThreads()
	};

	CThreadPool(uint32_t maxNumThreads, std::string poolName, ShutdownPolicy shutdownPolicy = ShutdownPolicy::WaitForRunningTasks);
	~CThreadPool() noexcept;

	CThreadPool(const CThreadPool&) = delete;
	CThreadPool& operator=(const CThreadPool&) = delete;

	// Stops and joins all workers. completePendingTasks == true first drains every queued task to completion,
	// including ones spawned during the drain; false abandons the backlog.
	void finishAllThreads(bool completePendingTasks = false);

	// Stops all workers without waiting for running tasks: queued tasks are destroyed unrun on the calling thread,
	// and a worker that is mid-task exits once its task returns, possibly after the pool is destroyed.
	// Only for tasks that touch nothing but state they co-own: such a task may outlive everything else.
	void abandonAllThreads();

	// Removes this tag's queued tasks and waits out any popped/running task, so that once it returns no task with this tag
	// is running or queued. The owner must stop submitting before calling this from its destructor.
	// Tag must be non-zero.
	void retire(uint64_t tag);

	// Returns the resulting length of the queue the task was pushed to
	template <typename F>
	size_t enqueue(F&& task, uint64_t tag = 0)
	{
		State& state = *_state;
		const uint32_t index = state.laneSelectorMod.mod(state.laneSelector.value.fetch_add(1, std::memory_order_relaxed));
		TaggedTask taggedTask{ nullptr, std::forward<F>(task) };
		TaskTagState* tagState = nullptr;
		if (tag != 0)
		{
			std::lock_guard lock(state.tagStateMutex);
			tagState = &state.tagStates.try_emplace(tag).first->second;

			assert_debug_only(!tagState->retired); // Enqueuing after the owner has begun retiring is a lifetime bug
			if (tagState->retired)
				return 0;

			++tagState->outstandingTaskCount;
			taggedTask.tagState = tagState;
		}

		// Incremented BEFORE the push: a task stolen (and decremented) before its own increment would underflow
		// the count. The transient overcount only costs a parked worker one wasted wakeup+rescan.
		++state.queuedCount.value;
		size_t queueLength;
		try
		{
			queueLength = state.queues[index].push(std::move(taggedTask));
		}
		catch (...)
		{
			--state.queuedCount.value;
			state.completeTaggedTask(tagState);
			throw;
		}
		// Wake one idle worker, if any: an idle worker can take ANY task (own lane or stolen), so a single
		// notify_one on the shared condvar always suffices and cannot be misrouted. The lock is not superfluous:
		// it forbids the notify from landing in the gap between a waiter's predicate check and its blocking
		// (queuedCount is modified outside idleMutex). No idle workers - no lock taken, so the hot path of a
		// saturated pool stays contention-free.
		if (state.idleCount.value.load() > 0)
		{
			std::lock_guard lock(state.idleMutex);
			state.idleCv.notify_one();
		}
		return queueLength;
	}

	// A throwing task leaves the future with std::future_error (broken_promise), not the task's exception
	template <typename F>
	[[nodiscard]] std::future<void> enqueueWithFuture(F&& task, uint64_t tag = 0)
	{
		std::promise<void> p;
		auto future = p.get_future();
		enqueue([task{ std::forward<F>(task) }, p{ std::move(p) }] () mutable {
			task();
			p.set_value();
		}, tag);
		return future;
	}

	// Runs fn(0) .. fn(count - 1) across the workers and the calling thread, returning once every call has
	// completed. The calling thread participates on equal footing and can complete the entire range alone,
	// which makes the call safe (deadlock-free) even from inside a pool task on a saturated pool. The caller
	// does not process unrelated pool tasks while waiting for the last in-flight fn to return.
	// fn is invoked concurrently. A throw from fn is contained and logged so it cannot strand the batch (the
	// completion count still advances), but that index's work is then left incomplete - fn should handle its own errors.
	template <typename Fn>
	void parallelFor(size_t count, Fn&& fn);

	// The non-blocking parallelFor: returns as soon as the batch is dispatched; onAllCompleted then runs
	// exactly once, on the worker that completes the last index - never on the calling thread (count == 0
	// also dispatches to a worker). If the pool is destroyed before any worker picks the batch up, neither
	// fn nor onAllCompleted runs. fn's exceptions are contained as in parallelFor; onAllCompleted must not throw.
	template <typename Fn, typename OnAllCompleted>
	void parallelForAsync(size_t count, Fn&& fn, OnAllCompleted&& onAllCompleted);

	// Blocks until all the worker threads are started
	void waitUntilStarted() noexcept;

	[[nodiscard]] size_t maxWorkersCount() const;
	// The number of queued (pushed, not yet popped) tasks. A task being executed is no longer counted, so 0 does
	// NOT mean all work has finished - up to maxWorkersCount() tasks may still be running.
	[[nodiscard]] size_t queueLength() const;

private:
	// Everything a worker touches. Co-owned by the pool and every worker thread, so a worker abandoned mid-task keeps it alive until it exits.
	struct State
	{
		explicit State(uint32_t numThreads);

		void runWorker(size_t ownQueueIndex, const std::string& threadName) noexcept;
		// Pops a task: own queue first, then steals from the others.
		[[nodiscard]] bool tryGetTask(size_t ownQueueIndex, TaggedTask& task);
		[[nodiscard]] bool taggedTaskCanRun(const TaskTagState* tagState);
		void completeTaggedTask(TaskTagState* tagState);
		// Executes one popped task with exception containment (a throw must not escape and unwind the caller), then
		// accounts it. Used by the shutdown drain; the hot worker loop inlines its own copy (see runWorker).
		void executeContainedTask(TaggedTask& item);

		// Members are laid out by access pattern: read-only-after-ctor members first (can share cache lines freely),
		// then the idle-wait group (only written when workers park/unpark, quiet while the pool is saturated),
		// then the write-hot shared words - CacheLinePadded to a line each, so their RMW ping-pong does not
		// invalidate the read-mostly lines or each other. idleCount is padded too: it must not spill onto the
		// read-only line above (benchmarked: park/unpark storms invalidating the queues header line cost up to
		// 2x in starved nanotask regimes), and its own line keeps enqueue()'s gate read clean of idleMutex traffic.
		const uint32_t maxNumThreads;
		Math::FastMod32 laneSelectorMod;
		std::deque<CConsumerBlockingQueue<TaggedTask>> queues; // Header written only in the ctor; the queues have their own internal locks
		std::atomic<bool> terminate{ false }; // Written once, at shutdown
		std::atomic<uint32_t> startedWorkerCount{ 0 }; // Written only while the workers start

		// The number of workers currently inside the idle wait; each transition is written solely by the waiting
		// worker itself, so nothing can consume a worker's idle state and leave it desynced. Gates the producer-side
		// notify. The (default, seq_cst) ordering makes the gate lossless: a parking worker increments this BEFORE
		// its wait predicate reads queuedCount, and a producer increments queuedCount BEFORE reading this - so
		// either the worker sees the new task and declines to block, or the producer sees the idler and notifies.
		// Never neither. A stale nonzero read (the worker just left the wait) costs one notify into an empty condvar
		// at worst: an awake worker always completes a full tryGetTask before re-parking, so no task is overlooked.
		CacheLinePadded<std::atomic<size_t>> idleCount{ 0 };
		std::mutex idleMutex;
		std::condition_variable idleCv;

		CacheLinePadded<std::atomic<uint32_t>> laneSelector{ 0 }; // RMW'd on every enqueue, by producers only
		// Total items currently queued (pushed, not yet popped) across all the queues. Incremented before the push,
		// decremented after every removal (pop/steal, shutdown drain, retire) - it must never underflow and must
		// reach exactly 0 when all queues are empty (asserted after a draining shutdown). Nonzero is what idle
		// workers' wait predicate checks, so queued work keeps a would-be sleeper awake to steal.
		// The most contended word in the pool: RMW'd on every enqueue AND every pop/steal.
		CacheLinePadded<std::atomic<size_t>> queuedCount{ 0 };

		// Tagged tasks register here before they become visible in a queue. Workers touch this state only at task
		// boundaries; task execution never holds the mutex, so retiring one tag cannot wait on unrelated work.
		std::mutex tagStateMutex;
		std::condition_variable tagStateChanged;
		// Queued tasks hold a raw TaskTagState* into this map, so it must be node-based: an element's address has to survive the insertion of other tags.
		std::map<uint64_t, TaskTagState> tagStates;
	};

	// Stops the workers and wakes the parked ones; the caller then joins or detaches them.
	void requestWorkersStop();

	const ShutdownPolicy _shutdownPolicy;
	const std::shared_ptr<State> _state;
	std::vector<std::thread> _workerThreads;
};

RESTORE_COMPILER_WARNINGS

namespace thread_pool_detail {

// The shared work-dispensing state of one parallelFor / parallelForAsync batch: indices are handed out one
// at a time from the shared dispenser, so uneven per-index costs load-balance naturally. Owned via shared_ptr
// by each helper task rather than the initiator's stack: a helper popped after the batch has already completed
// still touches the dispenser (finds it exhausted and returns) - the initiator may be long gone by then. Late
// helpers never invoke fn, so whatever fn references only has to stay alive until onAllCompleted has run.
template <typename Fn, typename OnAllCompleted>
struct ParallelBatchState
{
	ParallelBatchState(const size_t n, Fn&& f, OnAllCompleted&& onDone) :
		count(n), fn(std::forward<Fn>(f)), onAllCompleted(std::forward<OnAllCompleted>(onDone))
	{}

	// Executes indices from the shared dispenser until they run out; the last completed index runs onAllCompleted.
	// fn is contained: a throw must not abort the drain and leave completedCount short of count, which would strand
	// onAllCompleted (and any parallelFor caller blocked on it). onAllCompleted itself is expected not to throw.
	void drainIndices()
	{
		for (;;)
		{
			const size_t index = nextIndex.fetch_add(1);
			if (index >= count)
				return;
			try
			{
				fn(index);
			}
			catch (const std::exception& e)
			{
				log_unconditional_r(std::string{ "Exception in a parallelFor task: " } + e.what());
			}
			catch (...)
			{
				log_unconditional_r("Unknown exception in a parallelFor task");
			}
			if (completedCount.fetch_add(1) + 1 == count)
				onAllCompleted();
		}
	}

	std::atomic<size_t> nextIndex{ 0 };
	std::atomic<size_t> completedCount{ 0 };
	const size_t count;
	std::decay_t<Fn> fn;
	std::decay_t<OnAllCompleted> onAllCompleted;
};

template <typename Fn, typename OnAllCompleted>
[[nodiscard]] auto makeParallelBatchState(const size_t count, Fn&& fn, OnAllCompleted&& onAllCompleted)
{
	return std::make_shared<ParallelBatchState<Fn, OnAllCompleted>>(count, std::forward<Fn>(fn), std::forward<OnAllCompleted>(onAllCompleted));
}

} // namespace thread_pool_detail

template <typename Fn>
void CThreadPool::parallelFor(const size_t count, Fn&& fn)
{
	if (count == 0)
		return;
	if (count == 1)
	{
		fn(size_t{ 0 });
		return;
	}

	// The wait block is co-owned by the completion action rather than living on the caller's stack: the caller
	// can wake on the predicate check alone (e.g. spuriously) right after 'done' is set and return, destroying
	// a stack-owned mutex+condvar under the last worker's in-progress notify. Flipping 'done' under the mutex
	// also keeps the notify from landing in the gap between the caller's predicate check and its blocking.
	struct WaitSync
	{
		std::mutex mutex;
		std::condition_variable allDone;
		bool done = false;
	};
	auto sync = std::make_shared<WaitSync>();
	auto state = thread_pool_detail::makeParallelBatchState(count, std::forward<Fn>(fn), [sync] {
		std::lock_guard lock(sync->mutex);
		sync->done = true;
		sync->allDone.notify_one();
	});

	const uint32_t helperCount = (uint32_t)std::min<size_t>(count - 1, _state->maxNumThreads);
	for (uint32_t i = 0; i < helperCount; ++i)
		enqueue([state] { state->drainIndices(); });
	state->drainIndices();

	std::unique_lock lock(sync->mutex);
	sync->allDone.wait(lock, [&sync] { return sync->done; });
}

template <typename Fn, typename OnAllCompleted>
void CThreadPool::parallelForAsync(const size_t count, Fn&& fn, OnAllCompleted&& onAllCompleted)
{
	if (count == 0)
	{
		// No indices to dispense, but the contract stands: onAllCompleted still runs (once, on a worker)
		enqueue([onAllCompleted{ std::forward<OnAllCompleted>(onAllCompleted) }] () mutable { onAllCompleted(); });
		return;
	}

	auto state = thread_pool_detail::makeParallelBatchState(count, std::forward<Fn>(fn), std::forward<OnAllCompleted>(onAllCompleted));
	const uint32_t helperCount = (uint32_t)std::min<size_t>(count, _state->maxNumThreads);
	for (uint32_t i = 0; i < helperCount; ++i)
		enqueue([state] { state->drainIndices(); });
}
