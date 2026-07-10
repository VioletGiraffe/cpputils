#pragma once

#include "cconsumerblockingqueue.h"
#include "system/timing.h"

#include "compiler/compiler_warnings_control.h"
#include "math/math.hpp"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/function2/function2.hpp"
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

using TaskType = fu2::function_base < true, false, fu2::capacity_fixed<16 + sizeof(std::promise<void>)>, true, false, void() > ;

// A task bundled with an owner tag so that all of one owner's queued tasks can be removed at once via retire()
// (e.g. when the object that posted them is being destroyed). Tag 0 is the "untagged" sentinel and is never retired.
struct TaggedTask {
	uint64_t tag = 0;
	TaskType task;
};

// A pool of worker threads over per-thread task queues: enqueue() hash-picks a queue (no shared point of
// contention between busy workers), and a worker that finds its own queue empty steals from the others,
// so one busy queue cannot hold tasks hostage while other workers idle.
// Idle workers all park on ONE shared condvar: since any of them can take any task, a producer wakes a
// stealer with a single unrouted notify - no tracking of who sleeps where.
class CWorkerThreadPool
{
	class CWorkerThread
	{
	public:
		CWorkerThread(CWorkerThreadPool& pool, size_t queueIndex, std::string threadName);
		~CWorkerThread();

		CWorkerThread(const CWorkerThread&) = delete;
		CWorkerThread& operator=(const CWorkerThread&) = delete;

		[[nodiscard]] bool isStarted() const noexcept;

		void stop(bool finishPendingTasks = false);

	private:
		void threadFunc() noexcept;
		// Pops a task: own queue first, then steals from the others. Must be called under a shared lock on
		// _poolMutex so that retire()'s exclusive lock waits out whatever task is popped here.
		bool tryGetTask(TaggedTask& task);

	private:
		CWorkerThreadPool& _pool;
		const size_t _queueIndex; // this worker's own queue in _pool._queues
		const std::string _threadName;
		std::atomic<bool> _working {false};
		std::atomic<bool> _terminate {false};
		std::atomic<bool> _finishPendingTasks {false};
		// Must be the last member: its initialization starts the thread, which reads the members above -
		// they have to be initialized by then
		std::thread _thread;
	};

public:
	CWorkerThreadPool(size_t maxNumThreads, std::string poolName);
	~CWorkerThreadPool() noexcept = default;

	CWorkerThreadPool(const CWorkerThreadPool&) = delete;
	CWorkerThreadPool& operator=(const CWorkerThreadPool&) = delete;

	void finishAllThreads(bool completePendingTasks = false); // Does the same thing the destructor does, but can be called when needed

	// Removes all of this tag's not-yet-started tasks and waits out any in-flight task, so that once it returns no task
	// with this tag is running and none remains queued. Call from the destructor of the object that owns the tag. Tag must be non-zero.
	void retire(uint64_t tag);

