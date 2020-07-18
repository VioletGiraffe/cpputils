#pragma once

#include "utility/memory_cast.hpp"

#include "sha3.h"

#include <assert.h>
#include <array>
#include <stdint.h>

template <size_t NBits>
class Sha3_Hasher {
public:
	Sha3_Hasher() noexcept
	{
		if constexpr (NBits == 256)
			sha3_Init256(&_c);
		else if constexpr (NBits == 384)
			sha3_Init384(&_c);
		else if constexpr (NBits == 512)
			sha3_Init512(&_c);
		else
			static_assert(NBits == 256, "The following SHA3 width is supported: 256, 384, 512 bits");
	}

	void update(const void * const bufIn, const size_t len) noexcept
	{
		assert(!_finalized);
		if (len > 0) // it's unclear whether sha3_Update support 0 length. In fact, it seems to underflow it and enter an [almost] infinite loop.
			sha3_Update(&_c, bufIn, len);
		else
			assert(len > 0);
	}

	void update(const std::string& str) noexcept
	{
		this->update(str.data(), str.size());
	}

	template <typename T, typename = std::enable_if_t<is_trivially_serializable_v<std::remove_reference_t<T>>>>
	void update(T&& value) noexcept
	{
		static_assert(is_trivially_serializable_v<std::remove_reference_t<T>>);
		this->update(std::addressof(value), sizeof(value));
	}

	std::array<uint8_t, NBits / 8> getHash() noexcept
	{
		assert(!_finalized);

		const void* const hashData = sha3_Finalize(&_c);
		std::array<uint8_t, NBits / 8> hashDataArray;
		::memcpy(hashDataArray.data(), hashData, hashDataArray.size());
		_finalized = true;
		return hashDataArray;
	}

	uint64_t get64BitHash() noexcept
	{
		const auto fullHash = getHash();

		uint64_t hash64 = 0;
		for (size_t offset = 0; offset < fullHash.size(); offset += sizeof(uint64_t))
		{
			const auto eightBytes = memory_cast<uint64_t>(fullHash.data() + offset);
			hash64 ^= eightBytes;
		}

		return hash64;
	}

private:
	sha3_context _c;
	bool _finalized = false;
};

inline uint64_t sha3_64bit(const void * const bufIn, const size_t len) noexcept
{
	Sha3_Hasher<256> hasher;
	hasher.update(bufIn, len);
	return hasher.get64BitHash();
}
