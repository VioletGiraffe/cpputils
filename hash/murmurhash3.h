//-----------------------------------------------------------------------------
// MurmurHash3 was written by Austin Appleby, and is placed in the public
// domain. The author hereby disclaims copyright to this source code.

#pragma once

#include <array>
#include <stdint.h>

//-----------------------------------------------------------------------------

void MurmurHash3_x64_128(const void * key, int len, uint32_t seed, void * out);

uint64_t MurmurHash3_x64_64(const void * key, int len);
std::array<uint8_t, 8> MurmurHash3_x64_128(const void * key, int len);

//-----------------------------------------------------------------------------
