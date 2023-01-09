#include "cworkerthread.h"
#include "thread_helpers.h"
#include "assert/advanced_assert.h"
#include "utility/on_scope_exit.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

CWorkerThreadPool::CWorkerThread::CWorkerThread(CConsumerBlockingQueue<TaskType>& queue, std::string threadName) :
	_threadName(std::move(threadName)),
	_queue(queue),
	_thread{ &CWorkerThread::threadFunc, this }
{
}

CWorkerThreadPool::CWorkerThread::~CWorkerThread()
{
	stop();
}

bool CWorkerThreadPool::CWorkerThread::isStarted() const noexcept
{
	return _working;
}

void CWorkerThreadPool::CWorkerThread::stop()
{
	_terminate = true;

	// In case the thread was waiting. Since we can't wake only a specific thread, we have to wake all of them to terminate one.
	// TODO: find a deterministic fix for the shutdown issue
	std::this_thread::sleep_for(std::chrono::milliseconds(1));
	_queue.wakeAllThreads();

	if (_thread.joinable())
		_thread.join();

	_terminate = false;
	_working = false;
}

void CWorkerThreadPool::CWorkerThread::threadFunc() noexcept
{
	_working = true;

	setThreadName(_threadName.c_str());

	EXEC_ON_SCOPE_EXIT([this]() {
		_working = false;
	});

	try
	{
		while (!_terminate)
		{
			TaskType task;
			if (_queue.pop(task, 5000))
				task();
		}
	}
	catch (const std::exception& e)
	{
		assert_unconditional_r(std::string{"std::exception caught: "} + e.what());
	}
	catch (...)
	{
		assert_unconditional_r("Unknown exception caught");
	}
}

CWorkerThreadPool::CWorkerThreadPool(size_t maxNumThreads, std::string poolName) :
	_poolName(std::move(poolName)),
	_maxNumThreads(maxNumThreads)
{
	for (size_t i = 1; i <= maxNumThreads; ++i)
	{
		std::ostringstream stream;
		stream << _poolName << " worker thread #" << i;
		_workerThreads.emplace_back(_queue, stream.str());
	}
}

void CWorkerThreadPool::finishAllThreads()
{
	_workerThreads.clear();
}

void CWorkerThreadPool::waitUntilStarted() noexcept
{
	for (auto& th : _workerThreads)
	{
		while (!th.isStarted())
		{
			std::this_thread::yield();
		}
	}
}

size_t CWorkerThreadPool::maxWorkersCount() const
{
	return _maxNumThreads;
}

size_t CWorkerThreadPool::queueLength() const
{
	return _queue.size();
}
