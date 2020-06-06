/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <stdint.h>

#if !CONFIG_NO_UNALIGNED
#if __aarch64__ && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define LDLE64U(ptr, bits) __asm__("ldr %0, [%1]" : "=r"(bits) : "r"(ptr))
#endif
#endif

static inline uint64_t ldle64a(const uint64_t *ptr) {
	uint64_t val = *ptr;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	/* no swapping needed */ 
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	val = __builtin_bswap64(val);
#else
#error "__BYTE_ORDER__ not properly defined"
#endif
	return val;
}

static inline uint32_t ldle32a(const uint32_t *ptr) {
	uint32_t val = *ptr;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	/* no swapping needed */ 
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	val = __builtin_bswap32(val);
#else
#error "__BYTE_ORDER__ not properly defined"
#endif
	return val;
}
