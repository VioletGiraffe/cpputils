//-----------------------------------------------------------------------------
// MurmurHash3 was written by Austin Appleby, and is placed in the public
// domain. The author hereby disclaims copyright to this source code.

// Note - The x86 and x64 versions do _not_ produce the same results, as the
// algorithms are optimized for their respective platforms. You can still
// compile and run any of them on any platform, but your performance with the
// non-native version will be less than optimal.

#include "murmurhash3.h"

#include <string.h> // memcpy

//-----------------------------------------------------------------------------
// Platform-specific functions and macros

// Microsoft Visual Studio

#if defined(_MSC_VER)

#define FORCE_INLINE	__forceinline

#include <stdlib.h>

#define ROTL32(x,y) _rotl(x,y)
#define ROTL64(x,y) _rotl64(x,y)

#define BIG_CONSTANT(x) (x)

// Other compilers

#else	// defined(_MSC_VER)

#define	FORCE_INLINE inline __attribute__((always_inline))

inline uint32_t rotl32(uint32_t x, int8_t r)
{
	return (x << r) | (x >> (32 - r));
}

inline uint64_t rotl64(uint64_t x, int8_t r)
{
	return (x << r) | (x >> (64 - r));
}

#define	ROTL32(x,y) rotl32(x,y)
#define ROTL64(x,y) rotl64(x,y)

#define BIG_CONSTANT(x) (x##ULL)

#endif // !defined(_MSC_VER)

//-----------------------------------------------------------------------------
// Block read - if your platform needs to do endian-swapping or can only
// handle aligned reads, do the conversion here

FORCE_INLINE uint32_t getblock32(const void* p, int i)
{
	uint32_t block;
	::memcpy(&block, reinterpret_cast<const char*>(p) + i * sizeof(block), sizeof(block));
	return block;
}

FORCE_INLINE uint64_t getblock64(const void* p, int i)
{
	uint64_t block;
	::memcpy(&block, reinterpret_cast<const char*>(p) + i * sizeof(block), sizeof(block));
	return block;
}

//-----------------------------------------------------------------------------
// Finalization mix - force all bits of a hash block to avalanche

FORCE_INLINE uint32_t fmix32(uint32_t h)
{
	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;

	return h;
}

FORCE_INLINE uint64_t fmix64(uint64_t k)
{
	k ^= k >> 33;
	k *= BIG_CONSTANT(0xff51afd7ed558ccd);
	k ^= k >> 33;
	k *= BIG_CONSTANT(0xc4ceb9fe1a85ec53);
	k ^= k >> 33;

	return k;
}

//-----------------------------------------------------------------------------

void MurmurHash3_x86_32(const void* key, int len, uint32_t seed, void* out)
{
	const unsigned char* data = (const unsigned char*)key;
	const int nblocks = len / 4;

	uint32_t h1 = seed;

	const uint32_t c1 = 0xcc9e2d51;
	const uint32_t c2 = 0x1b873593;

	//----------
	// body

	const auto* blocks = data + nblocks * 4;

	for (int i = -nblocks; i; i++)
	{
		uint32_t k1 = getblock32(blocks, i);

		k1 *= c1;
		k1 = ROTL32(k1, 15);
		k1 *= c2;

		h1 ^= k1;
		h1 = ROTL32(h1, 13);
		h1 = h1 * 5 + 0xe6546b64;
	}

	//----------
	// tail

	const unsigned char* tail = (const unsigned char*)(data + nblocks * 4);

	uint32_t k1 = 0;

	switch (len & 3)
	{
	case 3: k1 ^= getblock32(tail, 2) << 16;
	case 2: k1 ^= getblock32(tail, 1) << 8;
	case 1: k1 ^= getblock32(tail, 0);
		k1 *= c1; k1 = ROTL32(k1, 15); k1 *= c2; h1 ^= k1;
	};

	//----------
	// finalization

	h1 ^= len;

	h1 = fmix32(h1);

	::memcpy(out, &h1, sizeof(h1));
}

void MurmurHash3_x86_32(const void* key, int len, void* out)
{
	return MurmurHash3_x86_32(key, len, 3829789199U, out);
}


void MurmurHash3_x64_128(const void* key, const int len,
	const uint32_t seed, void* out)
{
	const unsigned char* data = (const unsigned char*)key;
	const int nblocks = len / 16;

	uint64_t h1 = seed;
	uint64_t h2 = seed;

	const uint64_t c1 = BIG_CONSTANT(0x87c37b91114253d5);
	const uint64_t c2 = BIG_CONSTANT(0x4cf5ad432745937f);

	//----------
	// body

	const auto* blocks = data;

	for (int i = 0; i < nblocks; i++)
	{
		uint64_t k1 = getblock64(blocks, i * 2 + 0);
		uint64_t k2 = getblock64(blocks, i * 2 + 1);

		k1 *= c1; k1 = ROTL64(k1, 31); k1 *= c2; h1 ^= k1;

		h1 = ROTL64(h1, 27); h1 += h2; h1 = h1 * 5 + 0x52dce729;

		k2 *= c2; k2 = ROTL64(k2, 33); k2 *= c1; h2 ^= k2;

		h2 = ROTL64(h2, 31); h2 += h1; h2 = h2 * 5 + 0x38495ab5;
	}

	//----------
	// tail

	const unsigned char* tail = data + nblocks * 16;

	uint64_t k1 = 0;
	uint64_t k2 = 0;

	switch (len & 15)
	{
	case 15: k2 ^= getblock64(tail, 14) << 48;
	case 14: k2 ^= getblock64(tail, 13) << 40;
	case 13: k2 ^= getblock64(tail, 12) << 32;
	case 12: k2 ^= getblock64(tail, 11) << 24;
	case 11: k2 ^= getblock64(tail, 10) << 16;
	case 10: k2 ^= getblock64(tail, 9) << 8;
	case  9: k2 ^= getblock64(tail, 8);
		k2 *= c2; k2 = ROTL64(k2, 33); k2 *= c1; h2 ^= k2;

	case  8: k1 ^= getblock64(tail, 7) << 56;
	case  7: k1 ^= getblock64(tail, 6) << 48;
	case  6: k1 ^= getblock64(tail, 5) << 40;
	case  5: k1 ^= getblock64(tail, 4) << 32;
	case  4: k1 ^= getblock64(tail, 3) << 24;
	case  3: k1 ^= getblock64(tail, 2) << 16;
	case  2: k1 ^= getblock64(tail, 1) << 8;
	case  1: k1 ^= getblock64(tail, 0);
		k1 *= c1; k1 = ROTL64(k1, 31); k1 *= c2; h1 ^= k1;
	};

	//----------
	// finalization

	h1 ^= len; h2 ^= len;

	h1 += h2;
	h2 += h1;

	h1 = fmix64(h1);
	h2 = fmix64(h2);

	h1 += h2;
	h2 += h1;

	char* outBytePtr = reinterpret_cast<char*>(out);
	::memcpy(outBytePtr, &h1, sizeof(h1));
	::memcpy(outBytePtr + sizeof(uint64_t), &h2, sizeof(h2));
}

std::array<uint8_t, 16> MurmurHash3_x64_128(const void* key, int len)
{
	std::array<uint8_t, 16> hash;
	MurmurHash3_x64_128(key, len, 104729, hash.data());

	return hash;
}

uint64_t MurmurHash3_x64_64(const void* key, int len)
{
	uint64_t hash[2];
	MurmurHash3_x64_128(key, len, 104729, hash);

	return hash[0] ^ hash[1];
}
