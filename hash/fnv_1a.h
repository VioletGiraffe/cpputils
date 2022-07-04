#pragma once

#include <stdint.h>
#include <type_traits>

[[nodiscard]] inline constexpr uint64_t FNV_1a_64(const void* const data, const size_t length)
{
	uint64_t hash = 14695981039346656037ULL;
	for (const char* bytePtr = static_cast<const char*>(data), *end = bytePtr + length; bytePtr != end; ++bytePtr)
		hash = (hash ^ static_cast<uint64_t>(*bytePtr)) * 1099511628211ULL;

	return hash;
}

[[nodiscard]] inline constexpr uint32_t FNV_1a_32(const void* const data, const size_t length)
{
	uint32_t hash = 2166136261U;
	for (const char* bytePtr = static_cast<const char*>(data), *end = bytePtr + length; bytePtr != end; ++bytePtr)
		hash = (hash ^ static_cast<uint32_t>(*bytePtr)) * 16777619U;

	return hash;
}

class FNV_1a_64_hasher {
	static constexpr uint64_t fnv_offset_basis = 14695981039346656037ULL;
	static constexpr uint64_t fnv_prime = 1099511628211ULL;

public:
	template <typename T>
	inline constexpr void updateHash(const T& value) noexcept {
		static_assert(std::is_trivial_v<T>);
		for (const char* bytePtr = reinterpret_cast<const char*>(std::addressof(value)), *end = bytePtr + sizeof(value); bytePtr != end; ++bytePtr)
			_hash = (_hash ^ static_cast<uint64_t>(*bytePtr)) * fnv_prime;
	}

	inline constexpr void updateHash(const void* const data, const size_t length) noexcept {
		for (const char* bytePtr = static_cast<const char*>(data), *end = bytePtr + length; bytePtr != end; ++bytePtr)
			_hash = (_hash ^ static_cast<uint64_t>(*bytePtr)) * fnv_prime;
	}

	[[nodiscard]] inline constexpr uint64_t hash() const noexcept {
		return _hash;
	}

	inline constexpr void reset() noexcept {
		_hash = fnv_offset_basis;
	}

private:
	uint64_t _hash = fnv_offset_basis;
};

class FNV_1a_32_hasher {
	static constexpr uint32_t fnv_offset_basis = 2166136261U;
	static constexpr uint32_t fnv_prime = 16777619U;

public:
	template <typename T>
	inline constexpr uint32_t updateHash(const T& value) noexcept {
		static_assert(std::is_trivial_v<T>);
		for (const char* bytePtr = reinterpret_cast<const char*>(std::addressof(value)), *end = bytePtr + sizeof(value); bytePtr != end; ++bytePtr)
			_hash = (_hash ^ static_cast<uint32_t>(*bytePtr)) * fnv_prime;

		return _hash;
	}

	inline constexpr uint32_t updateHash(const void* const data, const size_t length) noexcept {
		for (const char* bytePtr = static_cast<const char*>(data), *end = bytePtr + length; bytePtr != end; ++bytePtr)
			_hash = (_hash ^ static_cast<uint32_t>(*bytePtr)) * fnv_prime;

		return _hash;
	}

	[[nodiscard]] inline constexpr  uint32_t calculatedHash() const noexcept {
		return _hash;
	}

	inline constexpr void reset() noexcept {
		_hash = fnv_offset_basis;
	}

private:
	uint32_t _hash = fnv_offset_basis;
};
