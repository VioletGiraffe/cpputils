#pragma once

#include <stdint.h>

inline uint64_t FNV_1a_64(const void* const data, const size_t length)
{
	uint64_t hash = 14695981039346656037ULL;
	for (const char* bytePtr = static_cast<const char*>(data), *end = bytePtr + length; bytePtr != end; ++bytePtr)
		hash = (hash ^ static_cast<uint64_t>(*bytePtr)) * 1099511628211ULL;

	return hash;
}
