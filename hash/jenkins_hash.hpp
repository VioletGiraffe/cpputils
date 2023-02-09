#pragma once

#include <string>
#include <stdint.h>

[[nodiscard]] consteval uint32_t jenkins_hash(std::string_view s, uint32_t seed = 0) noexcept
{
	uint32_t hash = seed;
	for (char c: s)
	{
		hash += static_cast<uint8_t>(c);
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}

	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);
	return hash;
}
