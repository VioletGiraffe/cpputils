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

	while (!_terminate)
	{
		bool ran = false;
		TaggedTask item;
		if (tryGetTask(item))
		{
			if (_pool.taggedTaskCanRun(item.tagState))
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
			}
			_pool.completeTaggedTask(item.tagState);
			ran = true;
		}

		// Wakes on _terminate and on _queuedCount becoming nonzero - i.e. on an enqueue to ANY queue, not just this
		// worker's own: that is what lets an idle worker steal a busy owner's backlog. Both conditions are set outside
		// _idleMutex, so they rely on the notify being sent under it (see enqueue() and stop()).
		if (!ran)
		{
			++_pool._idleCount.value; // Before the predicate reads _queuedCount, or enqueue() could miss this sleeper (see _idleCount)
			{
				std::unique_lock lock(_pool._idleMutex);
				_pool._idleCv.wait_for(lock, std::chrono::milliseconds(5000), [this] { return _terminate.load() || _pool._queuedCount.value.load() > 0; });
			}
			--_pool._idleCount.value;
		}
	}

	if (_finishPendingTasks)
	{
		TaggedTask item;
		while (_pool._queues[_queueIndex].try_pop(item))
		{
			--_pool._queuedCount.value;
			if (_pool.taggedTaskCanRun(item.tagState))
			{
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
			_pool.completeTaggedTask(item.tagState);
		}
	}

	_working = false;
}

bool CWorkerThreadPool::CWorkerThread::tryGetTask(TaggedTask& task)
{
	if (_pool._queues[_queueIndex].try_pop(task))
	{
		--_pool._queuedCount.value;
		return true;
	}
	// Nothing queued anywhere - don't sweep every queue mutex for nothing (the transient overcount of a push
	// in progress costs one wasted sweep at worst)
	if (_pool._queuedCount.value.load() == 0)
		return false;
	// Steal. Each worker's scan starts right past its own index, so concurrent thieves fan out over
	// different victims instead of converging on the same queue.
	const size_t n = _pool._maxNumThreads;
	for (size_t offset = 1; offset < n; ++offset)
	{
		const uint32_t victimIndex = _pool._laneSelectorMod.mod(static_cast<uint32_t>(_queueIndex + offset));
		if (_pool._queues[victimIndex].try_pop(task))
		{
			--_pool._queuedCount.value;
			return true;
		}
	}
	return false;
}

CWorkerThreadPool::CWorkerThreadPool(uint32_t numThreads, std::string poolName) :
	_poolName(std::move(poolName)),
	_maxNumThreads(numThreads),
	_laneSelectorMod(numThreads)
{
	_queues.resize(numThreads);
	for (size_t i = 1; i <= numThreads; ++i)
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
	assert_r(_idleCount.value == 0); // Every exited worker has left the idle wait and decremented; nonzero means a leak in the +/- pairing
	if (completePendingTasks) // All queues drained, so any nonzero count means a pop/removal path missed its decrement
		assert_r(_queuedCount.value == 0);
}

void CWorkerThreadPool::retire(uint64_t tag)
{
	assert_debug_only(tag != 0); // Tag 0 is the "untagged" sentinel and must never be retired (it would wipe unrelated tasks)

	TaskTagState* tagState;
	{
		std::lock_guard lock(_tagStateMutex);
		const auto it = _tagStates.find(tag);
		if (it == _tagStates.end())
			return;

		tagState = it->second.get();
		assert_debug_only(!tagState->retired);
		tagState->retired = true;
	}

	// Each queue lock decides whether a task is removed here or has already been popped by a worker. Popped tasks
	// remain in outstandingTaskCount and either finish normally or observe retired and are skipped before execution.
	size_t removedCount = 0;
	for (auto& q : _queues)
		removedCount += q.remove_if([tagState](const TaggedTask& item) { return item.tagState == tagState; });
	_queuedCount.value -= removedCount;

	std::unique_lock lock(_tagStateMutex);
	assert_debug_only(tagState->outstandingTaskCount >= removedCount);
	tagState->outstandingTaskCount -= removedCount;
	_tagStateChanged.wait(lock, [tagState] { return tagState->outstandingTaskCount == 0; });

	const auto it = _tagStates.find(tag);
	if (it != _tagStates.end() && it->second.get() == tagState)
		_tagStates.erase(it);
}

bool CWorkerThreadPool::taggedTaskCanRun(const TaskTagState* tagState)
{
	if (!tagState)
		return true;

	std::lock_guard lock(_tagStateMutex);
	return !tagState->retired;
}

void CWorkerThreadPool::completeTaggedTask(TaskTagState* tagState)
{
	if (!tagState)
		return;

	std::lock_guard lock(_tagStateMutex);
	assert_debug_only(tagState->outstandingTaskCount > 0);
	if (--tagState->outstandingTaskCount == 0)
		_tagStateChanged.notify_all();
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
	return _queuedCount.value;
}
