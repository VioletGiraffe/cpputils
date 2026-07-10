#include "cworkerthread.h"
#include "thread_helpers.h"
#include "assert/advanced_assert.h"

#include <algorithm>
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

	// In case the thread was already waiting on the queue
	_pool._queues[_queueIndex].wakeAllThreads();

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
					// Pass the wake along: more work remains queued and this worker is about to be busy with its
					// task for arbitrarily long. A producer's wake may have landed here even though this worker's
					// own queue already spoke for it (a parked flag cannot distinguish "idle" from "woken but not
					// yet running"), so whoever takes a task while surplus work remains wakes one more parked
					// worker - the wake hops until it reaches a free worker. At most one hop per popped task, so
					// this cannot storm.
					if (_pool._queuedCount.load() > 0)
						_pool.wakeOneParkedWorker(_queueIndex + 1);
					item.task();
					ran = true;
				}
			}

			// Wait for new work OUTSIDE the pool lock: a blocking wait while holding it would stall retire() for the whole timeout.
			// Wakes immediately on _terminate, and on _queuedCount becoming nonzero - i.e. on an enqueue to ANY queue,
			// not just this worker's own: that is what lets a parked worker steal a busy owner's backlog. Both flags are
			// set outside the queue mutex, so they rely on the notify being sent under it (see wakeAllThreads).
			if (!ran)
			{
				auto& parkedFlag = _pool._parkedFlags[_queueIndex].parked;
				// The flag is set inside the predicate, not once before the wait: wait_for re-evaluates the predicate and
				// re-blocks internally without returning here, and a claim-wake (see wakeOneParkedWorker) that arrives only
				// to find the count already drained ends in exactly such a re-block. The claimer took the flag, so without
				// re-setting it the worker would re-block claimed-but-parked: invisible to every claim-gated wake, hence
				// unreachable (permanently, if its own lane gets no further pushes). Storing before reading _queuedCount
				// keeps the wake handshake lossless on every re-block, not just the first (see _parkedFlags).
				_pool._queues[_queueIndex].waitForItem(5000, [this, &parkedFlag] {
					parkedFlag = true;
					return _terminate.load() || _pool._queuedCount.load() > 0;
				});
				parkedFlag = false;
			}
		}

		if (_finishPendingTasks)
		{
			TaggedTask item;
			while (_pool._queues[_queueIndex].try_pop(item))
			{
				--_pool._queuedCount;
				item.task();
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

// Claiming the flag with a CAS ensures concurrent callers pick DISTINCT sleepers - a plain read would re-pick
// a worker that was already woken but not yet scheduled (its flag still set). A no-op when no one is parked:
// then every worker is awake, and an awake worker neither parks while work remains queued nor takes a task
// without re-delegating its wake (see threadFunc), so queued work cannot be overlooked.
void CWorkerThreadPool::wakeOneParkedWorker(size_t scanStart)
{
	for (size_t k = 0; k < _maxNumThreads; ++k)
	{
		const size_t i = (scanStart + k) % _maxNumThreads;
		bool expected = true;
		if (_parkedFlags[i].parked.compare_exchange_strong(expected, false))
		{
			_queues[i].wakeAllThreads();
			return;
		}
	}
}

// Wakes every currently-parked worker (each claimed with a CAS, harmless if a concurrent caller races). A task
// landing behind a busy owner must be stolen by a worker whose own lane is empty; per-lane condvars make that
// worker reachable only by a wake aimed at its lane, so a single wake - which may land on a worker that has its
// own task - cannot be relied on to reach it. A woken worker that finds nothing simply re-parks (its predicate
// keeps it awake only while _queuedCount > 0).
void CWorkerThreadPool::wakeAllParkedWorkers()
{
	for (size_t i = 0; i < _maxNumThreads; ++i)
	{
		bool expected = true;
		if (_parkedFlags[i].parked.compare_exchange_strong(expected, false))
			_queues[i].wakeAllThreads();
	}
}

CWorkerThreadPool::CWorkerThreadPool(size_t maxNumThreads, std::string poolName) :
	_poolName(std::move(poolName)),
	_maxNumThreads(maxNumThreads),
	_parkedFlags(maxNumThreads)
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
	// Every worker clears its flag on leaving the wait, and all of them have exited by now
	assert_r(std::none_of(_parkedFlags.cbegin(), _parkedFlags.cend(), [](const ParkedFlag& f) { return f.parked.load(); }));
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
