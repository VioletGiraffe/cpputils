#include "cworkerthread.h"
#include "utility/on_scope_exit.hpp"

#include <algorithm>
#include <sstream>

CWorkerThreadPool::CWorkerThread::CWorkerThread(CConsumerBlockingQueue<std::function<void ()> >& queue, const std::string& threadName) :
	_queue(queue),
	_threadName(threadName)
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
	_queue.wakeAllThreads();

	if (_thread.joinable())
		_thread.join();

	_terminate = false;
	_working = false;
}

void CWorkerThreadPool::CWorkerThread::interrupt_point() const
{
	if (_terminate)
		return;
}

std::thread::id CWorkerThreadPool::CWorkerThread::tid() const
{
	return std::this_thread::get_id();
}

void CWorkerThreadPool::CWorkerThread::threadFunc()
{
	_working = true;
	EXEC_ON_SCOPE_EXIT([this]() {
		_working = false;
	});

	try
	{
		while (!_terminate)
		{
			std::function<void()> task;
			_queue.pop(task);
			if (task)
				task();
		}
	}
	catch (std::exception& e)
	{
		assert_unconditional_r((std::string("std::exception caught: ") + e.what()).c_str());
	}
	catch (...)
	{
		assert_unconditional_r("Unknown exception caught");
	}
}


CWorkerThreadPool::CWorkerThreadPool(size_t maxNumThreads, const std::string& poolName) :
	_poolName(poolName),
	_maxNumThreads(maxNumThreads)
{
	for (size_t i = 0; i < maxNumThreads; ++i)
	{
		std::ostringstream ss;
		ss << poolName << " worker thread #" << i+1;
		auto s = ss.str();
		_workerThreads.emplace_back(_queue, ss.str());
		_workerThreads.back().start();
	}
}

void CWorkerThreadPool::enqueue(const std::function<void ()>& task)
{
	_queue.push(task);
}

void CWorkerThreadPool::interrupt_point() const
{
	workerByTid(std::this_thread::get_id()).interrupt_point();
}

const CWorkerThreadPool::CWorkerThread& CWorkerThreadPool::workerByTid(std::thread::id id) const
{
	const auto threadIterator = std::find_if(_workerThreads.begin(), _workerThreads.end(), [&id](const CWorkerThread& thread){
		return thread.tid() == id;
	});

	if (threadIterator != _workerThreads.end())
		return *threadIterator;
	else
	{
		static CConsumerBlockingQueue<std::function<void ()>> q;
		static const CWorkerThread dummmy(q, std::string());
		assert_and_return_unconditional_r("Thread with the specified ID not found", dummmy);
	}
}
