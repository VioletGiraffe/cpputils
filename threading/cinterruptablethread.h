#pragma once

#include "thread_helpers.h"

#include "assert/advanced_assert.h"

#include <atomic>
#include <concepts>
#include <exception>
#include <string>
#include <thread>
#include <utility>

// A named worker thread with a cancellation flag
class CInterruptableThread
{
public:
	explicit CInterruptableThread(std::string threadName);
	~CInterruptableThread(); // Requests cancellation and joins

	CInterruptableThread(const CInterruptableThread&) = delete;
	CInterruptableThread& operator=(const CInterruptableThread&) = delete;

	// Requires that no run is in flight: join() before reusing the instance
	template <std::invocable<const std::atomic<bool>&> Payload>
	inline void start(Payload&& payload)
	{
		// Assigning over a joinable std::thread terminates the process, so the previous run must have been joined by now
		assert_r(!_thread.joinable());

		_cancellationRequested = false;

		_thread = std::thread([this, payload{ std::forward<Payload>(payload) }]() mutable {
			setThreadName(_threadName.c_str());
			// Contained so a throw is logged and survivable instead of terminating the process. This cannot stand in
			// for the payload's own completion: whatever its owner waits for, the payload must publish itself.
			try
			{
				payload(_cancellationRequested);
			}
			catch (const std::exception& e)
			{
				assert_unconditional_r("Exception in thread " + _threadName + ": " + e.what());
			}
			catch (...)
			{
				assert_unconditional_r("Unknown exception in thread " + _threadName);
			}
		});
	}

	void requestCancellation() noexcept;
	[[nodiscard]] bool cancellationRequested() const noexcept;

	[[nodiscard]] bool joinable() const noexcept;
	void join(); // No-op if no run is in flight

private:
	std::string _threadName;
	std::thread _thread;
	std::atomic<bool> _cancellationRequested {false};
};

