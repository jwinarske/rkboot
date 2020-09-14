/* SPDX-License-Identifier: CC0-1.0 */
#include "compression.h"
#include <assert.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>

#define debug(...) /*fprintf(stderr, __VA_ARGS__)*/

/* assumes that length >= 8.
always copies front-to-back with aligned 8-byte accesses, which makes it a suitable lzcommon_match_copy for distance >= 8 */
static void copy(u8 *dest, const u8 *src, u32 length) {
	const u64 *end = (const u64*)(((u64)src + length + 7) & ~(u64)7);
	u64 *dest_end = (u64*)(((u64)dest + length + 7) & ~(u64)7);
	u8 src_align_off = (~(u64)src + 1) & 7, dest_align_off = (~(u64)dest + 1) & 7;
	debug("\nsrc%"PRIu8" dest%"PRIu8"\n", src_align_off, dest_align_off);
	if (src_align_off == dest_align_off) {
		if (src_align_off & 1) {
			*dest++ = *src++;
		}
		if (src_align_off & 2) {
			*(u16*)dest = *(u16*)src;
			src += 2; dest += 2;
		}
		if (src_align_off & 4) {
			*(u32*)dest = *(u32*)src;
			src += 4; dest += 4;
		}
		const u64 *src64 = (const u64*)src;
		u64 *dest64 = (u64 *)dest;
		while (src64 < end) {*dest64++ = *src64++;}
	} else {
		u8 shift = 0;
		u64 bits = 0;
		if (src_align_off & 1) {
			bits |= *src++;
			shift += 8;
		}
		if (src_align_off & 2) {
			bits |= (u64)*(u16 *)src << shift;
			src += 2; shift += 16;
		}
		if (src_align_off & 4) {
			bits |= (u64)*(u32 *)src << shift;
			src += 4; shift += 32;
		}
		assert(((u64)src & 7) == 0);
		const u64 *src64 = (const u64 *)src;
		if (src_align_off < dest_align_off) {
			bits |= *src64 << shift;
		}
		debug("0x%"PRIx64"/%"PRIu8"\n", bits, shift);
		if (dest_align_off & 1) {
			*dest++ = (u8)bits;
			bits >>= 8; shift -= 8;
		}
		if (dest_align_off & 2) {
			*(u16 *)dest = (u16)bits;
			dest += 2;
			bits >>= 16; shift -= 16;
		}
		if (dest_align_off & 4) {
			*(u32 *)dest = (u32)bits;
			dest += 4;
			bits >>= 32; shift -= 32;
		}
		assert(((u64)dest & 7) == 0);
		u64 *dest64 = (u64 *)dest;
		shift &= 0x38;
		assert(shift != 0);
		if (src_align_off < dest_align_off) {
			bits = *src64++ >> (64 - shift);
			debug("0x%"PRIx64"/%"PRIu8"\n", bits, shift);
		}
		while (src64 < end) {
			u64 x = *src64++;
			u64 val = x << shift | bits;
			debug("loop 0x%"PRIx64"/%"PRIu8", write %016"PRIx64"\n", bits, shift, val);
			*dest64++ = val;
			bits = x >> (64 - shift);
		}
		if (end - src64 < dest_end - dest64) {
			/* write only if at least one byte is not garbage */
			debug("end 0x%"PRIx64"/%"PRIu8"\n", bits, shift);
			*dest64 = bits;
		}
	}
	/*u8 shift = 0;
	u64 bits = 0;
#define ALIGN_SOURCE(n, type) if ((u64)src & (n)) {\
		bits = (u64)*(type *)src << shift;\
		src += (n); shift += 8 * (n);\
	}
	ALIGN_SOURCE(1, u8)
	ALIGN_SOURCE(2, u16)
	ALIGN_SOURCE(4, u32)
	const u64 *src64 = (u64*)src;
#define ALIGN_DEST(n, type) if ((u64)dest & (n)) {\
		if (shift >= 8 * (n)) {\
			*(type *)dest = (type)bits;\
			dest += (n);\
			bits >>= 8 * (n); shift -= 8 * (n);\
		} else if (length >= (n)) {\
			u64 x = *src64++;\
			*dest++ = (type)x;\
			bits = x >> (n); shift = 64 - 8 * (n);\
		}\
	}
	ALIGN_DEST(1, u8)
	ALIGN_DEST(2, u16)
	ALIGN_DEST(4, u32)
	u64 *dest64 = (u64 *)dest;
	if (shift) {
		while (src64 < end) {
			u64 x = *src64++;
			*dest64++ = bits | x << shift;
			bits = x >> (64 - shift);
		}
		*dest64++ = bits;
	} else {
		while (src64 < end) {
			*dest64++ = *src64++;
		}
	}*/
}

/* this function does not assume that dest and src can be subtracted, but it must behave safely (correctly copy `length` bytes, while possibly writing garbage until the next LZCOMMON_BLOCK boundary) as long as `dest` is not within `src` and `src+length` (used in zstd decompression) */
void lzcommon_literal_copy(u8 *dest, const u8 *src, u32 length) {
	if (length < LZCOMMON_BLOCK) {
		length = LZCOMMON_BLOCK;
		while (length--) {*dest++ = *src++;}
	} else {
		copy(dest, src, length);
	}
}

void lzcommon_match_copy(u8 *dest, u32 dist, u32 length) {
	const u8 *src = dest - dist;
	if (dist >= length) {
		lzcommon_literal_copy(dest, src, length);
		return;
	} else if (dist == 1) {
		memset(dest, *src, length);
	} else {
		while (length--) {*dest++ = *src++;}
	}
}