	// Returns the resulting length of the queue the task was pushed to
	template <typename F>
	size_t enqueue(F&& task, uint64_t tag = 0)
	{
		// Fibonacci hashing of the timestamp: the multiply avalanches the low-bit deltas between consecutive
		// rdtsc reads into the high bits that Math::reduce() keys on. The raw timestamp's high bits change only
		// a few times per second, which used to land every push of a burst on the SAME lane. The mixed value is
		// better than random for bursts: multiplicative hashing spreads near-constant-delta inputs quasi-evenly
		// over the lanes (equidistribution), approximating a round robin with no shared state.
		const uint64_t mixed = rdtsc_fast_thread_local() * 0x9E3779B97F4A7C15ull;
		const size_t index = Math::reduce(static_cast<uint32_t>(mixed >> 32), (uint32_t)_maxNumThreads);
		// Incremented BEFORE the push: a task stolen (and decremented) before its own increment would underflow
		// the count. The transient overcount only costs a parked worker one wasted wakeup+rescan.
		++_queuedCount;
		const size_t queueLength = _queues[index].push(TaggedTask{ tag, std::forward<F>(task) });
		// Wake one idle worker, if any: an idle worker can take ANY task (own lane or stolen), so a single
		// notify_one on the shared condvar always suffices and cannot be misrouted. The lock is not superfluous:
		// it forbids the notify from landing in the gap between a waiter's predicate check and its blocking
		// (_queuedCount is modified outside _idleMutex). No idle workers - no lock taken, so the hot path of a
		// saturated pool stays contention-free.
		if (_idleCount.load() > 0)
		{
			std::lock_guard lock(_idleMutex);
			_idleCv.notify_one();
		}
		return queueLength;
	}

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
	// completed. Indices are handed out one at a time from a shared dispenser, so uneven per-index costs
	// load-balance naturally. The calling thread participates on equal footing and can complete the entire
	// range alone; a helper task popped after the indices have run out simply returns. That makes the call
	// safe (deadlock-free) even from inside a pool task on a saturated pool. The caller does not process
	// unrelated pool tasks while waiting for the last in-flight fn to return.
	// fn is invoked concurrently and must not throw: the workers' exception containment would swallow the
	// throw and the completion count would never be reached, leaving the caller waiting forever.
	template <typename Fn>
	void parallelFor(const size_t count, Fn&& fn)
	{
		if (count == 0)
			return;
		if (count == 1)
		{
			fn(size_t{ 0 });
			return;
		}

		struct BatchState
		{
			BatchState(const size_t n, Fn&& f) : count(n), fn(std::forward<Fn>(f)) {}

			// Executes indices from the shared dispenser until they run out; the last completed index wakes the caller.
			void drainIndices()
			{
				for (;;)
				{
					const size_t index = nextIndex.fetch_add(1);
					if (index >= count)
						return;
					fn(index);
					if (completedCount.fetch_add(1) + 1 == count)
					{
						// The lock is required: it forbids the notify from landing in the gap between the
						// caller's predicate check and its blocking (completedCount is modified outside the mutex)
						std::lock_guard lock(mutex);
						allDone.notify_one();
					}
				}
			}

			std::atomic<size_t> nextIndex{ 0 };
			std::atomic<size_t> completedCount{ 0 };
			std::mutex mutex;
			std::condition_variable allDone;
			const size_t count;
			std::decay_t<Fn> fn;
		};

		// Shared ownership rather than the caller's stack: a helper popped after the batch has already
		// completed still touches the dispenser (finds it exhausted and returns) - the caller may be long gone
		// by then. Late helpers never invoke fn, so whatever fn references only has to outlive the call itself.
		auto state = std::make_shared<BatchState>(count, std::forward<Fn>(fn));
		const size_t helperCount = std::min(count - 1, _maxNumThreads);
		for (size_t i = 0; i < helperCount; ++i)
			enqueue([state] { state->drainIndices(); });
		state->drainIndices();
		std::unique_lock lock(state->mutex);
		state->allDone.wait(lock, [&state] { return state->completedCount.load() == state->count; });
	}

	// Blocks until all the worker threads are started
	void waitUntilStarted() noexcept;

	[[nodiscard]] size_t maxWorkersCount() const;
	// The number of queued (pushed, not yet popped) tasks. A task being executed is no longer counted, so 0 does
	// NOT mean all work has finished - up to maxWorkersCount() tasks may still be running.
	[[nodiscard]] size_t queueLength() const;

private:
	const std::string _poolName;
	const size_t _maxNumThreads;
	// Total items currently queued (pushed, not yet popped) across all the queues. Incremented before the push,
	// decremented after every removal (pop/steal, shutdown drain, retire) - it must never underflow and must
	// reach exactly 0 when all queues are empty (asserted after a draining shutdown). Nonzero is what idle
	// workers' wait predicate checks, so queued work keeps a would-be sleeper awake to steal.
	std::atomic<size_t> _queuedCount{ 0 };
	// The number of workers currently inside the idle wait; each transition is written solely by the waiting
	// worker itself, so nothing can consume a worker's idle state and leave it desynced. Gates the producer-side
	// notify. The (default, seq_cst) ordering makes the gate lossless: a parking worker increments this BEFORE
	// its wait predicate reads _queuedCount, and a producer increments _queuedCount BEFORE reading this - so
	// either the worker sees the new task and declines to block, or the producer sees the idler and notifies.
	// Never neither. A stale nonzero read (the worker just left the wait) costs one notify into an empty condvar
	// at worst: an awake worker always completes a full tryGetTask before re-parking, so no task is overlooked.
	std::atomic<size_t> _idleCount{ 0 };
	std::mutex _idleMutex;
	std::condition_variable _idleCv;
	// Worker pop+execute takes it shared; retire() takes it exclusive (see the .cpp for why).
	std::shared_mutex _poolMutex;
	std::deque<CConsumerBlockingQueue<TaggedTask>> _queues;
	// The workers access every pool member above, so this must be declared last: its destruction joins the threads.
	std::deque<CWorkerThread> _workerThreads; // Cannot be std::vector because CWorkerThread cannot be made movable (let alone copyable)
};
