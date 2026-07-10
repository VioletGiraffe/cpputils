#pragma once

#include "cconsumerblockingqueue.h"
#include "system/timing.h"

#include "compiler/compiler_warnings_control.h"
#include "math/math.hpp"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/function2/function2.hpp"
RESTORE_COMPILER_WARNINGS

#include <atomic>
#include <deque>
#include <future>
#include <shared_mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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
		std::thread _thread;
		std::atomic<bool> _working {false};
		std::atomic<bool> _terminate {false};
		std::atomic<bool> _finishPendingTasks {false};
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
		// push() has woken this queue's owner; when the owner is parked AND this task is alone in the queue, that
		// fresh wake is dedicated to exactly this task and no one else needs waking - the common case, making the
		// push as cheap as pre-stealing. A collision (queueLength > 1) means the task sits behind a busy owner and
		// must be STOLEN; the worker that will steal it is one whose own lane is empty, and with per-lane condvars
		// that worker is reachable only by a wake aimed at ITS lane - a single targeted wake can miss it entirely -
		// so wake ALL parked workers (rare under load: collisions need the producer to outrun a lane's owner).
		// When only the owner is momentarily busy (queueLength == 1, owner not parked), one wake suffices.
		if (queueLength > 1)
			wakeAllParkedWorkers();
		else if (!_parkedFlags[index].parked.load())
			wakeOneParkedWorker(index + 1);
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

	// Blocks until all the worker threads are started
	void waitUntilStarted() noexcept;

	[[nodiscard]] size_t maxWorkersCount() const;
	// The number of queued (pushed, not yet popped) tasks. A task being executed is no longer counted, so 0 does
	// NOT mean all work has finished - up to maxWorkersCount() tasks may still be running.
	[[nodiscard]] size_t queueLength() const;

private:
	// Wakes at most one parked worker, scanning from scanStart (wraps around); a no-op if no one is parked.
	void wakeOneParkedWorker(size_t scanStart);
	// Wakes every currently-parked worker; used on a collision, when a task may be stuck behind a busy owner.
	void wakeAllParkedWorkers();

private:
	const std::string _poolName;
	const size_t _maxNumThreads;
	// Total items currently queued (pushed, not yet popped) across all the queues. Incremented before the push,
	// decremented after every removal (pop/steal, shutdown drain, retire) - it must never underflow and must
	// reach exactly 0 when all queues are empty (asserted after a draining shutdown). Nonzero is what parked
	// workers' wait predicate checks, letting a worker parked on its own empty queue wake up and steal.
	std::atomic<size_t> _queuedCount{ 0 };
	// One flag per worker, set while it is parked in the idle wait; padded so different workers' frequent
	// park/unpark stores do not false-share a cache line. enqueue() reads them to decide whom to wake: nobody
	// when the pushed-to queue's owner is parked and the task is alone in that queue (the owner's fresh wake
	// from push() is dedicated to it), one parked peer otherwise. The (default, seq_cst) ordering makes this
	// lossless: a parking worker sets its flag BEFORE its wait predicate reads _queuedCount, and a producer
	// increments _queuedCount BEFORE reading the flags - so either the worker sees the new task and stays
	// awake, or the producer sees the parked flag and wakes it. Never neither.
	struct alignas(64) ParkedFlag { std::atomic<bool> parked{ false }; };
	std::vector<ParkedFlag> _parkedFlags;
	// Worker pop+execute takes it shared; retire() takes it exclusive (see the .cpp for why).
	std::shared_mutex _poolMutex;
	std::deque<CConsumerBlockingQueue<TaggedTask>> _queues;
	// The workers access every pool member above, so this must be declared last: its destruction joins the threads.
	std::deque<CWorkerThread> _workerThreads; // Cannot be std::vector because CWorkerThread cannot be made movable (let alone copyable)
};
