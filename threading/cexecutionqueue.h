#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/function2/function2.hpp"
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <deque>
#include <mutex>
#include <utility>

// A thread-safe class for delayed code execution, useful for cross-thread execution / communication
class CExecutionQueue
{
	// Fits a this pointer and three 64-bit values without allocating on 64-bit platforms.
	using Task = fu2::function_base<true, false, fu2::capacity_fixed<32>, true, false, void()>;

	struct Executee
	{
		int tag = 0;
		Task code;
	};


public:
	enum ExecutionMode {execFirst, execAll};

	CExecutionQueue() noexcept = default;

	CExecutionQueue(const CExecutionQueue&) = delete;
	CExecutionQueue& operator=(const CExecutionQueue&) = delete;

	inline ~CExecutionQueue()
	{
		std::lock_guard<std::mutex> locker(_queueMutex);
	}

	inline void enqueue(Task code, int tag = -1)
	{
		std::lock_guard<std::mutex> locker(_queueMutex);
		const auto existingExecutee = tag == -1 ? _queue.end() : std::find_if(_queue.begin(), _queue.end(), [tag](const Executee& e){return e.tag == tag;});
		if (existingExecutee != _queue.end())
			_queue.erase(existingExecutee);

		_queue.emplace_back(Executee{ tag, std::move(code) });
	}

	inline void exec(ExecutionMode mode = execAll)
	{
		Executee queueItem;
		while (try_pop(queueItem))
		{
			if (queueItem.code)
				queueItem.code();

			if (mode == execFirst)
				return;
		}
	}

private:
	inline bool try_pop(Executee& e)
	{
		std::lock_guard<std::mutex> locker(_queueMutex);
		if (!_queue.empty())
		{
			e = std::move(_queue.front());
			_queue.pop_front();
			return true;
		}

		return false;
	}

private:
	std::deque<Executee> _queue;
	std::mutex           _queueMutex;
};
