#pragma once

#include "assert/advanced_assert.h"
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/function2/function2.hpp"
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <cstddef>
#include <deque>
#include <exception>
#include <mutex>
#include <string>
#include <utility>

// A thread-safe class for delayed code execution, useful for cross-thread execution / communication
class CExecutionQueue
{
	static constexpr int untagged = -1;

	// Fits a this pointer and three 64-bit values without allocating on 64-bit platforms.
	using Task = fu2::function_base<true, false, fu2::capacity_fixed<32>, true, false, void()>;

	struct QueuedTask
	{
		int tag = 0;
		Task code;
	};


public:
	enum ExecutionMode {execFirst, execAll};

	CExecutionQueue() noexcept = default;

	CExecutionQueue(const CExecutionQueue&) = delete;
	CExecutionQueue& operator=(const CExecutionQueue&) = delete;

	// After callers have prevented new operations, wait for an operation already inside the queue lock to finish.
	inline ~CExecutionQueue()
	{
		std::lock_guard<std::mutex> locker(_queueMutex);
	}

	inline void enqueue(Task code, int tag = untagged)
	{
		std::lock_guard<std::mutex> locker(_queueMutex);
		const auto existingTask = tag == untagged ? _queue.end() : std::find_if(_queue.begin(), _queue.end(), [tag](const QueuedTask& task){return task.tag == tag;});
		if (existingTask != _queue.end())
			_queue.erase(existingTask);

		_queue.emplace_back(QueuedTask{ tag, std::move(code) });
	}

	// Newly added work cannot extend an execAll() drain beyond the number of tasks pending at entry.
	inline void exec(ExecutionMode mode = execAll)
	{
		std::size_t tasksToExecute = 1;
		if (mode == execAll)
		{
			std::lock_guard<std::mutex> locker(_queueMutex);
			tasksToExecute = _queue.size();
		}

		QueuedTask queuedTask;
		while (tasksToExecute > 0 && tryPop(queuedTask))
		{
			--tasksToExecute;
			if (!queuedTask.code)
				continue;

			// Contain each task so one that throws cannot abort the rest of the batch or reach the caller's loop.
			try
			{
				queuedTask.code();
			}
			catch (const std::exception& e)
			{
				assert_unconditional_r(std::string{ "Exception in a queued task: " } + e.what());
			}
			catch (...)
			{
				assert_unconditional_r("Unknown exception in a queued task");
			}
		}
	}

private:
	inline bool tryPop(QueuedTask& task)
	{
		std::lock_guard<std::mutex> locker(_queueMutex);
		if (!_queue.empty())
		{
			task = std::move(_queue.front());
			_queue.pop_front();
			return true;
		}

		return false;
	}

	std::deque<QueuedTask> _queue;
	std::mutex              _queueMutex;
};
