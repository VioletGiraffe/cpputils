#include "cworkerthread.h"
#include "thread_helpers.h"
#include "assert/advanced_assert.h"

#include <algorithm>
#include <sstream>
#include <utility>

CWorkerThreadPool::CWorkerThread::CWorkerThread(CConsumerBlockingQueue<TaskType>& queue, std::string threadName) :
	_queue(queue),
	_threadName(std::move(threadName)),
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

void CWorkerThreadPool::CWorkerThread::stop(bool finishPendingTasks)
{
	// The order matters
	_finishPendingTasks = finishPendingTasks;
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

	try
	{
		while (!_terminate)
		{
			TaskType task;
			if (_queue.pop(task, 5000))
				task();
		}

		if (_finishPendingTasks)
		{
			TaskType task;
			while (_queue.try_pop(task))
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

	_working = false;
}

CWorkerThreadPool::CWorkerThreadPool(size_t maxNumThreads, std::string poolName) :
	_poolName(std::move(poolName)),
	_maxNumThreads(maxNumThreads)
{
	_queues.resize(maxNumThreads);
	for (size_t i = 1; i <= maxNumThreads; ++i)
	{
		std::ostringstream stream;
		stream << _poolName << " worker thread #" << i;
		_workerThreads.emplace_back(_queues[i - 1], stream.str());
	}
}

void CWorkerThreadPool::finishAllThreads(bool completePendingTasks)
{
	for (auto& th : _workerThreads)
		th.stop(completePendingTasks);

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
	size_t length = 0;
	for (const auto& q : _queues)
		length += q.size();

	return length;
}
