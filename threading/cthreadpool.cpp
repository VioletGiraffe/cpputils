#include "cthreadpool.h"
#include "thread_helpers.h"
#include "assert/advanced_assert.h"

#include <chrono>
#include <sstream>
#include <thread>
#include <utility>

CThreadPool::State::State(const uint32_t numThreads) :
	maxNumThreads(numThreads),
	laneSelectorMod(numThreads)
{
	queues.resize(numThreads);
}

void CThreadPool::State::runWorker(const size_t ownQueueIndex, const std::string& threadName) noexcept
{
	++startedWorkerCount;

	setThreadName(threadName.c_str());

	while (!terminate)
	{
		bool ran = false;
		TaggedTask item;
		if (tryGetTask(ownQueueIndex, item))
		{
			if (taggedTaskCanRun(item.tagState))
			{
				// The task's exception is contained right here: a throw must not unwind runWorker, which would
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
					log_unconditional_r(std::string{ "Exception in a worker task: " } + e.what());
				}
				catch (...)
				{
					log_unconditional_r("Unknown exception in a worker task");
				}
			}
			completeTaggedTask(item.tagState);
			ran = true;
		}

		// Wakes on terminate and on queuedCount becoming nonzero - i.e. on an enqueue to ANY queue, not just this
		// worker's own: that is what lets an idle worker steal a busy owner's backlog. Both conditions are set outside
		// idleMutex, so they rely on the notify being sent under it (see enqueue() and requestWorkersStop()).
		if (!ran)
		{
			++idleCount.value; // Before the predicate reads queuedCount, or enqueue() could miss this sleeper (see idleCount)
			{
				std::unique_lock lock(idleMutex);
				idleCv.wait_for(lock, std::chrono::milliseconds(5000), [this] { return terminate.load() || queuedCount.value.load() > 0; });
			}
			--idleCount.value;
		}
	}
}

bool CThreadPool::State::tryGetTask(const size_t ownQueueIndex, TaggedTask& task)
{
	if (queues[ownQueueIndex].try_pop(task))
	{
		--queuedCount.value;
		return true;
	}
	// Nothing queued anywhere - don't sweep every queue mutex for nothing (the transient overcount of a push
	// in progress costs one wasted sweep at worst)
	if (queuedCount.value.load() == 0)
		return false;
	// Steal. Each worker's scan starts right past its own index, so concurrent thieves fan out over
	// different victims instead of converging on the same queue.
	const size_t n = maxNumThreads;
	for (size_t offset = 1; offset < n; ++offset)
	{
		const uint32_t victimIndex = laneSelectorMod.mod(static_cast<uint32_t>(ownQueueIndex + offset));
		if (queues[victimIndex].try_pop(task))
		{
			--queuedCount.value;
			return true;
		}
	}
	return false;
}

CThreadPool::CThreadPool(const uint32_t numThreads, std::string poolName, const ShutdownPolicy shutdownPolicy) :
	_shutdownPolicy(shutdownPolicy),
	_state(std::make_shared<State>(numThreads))
{
	_workerThreads.reserve(numThreads);
	for (size_t i = 1; i <= numThreads; ++i)
	{
		std::ostringstream stream;
		stream << poolName << " worker thread #" << i;
		// The thread's copy of state is released only after runWorker returns.
		_workerThreads.emplace_back([state = _state, queueIndex = i - 1, threadName = stream.str()] { state->runWorker(queueIndex, threadName); });
	}
}

CThreadPool::~CThreadPool() noexcept
{
	if (_shutdownPolicy == ShutdownPolicy::AbandonRunningTasks)
		abandonAllThreads();
	else
		finishAllThreads(false);
}

void CThreadPool::requestWorkersStop()
{
	_state->terminate = true;
	// Every worker observes the stop request before this single wake. The lock serializes the request with
	// each idle waiter's predicate check, eliminating the lost-wakeup window.
	std::lock_guard locker{ _state->idleMutex };
	_state->idleCv.notify_all();
}

