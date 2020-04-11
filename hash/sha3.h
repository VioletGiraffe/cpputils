#pragma once

#include "compiler/compiler_warnings_control.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

	/* -------------------------------------------------------------------------
	 * Works when compiled for either 32-bit or 64-bit targets, optimized for
	 * 64 bit.
	 *
	 * Canonical implementation of Init/Update/Finalize for SHA-3 byte input.
	 *
	 * SHA3-256, SHA3-384, SHA-512 are implemented. SHA-224 can easily be added.
	 *
	 * Based on code from http://keccak.noekeon.org/ .
	 *
	 * I place the code that I wrote into public domain, free to use.
	 *
	 * I would appreciate if you give credits to this work if you used it to
	 * write or test * your code.
	 *
	 * Aug 2015. Andrey Jivsov. crypto@brainhub.org
	 * ---------------------------------------------------------------------- */

	 /* 'Words' here refers to uint64_t */
#define SHA3_KECCAK_SPONGE_WORDS \
	(((1600)/8/*bits to byte*/)/sizeof(uint64_t))
	typedef struct sha3_context_ {
		uint64_t saved;             /* the portion of the input message that we
									 * didn't consume yet */

STORE_COMPILER_WARNINGS
DISABLE_MSVC_WARNING(4201) // warning C4201: nonstandard extension used: nameless struct/union
		union {                     /* Keccak's state */
			uint64_t s[SHA3_KECCAK_SPONGE_WORDS];
			uint8_t sb[SHA3_KECCAK_SPONGE_WORDS * 8];
		};
RESTORE_COMPILER_WARNINGS

		unsigned byteIndex;         /* 0..7--the next byte after the set one
									 * (starts from 0; 0--none are buffered) */
		unsigned wordIndex;         /* 0..24--the next word to integrate input
									 * (starts from 0) */
		unsigned capacityWords;     /* the double size of the hash output in
									 * words (e.g. 16 for Keccak 512) */
	} sha3_context;


	/* For Init or Reset call these: */
	void sha3_Init256(void *priv);
	void sha3_Init384(void *priv);
	void sha3_Init512(void *priv);

	void sha3_Update(void *priv, void const *bufIn, size_t len);

	void const *sha3_Finalize(void *priv);

#ifdef __cplusplus
} // extern C
#endif
