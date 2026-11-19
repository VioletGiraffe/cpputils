#pragma once

#include "cinterruptablethread.h"

#include "assert/advanced_assert.h"
#include "utility/on_scope_exit.hpp"

#include <atomic>
#include <concepts>
#include <exception>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

// One run of a payload on its own thread, for an owner that polls for the result. Call from the owner's thread only.
// Destruction cancels and joins: it blocks until the payload returns.
template <typename Result>
class CBackgroundTask
{
public:
	explicit CBackgroundTask(std::string threadName) : _thread{ std::move(threadName) } {}

	// The payload's thread holds `this`
	CBackgroundTask(const CBackgroundTask&) = delete;
	CBackgroundTask& operator=(const CBackgroundTask&) = delete;
	CBackgroundTask(CBackgroundTask&&) = delete;
	CBackgroundTask& operator=(CBackgroundTask&&) = delete;

	// Once per instance. A payload that stops early on cancellation returns nullopt.
	// A task whose thread fails to start finishes at once, with no result.
	template <typename Payload>
		requires std::same_as<std::invoke_result_t<std::decay_t<Payload>&, const std::atomic<bool>&>, std::optional<Result>>
	void start(Payload&& payload)
	{
		assert_and_return_r(!_thread.joinable() && !_finished, );

		try
		{
			_thread.start([this, payload{ std::forward<Payload>(payload) }](const std::atomic<bool>& cancelled) mutable {
				// Also on a throw, which the thread only logs: the owner waits for this
				EXEC_ON_SCOPE_EXIT([this] { _finished = true; });
				_result = payload(cancelled);
			});
		}
		catch (const std::exception& e)
		{
			log_unconditional_r(std::string{ "Background task failed to start: " } + e.what());
			_finished = true;
		}
	}

	// Joins the thread once the payload has returned
	[[nodiscard]] bool finished()
	{
		if (!_finished)
			return false;

		_thread.join();
		return true;
	}

	void cancel() noexcept { _thread.requestCancellation(); } // No effect before start()

	// Returns once the payload has returned; finished() is then true. No effect before start().
	void cancelAndWait()
	{
		_thread.requestCancellation();
		_thread.join();
	}

	// Requires finished(). Empty if the payload returned nullopt or threw, or if the thread failed to start.
	[[nodiscard]] const std::optional<Result>& result() const noexcept
	{
		assert_r(_finished);
		return _result;
	}

private:
	std::optional<Result> _result; // Written by the payload's thread before it sets _finished
	std::atomic<bool> _finished{ false };
	// Last: destroyed first, joining the thread before the fields it writes are gone
	CInterruptableThread _thread;
};
