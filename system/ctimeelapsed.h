#pragma once

#include <chrono>

class CTimeElapsed
{
public:
	explicit CTimeElapsed(bool autoStart = false) noexcept {
		if (autoStart)
			start();
	}
	constexpr ~CTimeElapsed() noexcept = default;

	CTimeElapsed(const CTimeElapsed&) = delete;
	CTimeElapsed& operator=(const CTimeElapsed&) = delete;

	inline void start() noexcept {
		_previouslyAccumulatedTime = std::chrono::nanoseconds{ 0 };
		_startTimeStamp = Clock::now();
		_paused = false;
	}

	// pause/resume are idempotent but not nestable: one resume() undoes any number of pause() calls.
	inline void pause() noexcept {
		if (_paused)
			return;

		_previouslyAccumulatedTime += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - _startTimeStamp);
		_paused = true;
	}

	inline void resume() noexcept {
		if (!_paused)
			return;

		_startTimeStamp = Clock::now();
		_paused = false;
	}

	// Returns the time since the last start() call, minus however long the pause(s) had lasted, in the specified std::chrono duration units
	template <typename StdChronoDurationUnit = std::chrono::milliseconds>
	[[nodiscard]] inline uint64_t elapsed() const noexcept {
		// now() keeps advancing while paused, so the running term is dropped rather than rebased onto the pause instant.
		const auto timeSinceResume = _paused
			? Clock::duration::zero()
			: Clock::now() - _startTimeStamp;
		return std::chrono::duration_cast<StdChronoDurationUnit>(timeSinceResume + _previouslyAccumulatedTime).count();
	}

	[[nodiscard]] inline uint64_t nsElapsed() const noexcept { return elapsed<std::chrono::nanoseconds>(); }
	[[nodiscard]] inline uint64_t usElapsed() const noexcept { return elapsed<std::chrono::microseconds>(); }
	[[nodiscard]] inline uint64_t msElapsed() const noexcept { return elapsed<std::chrono::milliseconds>(); }

	[[nodiscard]] inline bool paused() const noexcept { return _paused; }

private:
	using Clock = std::chrono::steady_clock;

	Clock::time_point _startTimeStamp {};
	std::chrono::nanoseconds _previouslyAccumulatedTime {0};
	bool _paused = true;
};
