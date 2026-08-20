#include "memory_functions.h"

#include <string.h>
#include <cstddef>

const void* memfind(const void* haystack, const size_t haystackSize, const void* needle, const size_t needleSize) noexcept
{
	if (needleSize == 0 || haystackSize == 0 || needleSize > haystackSize) [[unlikely]]
		return nullptr;

	auto* bHaystack = reinterpret_cast<const std::byte*>(haystack), *bNeedle = reinterpret_cast<const std::byte*>(needle);
	// One past the last offset the needle still fits at, so the same value bounds every first-byte scan below.
	const auto* const searchEnd = bHaystack + (haystackSize - needleSize) + 1;

	for (const auto* match = bHaystack; match < searchEnd; ++match)
	{
		match = reinterpret_cast<const std::byte*>(::memchr(match, (char)bNeedle[0], static_cast<size_t>(searchEnd - match)));
		if (!match)
			return nullptr;

		if (needleSize == 1) [[unlikely]]
			return match;

		if (::memcmp(match + 1, bNeedle + 1, needleSize - 1) == 0)
			return match;
	}

	return nullptr;
}
