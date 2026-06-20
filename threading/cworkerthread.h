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

using TaskType = fu2::function_base < true, false, fu2::capacity_fixed<16 + sizeof(std::promise<void>)>, true, false, void() > ;

// A task bundled with an owner tag so that all of one owner's queued tasks can be removed at once via retire()
// (e.g. when the object that posted them is being destroyed). Tag 0 is the "untagged" sentinel and is never retired.
struct TaggedTask {
	uint64_t tag = 0;
	TaskType task;
};

class CWorkerThreadPool
{
	class CWorkerThread
	{
	public:
		CWorkerThread(CConsumerBlockingQueue<TaggedTask>& queue, std::shared_mutex& poolMutex, std::string threadName);
		~CWorkerThread();

		CWorkerThread(const CWorkerThread&) = delete;
		CWorkerThread& operator=(const CWorkerThread&) = delete;

		[[nodiscard]] bool isStarted() const noexcept;

		void stop(bool finishPendingTasks = false);

	private:
		void threadFunc() noexcept;

	private:
		CConsumerBlockingQueue<TaggedTask>& _queue;
		std::shared_mutex& _poolMutex;
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

	// Returns the current queue length
	template <typename F>
	size_t enqueue(F&& task, uint64_t tag = 0)
	{
		const uint64_t timestamp = rdtsc_fast_thread_local();
		const size_t index = Math::reduce(static_cast<uint32_t>(timestamp ^ (timestamp >> 32)), (uint32_t)_maxNumThreads);
		return _queues[index].push(TaggedTask{ tag, std::forward<F>(task) });
	}

	template <typename F>
	[[nodiscard]] std::future<void> enqueueWithFuture(F&& task, uint64_t tag = 0)
	{
		const uint64_t timestamp = rdtsc_fast_thread_local();
		const size_t index = Math::reduce(static_cast<uint32_t>(timestamp ^ (timestamp >> 32)), (uint32_t)_maxNumThreads);

		std::promise<void> p;
		auto future = p.get_future();
		_queues[index].push(TaggedTask{ tag, [task{ std::forward<F>(task) }, p{ std::move(p) }] () mutable {
			task();
			p.set_value();
		} });

		return future;
	}

	// Blocks until all the worker threads are started
	void waitUntilStarted() noexcept;

	[[nodiscard]] size_t maxWorkersCount() const;
	[[nodiscard]] size_t queueLength() const;

private:
	// Declared before the queues and threads so it outlives the worker threads that lock it.
	// Worker pop+execute takes it shared; retire() takes it exclusive (see the .cpp for why).
	std::shared_mutex _poolMutex;
	// It is important that the queue is declared before threads (means it will only be destroyed after all the threads using it stop)
	std::deque<CConsumerBlockingQueue<TaggedTask>> _queues;
	std::deque<CWorkerThread> _workerThreads; // Cannot be std::vector because CWorkerThread cannot be made movable (let alone copyable)
	const std::string _poolName;
	const size_t _maxNumThreads;
};
