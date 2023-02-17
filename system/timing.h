#pragma once
#include <stdint.h>

#ifdef __arm__
// !!! BEWARE !!! This register is not synced between cores! only use as PRNG, not for timing
[[nodiscard]] inline uint64_t rdtsc()
{
	uint64_t ts;
	asm volatile("mrs %0, cntvct_el0" : "=r" (ts));

	return val;
}
#else

#ifdef _MSC_VER
	#include <intrin.h>
#else
	#include <immintrin.h>
#endif

[[nodiscard]] inline uint64_t rdtsc()
{
	return __rdtsc();
}
#endif

// Monotonic clock that returns time elapsed in milliseconds since an unspecified point.
// Resolution: 10-16 ms on Windows.
[[nodiscard]] uint64_t timeElapsedMs();
