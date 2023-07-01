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
	while (clock_gettime(CLOCK_MONOTONIC_RAW, &val) != 0) [[unlikely]]
		/* no body */;

	return tsToMs(ts);
#else
	struct timespec ts;
	if (::timespec_get(&ts, TIME_UTC) == 0) [[unlikely]]
		return 0;

	return tsToMs(ts);
#endif
}

#ifdef __ARM_ARCH_ISA_A64 // This condition is true for both 32-bit and 64-bit ARM
[[nodiscard]] uint64_t rdtsc()
{
	struct timespec val;
	while (clock_gettime(CLOCK_MONOTONIC_RAW, &val) != 0)
	   /* no body */;

	return (uint64_t)val.tv_sec * 1000000000ULL + (uint64_t)val.tv_nsec;
}
#endif
