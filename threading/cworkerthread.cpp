#include "cworkerthread.h"
#include "thread_helpers.h"
#include "assert/advanced_assert.h"
#include "utility/on_scope_exit.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

CWorkerThreadPool::CWorkerThread::CWorkerThread(CConsumerBlockingQueue<std::function<void ()> >& queue, std::string threadName) :
	_threadName(std::move(threadName)),
	_queue(queue)
{
}

CWorkerThreadPool::CWorkerThread::~CWorkerThread()
{
	stop();
}

void CWorkerThreadPool::CWorkerThread::start()
{
	if (_working)
		return;

	if (_thread.joinable())
		_thread.join();

	_working = true;
	_thread = std::thread(&CWorkerThread::threadFunc, this);
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

void CWorkerThreadPool::CWorkerThread::threadFunc()
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
			std::function<void()> task;
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
		_workerThreads.emplace_back(_queue, (std::ostringstream{} << _poolName << " worker thread #" << i).str()).start();
	}
}

void CWorkerThreadPool::finishAllThreads()
{
	_workerThreads.clear();
}

size_t CWorkerThreadPool::enqueue(const std::function<void ()>& task)
{
	auto pushResult = _queue.try_push(task);
	if (pushResult.pushed == false)
	{
		assert_unconditional_r("Max queue length exceeded, retrying...");
		do {
			pushResult = _queue.try_push(task);
		} while (pushResult.pushed == false);
	}

	return pushResult.queueSize;
}

size_t CWorkerThreadPool::maxWorkersCount() const
{
	return _maxNumThreads;
}

size_t CWorkerThreadPool::queueLength() const
{
	return _queue.size();
}