void CThreadPool::finishAllThreads(bool completePendingTasks)
{
	requestWorkersStop();

	// Empty after abandonAllThreads(), whose detached workers may still be parked
	if (!_workerThreads.empty())
	{
		for (auto& th : _workerThreads)
			th.join(); // Any queued tasks are left in place for the optional drain below

		_workerThreads.clear();
		assert_r(_state->idleCount.value == 0); // Every exited worker has left the idle wait and decremented; nonzero means a leak in the +/- pairing
	}

	if (!completePendingTasks)
		return;

	// With every worker gone this is the only moment nothing is running, so a task that enqueues more work - onto
	// ANY lane - cannot outrun the drain the way a per-lane worker drain could. Sweep all lanes repeatedly until a
	// full pass comes up empty: tasks spawned mid-drain land in some lane and a subsequent pass collects them.
	TaggedTask item;
	for (bool drainedAny = true; drainedAny; )
	{
		drainedAny = false;
		for (auto& q : _state->queues)
		{
			while (q.try_pop(item))
			{
				--_state->queuedCount.value;
				drainedAny = true;
				_state->executeContainedTask(item);
			}
		}
	}

	assert_r(_state->queuedCount.value == 0); // Everything drained, so any nonzero count means a pop/removal path missed its decrement
}

void CThreadPool::abandonAllThreads()
{
	requestWorkersStop();

	// Queued tasks are destroyed here, not with the state: a detached worker may release the state long after the captures' owners are gone.
	for (auto& q : _state->queues)
	{
		TaggedTask item;
		while (q.try_pop(item))
		{
			--_state->queuedCount.value;
			_state->completeTaggedTask(item.tagState);
		}
	}

	for (auto& th : _workerThreads)
		th.detach();
	_workerThreads.clear();
}

void CThreadPool::retire(uint64_t tag)
{
	assert_debug_only(tag != 0); // Tag 0 is the "untagged" sentinel and must never be retired (it would wipe unrelated tasks)

	State& state = *_state;
	TaskTagState* tagState;
	{
		std::lock_guard lock(state.tagStateMutex);
		const auto it = state.tagStates.find(tag);
		if (it == state.tagStates.end())
			return;

		tagState = &it->second;
		assert_debug_only(!tagState->retired);
		tagState->retired = true;
	}

	// Each queue lock decides whether a task is removed here or has already been popped by a worker. Popped tasks
	// remain in outstandingTaskCount and either finish normally or observe retired and are skipped before execution.
	size_t removedCount = 0;
	for (auto& q : state.queues)
		removedCount += q.remove_if([tagState](const TaggedTask& item) noexcept { return item.tagState == tagState; });
	state.queuedCount.value -= removedCount;

	std::unique_lock lock(state.tagStateMutex);
	assert_debug_only(tagState->outstandingTaskCount >= removedCount);
	tagState->outstandingTaskCount -= removedCount;
	state.tagStateChanged.wait(lock, [tagState] { return tagState->outstandingTaskCount == 0; });

	const auto it = state.tagStates.find(tag);
	if (it != state.tagStates.end() && &it->second == tagState)
		state.tagStates.erase(it);
}

bool CThreadPool::State::taggedTaskCanRun(const TaskTagState* tagState)
{
	if (!tagState)
		return true;

	std::lock_guard lock(tagStateMutex);
	return !tagState->retired;
}

void CThreadPool::State::completeTaggedTask(TaskTagState* tagState)
{
	if (!tagState)
		return;

	std::lock_guard lock(tagStateMutex);
	assert_debug_only(tagState->outstandingTaskCount > 0);
	if (--tagState->outstandingTaskCount == 0)
		tagStateChanged.notify_all();
}

void CThreadPool::State::executeContainedTask(TaggedTask& item)
{
	if (taggedTaskCanRun(item.tagState))
	{
		// A throw must not escape to unwind the caller. A future-wrapped task that throws leaves its promise
		// unfulfilled, so the waiter gets std::future_error (broken_promise) rather than the task's exception.
		try
		{
			item.task();
		}
		catch (const std::exception& e)
		{
			log_unconditional_r(std::string{ "Exception in a worker task: " } + e.what());
		}
		catch (...)
		{
			log_unconditional_r("Unknown exception in a worker task");
		}
	}
	completeTaggedTask(item.tagState);
}

void CThreadPool::waitUntilStarted() noexcept
{
	while (_state->startedWorkerCount < _workerThreads.size())
		std::this_thread::yield();
}

size_t CThreadPool::maxWorkersCount() const
{
	return _state->maxNumThreads;
}

size_t CThreadPool::queueLength() const
{
	return _state->queuedCount.value;
}
