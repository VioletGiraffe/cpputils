#pragma once

#include "cconsumerblockingqueue.h"

#include <atomic>
#include <deque>
#include <future>
#include <string>
#include <thread>
#include <utility>

using TaskType = std::move_only_function<void()>;

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
		std::atomic<bool> _working {false};
		std::atomic<bool> _terminate {false};
		std::thread _thread;
	};

public:
	CWorkerThreadPool(size_t maxNumThreads, std::string poolName);
	void finishAllThreads(); // Does the same thing the destructor does, but can be called when needed

	CWorkerThreadPool(const CWorkerThreadPool&) = delete;
	CWorkerThreadPool& operator=(const CWorkerThreadPool&) = delete;

	// Returns the current queue length
	template <typename F>
	size_t enqueue(F&& task)
	{
		return _queue.push(std::forward<F>(task));
	}

	template <typename F>
	std::future<void> enqueueWithFuture(F&& task)
	{
		std::promise<void> p;
		auto future = p.get_future();
		_queue.push([task{ std::move(task) }, p{ std::move(p) }] () mutable {
			task();
			p.set_value();
		});

		return future;
	}

	// Blocks until all the worker threads are started
	void waitUntilStarted() noexcept;

	size_t maxWorkersCount() const;
	size_t queueLength() const;

private:
	CConsumerBlockingQueue<TaskType> _queue; // It may be important that the queue is declared before threads (means it will only be destroyed after all the threads using it stop)
	std::deque<CWorkerThread> _workerThreads; // Cannot be std::vector because CWorkerThread cannot be made movable (let alone copyable)
	const std::string _poolName;
	const size_t _maxNumThreads;
};
