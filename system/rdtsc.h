#pragma once
#include <stdint.h>

#ifdef __arm__
inline uint64_t rdtsc()
{
	uint64_t ts;
	asm volatile("mrs %0, cntvct_el0" : "=r" (ts));

	return val;
}
#else

#ifdef __MSC_VER
	#include <intrin.h>
#else
	#include <immintrin.h>
#endif

__forceinline uint64_t rdtsc()
{
	return __rdtsc();
}
#endif
