#pragma once
#include <stdint.h>

#ifdef __arm__
// !!! BEWARE !!! This register is not synced between cores! only use as PRNG, not for timing
[[nodiscard]] inline uint64_t rdtsc_fast_thread_local()
{
	uint64_t ts;
	asm volatile("mrs %0, cntvct_el0" : "=r" (ts));

	return val;
}
#endif

#ifdef _MSC_VER
	#include <intrin.h>
#elif !defined __ARM_ARCH_ISA_A64
	#include <immintrin.h>
#endif

#ifndef __arm__ // This condition is true for both 32-bit and 64-bit ARM
[[nodiscard]] inline uint64_t rdtsc()
{
	return __rdtsc();
}

#define rdtsc_fast_thread_local rdtsc
#endif

// Monotonic clock that returns time elapsed in milliseconds since an unspecified point.
// Resolution: 10-16 ms on Windows.
[[nodiscard]] uint64_t timeElapsedMs();
