#include "timing.h"

#ifdef _WIN32
#include <Windows.h>
#else
#include <time.h>

inline constexpr uint64_t tsToMs(struct timespec& ts)
{
	return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1'000'000;
}

#endif

uint64_t timeElapsedMs()
{
#ifdef _WIN32
	return ::GetTickCount64();
#elif defined __linux__
	struct timespec ts;
	if (::clock_gettime(CLOCK_MONOTONIC_RAW , &ts) != 0) [[unlikely]]
		return 0;

	return tsToMs(ts);
#else
	struct timespec ts;
	if (::timespec_get(&ts, TIME_UTC) == 0) [[unlikely]]
		return 0;

	return tsToMs(ts);
#endif
}
