#pragma once

#include "cconsumerblockingqueue.h"

#include <atomic>
#include <deque>
#include <functional>
#include <string>
#include <thread>

class CWorkerThreadPool
{
	class CWorkerThread
	{
	public:
		CWorkerThread(CConsumerBlockingQueue<std::function<void()>>& queue, std::string threadName);
		~CWorkerThread();

		CWorkerThread(const CWorkerThread&) = delete;
		CWorkerThread& operator=(const CWorkerThread&) = delete;

		[[nodiscard]] bool isStarted() const noexcept;

		void stop();

	private:
		void threadFunc() noexcept;

	private:
		CConsumerBlockingQueue<std::function<void()>>& _queue;
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
	size_t enqueue(std::function<void ()> task);
	// Blocks until all the worker threads are started
	void waitUntilStarted() noexcept;

	size_t maxWorkersCount() const;
	size_t queueLength() const;

private:
	CConsumerBlockingQueue<std::function<void()>> _queue; // It may be important that the queue is declared before threads (means it will only be destroyed after all the threads using it stop)
	std::deque<CWorkerThread> _workerThreads; // Cannot be std::vector because CWorkerThread cannot be made movable (let alone copyable)
	const std::string _poolName;
	const size_t _maxNumThreads;
};
