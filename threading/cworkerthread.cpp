#include "cworkerthread.h"
#include "thread_helpers.h"
#include "assert/advanced_assert.h"

#include <algorithm>
#include <sstream>
#include <utility>

CWorkerThreadPool::CWorkerThread::CWorkerThread(CConsumerBlockingQueue<TaggedTask>& queue, std::shared_mutex& poolMutex, std::string threadName) :
	_queue(queue),
	_poolMutex(poolMutex),
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

	// In case the thread was already waiting on the queue
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
			bool ran = false;
			{
				// pop + execute together under a shared lock. retire() takes the same lock exclusively, so it can neither
				// remove a task that's already running nor allow a task to start after its owner has been retired.
				std::shared_lock sharedLock(_poolMutex);
				TaggedTask item;
				if (_queue.try_pop(item))
				{
					item.task();
					ran = true;
				}
			}

			// Wait for new work OUTSIDE the pool lock: a blocking wait while holding it would stall retire() for the whole timeout.
			// Also wakes immediately on _terminate so stop() never has to wait out the timeout to shut the thread down.
			if (!ran)
				_queue.waitForItem(5000, [this] { return _terminate.load(); });
		}

		if (_finishPendingTasks)
		{
			TaggedTask item;
			while (_queue.try_pop(item))
				item.task();
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
		_workerThreads.emplace_back(_queues[i - 1], _poolMutex, stream.str());
	}
}

void CWorkerThreadPool::finishAllThreads(bool completePendingTasks)
{
	for (auto& th : _workerThreads)
		th.stop(completePendingTasks);

	_workerThreads.clear();
}

void CWorkerThreadPool::retire(uint64_t tag)
{
	assert_debug_only(tag != 0); // Tag 0 is the "untagged" sentinel and must never be retired (it would wipe unrelated tasks)

	// Exclusive lock: blocks until every in-flight task (all shared holders) finishes, after which no task is running.
	// Then drop this tag's not-yet-started tasks from every per-thread queue. A task with this tag cannot be enqueued
	// concurrently here, because its only poster is the owner being destroyed (which is what called retire).
	std::unique_lock exclusiveLock(_poolMutex);
	for (auto& q : _queues)
		q.remove_if([tag](const TaggedTask& item) { return item.tag == tag; });
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
