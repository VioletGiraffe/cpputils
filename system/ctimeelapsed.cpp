#include "ctimeelapsed.h"
#include "assert/advanced_assert.h"

CTimeElapsed::CTimeElapsed(bool autoStart) noexcept
{
	if (autoStart)
		start();
}

void CTimeElapsed::start() noexcept
{
	_pausedFor = std::chrono::nanoseconds(0);
	_startTimeStamp = std::chrono::high_resolution_clock::now();
	_paused = false;
}

void CTimeElapsed::pause() noexcept
{
	assert_r(!_paused);
	_pauseTimeStamp = std::chrono::high_resolution_clock::now();
	_paused = true;
}

void CTimeElapsed::resume() noexcept
{
	assert_r(_paused);
	_paused = false;
	_pausedFor += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - _pauseTimeStamp);
}

bool CTimeElapsed::paused() const noexcept
{
	return _paused;
}
