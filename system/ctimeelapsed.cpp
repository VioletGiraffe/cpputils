#include "ctimeelapsed.h"
#include "assert/advanced_assert.h"

CTimeElapsed::CTimeElapsed(bool autoStart) noexcept
{
	if (autoStart)
		start();
}

void CTimeElapsed::start() noexcept
{
	_previouslyAccumulatedTime = std::chrono::nanoseconds{ 0 };
	_startTimeStamp = std::chrono::high_resolution_clock::now();
	_paused = false;
}

void CTimeElapsed::pause() noexcept
{
	assert_r(!_paused);
	_previouslyAccumulatedTime += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - _startTimeStamp);
	_paused = true;
}

void CTimeElapsed::resume() noexcept
{
	assert_r(_paused);
	_paused = false;
	_startTimeStamp = std::chrono::high_resolution_clock::now();
}

bool CTimeElapsed::paused() const noexcept
{
	return _paused;
}
