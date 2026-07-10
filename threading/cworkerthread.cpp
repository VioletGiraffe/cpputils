#include "cworkerthread.h"
#include "thread_helpers.h"
#include "assert/advanced_assert.h"

#include <sstream>
#include <utility>

CWorkerThreadPool::CWorkerThread::CWorkerThread(CWorkerThreadPool& pool, size_t queueIndex, std::string threadName) :
	_pool(pool),
	_queueIndex(queueIndex),
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

	// In case the thread was already in the idle wait. The lock serializes the terminate store above with a
	// waiter's predicate check, eliminating the lost-wakeup window. notify_all rather than notify_one: one wake
	// could land on a different worker, whose own _terminate is not set (it would just re-check and re-block).
	{
		std::lock_guard locker{ _pool._idleMutex };
		_pool._idleCv.notify_all();
	}

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
				std::shared_lock sharedLock(_pool._poolMutex);
				TaggedTask item;
				if (tryGetTask(item))
				{
					// The task's exception is contained right here: a throw must not unwind threadFunc, which would
					// silently and permanently shrink the pool by one worker. Inlined rather than a helper function:
					// MSVC never inlines a function containing try, and the extra call is measurable on nanotasks.
					// A future-wrapped task that throws leaves its promise unfulfilled, so the waiter gets
					// std::future_error (broken_promise) rather than the task's exception.
					try
					{
						item.task();
					}
					catch (const std::exception& e)
					{
						assert_unconditional_r(std::string{ "Exception in a worker task: " } + e.what());
					}
					catch (...)
					{
						assert_unconditional_r("Unknown exception in a worker task");
					}
					ran = true;
				}
			}

			// Wait for new work OUTSIDE the pool lock: a blocking wait while holding it would stall retire() for the whole timeout.
			// Wakes on _terminate and on _queuedCount becoming nonzero - i.e. on an enqueue to ANY queue, not just this
			// worker's own: that is what lets an idle worker steal a busy owner's backlog. Both conditions are set outside
			// _idleMutex, so they rely on the notify being sent under it (see enqueue() and stop()).
			if (!ran)
			{
				++_pool._idleCount; // Before the predicate reads _queuedCount, or enqueue() could miss this sleeper (see _idleCount)
				{
					std::unique_lock lock(_pool._idleMutex);
					_pool._idleCv.wait_for(lock, std::chrono::milliseconds(5000), [this] { return _terminate.load() || _pool._queuedCount.load() > 0; });
				}
				--_pool._idleCount;
			}
		}

		if (_finishPendingTasks)
		{
			TaggedTask item;
			while (_pool._queues[_queueIndex].try_pop(item))
			{
				--_pool._queuedCount;
				try // Same task-exception containment as in the main loop above
				{
					item.task();
				}
				catch (const std::exception& e)
				{
					assert_unconditional_r(std::string{ "Exception in a worker task: " } + e.what());
				}
				catch (...)
				{
					assert_unconditional_r("Unknown exception in a worker task");
				}
			}
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

bool CWorkerThreadPool::CWorkerThread::tryGetTask(TaggedTask& task)
{
	if (_pool._queues[_queueIndex].try_pop(task))
	{
		--_pool._queuedCount;
		return true;
	}
	// Nothing queued anywhere - don't sweep every queue mutex for nothing (the transient overcount of a push
	// in progress costs one wasted sweep at worst)
	if (_pool._queuedCount.load() == 0)
		return false;
	// Steal. Each worker's scan starts right past its own index, so concurrent thieves fan out over
	// different victims instead of converging on the same queue.
	const size_t n = _pool._maxNumThreads;
	for (size_t offset = 1; offset < n; ++offset)
	{
		if (_pool._queues[(_queueIndex + offset) % n].try_pop(task))
		{
			--_pool._queuedCount;
			return true;
		}
	}
	return false;
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
		_workerThreads.emplace_back(*this, i - 1, stream.str());
	}
}

void CWorkerThreadPool::finishAllThreads(bool completePendingTasks)
{
	for (auto& th : _workerThreads)
		th.stop(completePendingTasks);

	_workerThreads.clear();
	assert_r(_idleCount == 0); // Every exited worker has left the idle wait and decremented; nonzero means a leak in the +/- pairing
	if (completePendingTasks) // All queues drained, so any nonzero count means a pop/removal path missed its decrement
		assert_r(_queuedCount == 0);
}

void CWorkerThreadPool::retire(uint64_t tag)
{
	assert_debug_only(tag != 0); // Tag 0 is the "untagged" sentinel and must never be retired (it would wipe unrelated tasks)

	// Exclusive lock: blocks until every in-flight task (all shared holders) finishes, after which no task is running.
	// Then drop this tag's not-yet-started tasks from every per-thread queue. A task with this tag cannot be enqueued
	// concurrently here, because its only poster is the owner being destroyed (which is what called retire).
	std::unique_lock exclusiveLock(_poolMutex);
	size_t removedCount = 0;
	for (auto& q : _queues)
		removedCount += q.remove_if([tag](const TaggedTask& item) { return item.tag == tag; });
	_queuedCount -= removedCount;
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
	return _queuedCount;
}
