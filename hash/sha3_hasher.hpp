#pragma once

#include "utility/memory_cast.hpp"

#include "sha3.h"

#include <assert.h>
#include <array>
#include <stdint.h>

template <size_t NBits>
class Sha3_Hasher {
public:
	inline Sha3_Hasher() noexcept {
		if constexpr (NBits == 256)
			sha3_Init256(&_c);
		else if constexpr (NBits == 384)
				sha3_Init384(&_c);
		else if constexpr (NBits == 256)
				sha3_Init512(&_c);
		else
			static_assert(NBits == 256, "The following SHA3 width is supported: 256, 384, 512 bits");
	}

	inline void update(const void * const bufIn, const size_t len) noexcept {
		assert(!_finalized);
		sha3_Update(&_c, bufIn, len);
	}

	template <typename T>
	inline void update(T&& value) noexcept {
		static_assert(std::is_trivial_v<std::remove_reference_t<T>>);
		this->update(std::addressof(value), sizeof(value));
	}

	inline std::array<uint8_t, NBits / 8> getHash() noexcept {
		assert(!_finalized);

		const void* const hashData = sha3_Finalize(&_c);
		std::array<uint8_t, NBits / 8> hashDataArray;
		::memcpy(hashDataArray.data(), hashData, hashDataArray.size());
		_finalized = true;
		return hashDataArray;
	}

	inline uint64_t get64BitHash() noexcept {
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
