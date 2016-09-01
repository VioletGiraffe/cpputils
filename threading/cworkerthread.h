#pragma once

#include "cconsumerblockingqueue.h"
#include "../assert/advanced_assert.h"

#include <atomic>
#include <string>
#include <thread>

class CWorkerThreadPool
{
	class CWorkerThread
	{
	public:
		explicit CWorkerThread(CConsumerBlockingQueue<std::function<void()>>& queue, const std::string& threadName);
		~CWorkerThread();

		CWorkerThread(const CWorkerThread&) = delete;
		CWorkerThread& operator=(const CWorkerThread&) = delete;

		void start();
		void stop();

		void interrupt_point() const;

		std::thread::id tid() const;

	private:
		void threadFunc();

	private:
		CConsumerBlockingQueue<std::function<void()>>& _queue;
		std::thread _thread;
		std::atomic<bool> _working {false};
		std::atomic<bool> _terminate {false};
		std::string _threadName;
	};

public:
	CWorkerThreadPool(size_t maxNumThreads, const std::string& poolName);
	~CWorkerThreadPool() = default;

	CWorkerThreadPool(const CWorkerThreadPool&) = delete;
	CWorkerThreadPool& operator=(const CWorkerThreadPool&) = delete;

	void enqueue(const std::function<void()>& task);

	void interrupt_point() const;

private:
	const CWorkerThread& workerByTid(std::thread::id id) const;

private:
	CConsumerBlockingQueue<std::function<void()>> _queue;
	std::deque<CWorkerThread> _workerThreads;
	const std::string _poolName;
	const size_t _maxNumThreads;
};


