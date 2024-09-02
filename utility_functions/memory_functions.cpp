#include "memory_functions.h"

#include <string.h>
#include <cstddef>

const void* memmem(const void* haystack, const size_t haystackSize, const void* needle, const size_t needleSize) noexcept
{
	if (needleSize == 0 || haystackSize == 0 || needleSize > haystackSize) [[unlikely]]
		return nullptr;

	const size_t lastPossibleStartingLocation = haystackSize - needleSize;
	auto* bHaystack = reinterpret_cast<const std::byte*>(haystack), *bNeedle = reinterpret_cast<const std::byte*>(needle);

	for (const auto* match = bHaystack, *end = bHaystack + lastPossibleStartingLocation; match != end; )
	{
		const size_t lengthLeft = lastPossibleStartingLocation - static_cast<size_t>(match - bHaystack) + 1;
		match = reinterpret_cast<const std::byte*>(::memchr(match, (char)bNeedle[0], lengthLeft));
		if (!match)
			return nullptr;

		if (needleSize == 1) [[unlikely]]
			return match;

		if (::memcmp(match + 1, bNeedle + 1, needleSize - 1) == 0)
			return match;

		++match;
	}

	return nullptr;
}
