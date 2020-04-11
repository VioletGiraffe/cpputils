#pragma once

#include <stdint.h>
#include <type_traits>

inline uint64_t FNV_1a_64(const void* const data, const size_t length)
{
	uint64_t hash = 14695981039346656037ULL;
	for (const char* bytePtr = static_cast<const char*>(data), *end = bytePtr + length; bytePtr != end; ++bytePtr)
		hash = (hash ^ static_cast<uint64_t>(*bytePtr)) * 1099511628211ULL;

	return hash;
}

class FNV_1a_64_hasher {
public:
	template <typename T>
	void updateHash(const T& value) noexcept {
		static_assert(std::is_trivial_v<T>);
		for (const char* bytePtr = reinterpret_cast<const char*>(std::addressof(value)), *end = bytePtr + sizeof(value); bytePtr != end; ++bytePtr)
			_hash = (_hash ^ static_cast<uint64_t>(*bytePtr)) * 1099511628211ULL;
	}

	inline uint64_t hash() const noexcept {
		return _hash;
	}

private:
	uint64_t _hash = 14695981039346656037ULL;
};
