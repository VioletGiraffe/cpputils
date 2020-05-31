#pragma once

#include <string>
#include <stdint.h>

constexpr uint32_t jenkins_hash(std::string_view s) noexcept
{
	uint32_t hash = 0;
	for (uint32_t i = 0; i < s.size(); ++i)
	{
		hash += s[i];
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}

	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);
	return hash;
}
