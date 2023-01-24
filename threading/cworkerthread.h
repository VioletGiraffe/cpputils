#pragma once

#include "cconsumerblockingqueue.h"
#include "system/rdtsc.h"

#include "3rdparty/function2/function2.hpp"

#include <atomic>
#include <deque>
#include <future>
#include <string>
#include <thread>
#include <utility>

using TaskType = fu2::function_base < true, false, fu2::capacity_fixed<16 + sizeof(std::promise<void>)>, true, false, void() > ;

class CWorkerThreadPool
{
	class CWorkerThread
	{
	public:
		CWorkerThread(CConsumerBlockingQueue<TaskType>& queue, std::string threadName);
		~CWorkerThread();

		CWorkerThread(const CWorkerThread&) = delete;
		CWorkerThread& operator=(const CWorkerThread&) = delete;

		[[nodiscard]] bool isStarted() const noexcept;

		void stop();

	private:
		void threadFunc() noexcept;

	private:
		CConsumerBlockingQueue<TaskType>& _queue;
		const std::string _threadName;
		std::thread _thread;
		std::atomic<bool> _working {false};
		std::atomic<bool> _terminate {false};
	};

	inline static constexpr uint64_t reduce(uint32_t value, uint32_t range) {
		return ((uint64_t)value * (uint64_t)range) >> 32;
	}
	
public:
	CWorkerThreadPool(size_t maxNumThreads, std::string poolName);
	void finishAllThreads(); // Does the same thing the destructor does, but can be called when needed

	CWorkerThreadPool(const CWorkerThreadPool&) = delete;
	CWorkerThreadPool& operator=(const CWorkerThreadPool&) = delete;

	// Returns the current queue length
	template <typename F>
	size_t enqueue(F&& task)
	{
		const uint64_t timestamp = rdtsc();
		const size_t index = reduce(static_cast<uint32_t>(timestamp ^ (timestamp >> 32)), (uint32_t)_maxNumThreads);
		return _queues[index].push(std::forward<F>(task));
	}

	template <typename F>
	[[nodiscard]] std::future<void> enqueueWithFuture(F&& task)
	{
		const uint64_t timestamp = rdtsc();
		const size_t index = reduce(static_cast<uint32_t>(timestamp ^ (timestamp >> 32)), (uint32_t)_maxNumThreads);

		std::promise<void> p;
		auto future = p.get_future();
		_queues[index].push([task{ std::move(task) }, p{ std::move(p) }] () mutable {
			task();
			p.set_value();
		});

		return future;
	}

	// Blocks until all the worker threads are started
	void waitUntilStarted() noexcept;

	[[nodiscard]] size_t maxWorkersCount() const;
	[[nodiscard]] size_t queueLength() const;

private:
	// It is important that the queue is declared before threads (means it will only be destroyed after all the threads using it stop)
	std::deque<CConsumerBlockingQueue<TaskType>> _queues;
	std::deque<CWorkerThread> _workerThreads; // Cannot be std::vector because CWorkerThread cannot be made movable (let alone copyable)
	const std::string _poolName;
	const size_t _maxNumThreads;
};
