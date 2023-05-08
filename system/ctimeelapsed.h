#pragma once

#include <chrono>

class CTimeElapsed
{
public:
	explicit CTimeElapsed(bool autoStart = false) noexcept;
	constexpr ~CTimeElapsed() noexcept = default;

	CTimeElapsed(const CTimeElapsed&) = delete;
	CTimeElapsed& operator=(const CTimeElapsed&) = delete;

	void start() noexcept;
	void pause() noexcept;
	void resume() noexcept;
	// Returns the time since the last start() call, minus however long the pause(s) had lasted, in the specified std::chrono duration units
	template <typename StdChronoDurationUnit = std::chrono::milliseconds>
	[[nodiscard]] uint64_t elapsed() const noexcept {
		if (!_paused)
			return (std::chrono::duration_cast<StdChronoDurationUnit>((std::chrono::high_resolution_clock::now() - _startTimeStamp) + _pausedFor)).count();
		else
			return (std::chrono::duration_cast<StdChronoDurationUnit>(_pauseTimeStamp - _startTimeStamp)).count();
	}

	[[nodiscard]] bool paused() const noexcept;

private:
	std::chrono::high_resolution_clock::time_point _startTimeStamp;
	std::chrono::nanoseconds _pausedFor {0};
	std::chrono::high_resolution_clock::time_point _pauseTimeStamp;
	bool _paused = false;
};
