/* SPDX-License-Identifier: CC0-1.0 */
#include "zstd_internal.h"
#include <assert.h>
#include <inttypes.h>
#include "../include/log.h"

enum {
	Raw_Literals_Block = 0,
	RLE_Literals_Block,
	Compressed_Literals_Block,
	Treeless_Literals_Block
};

struct literals_probe zstd_probe_literals(const u8 *in, const u8 *end) {
	struct literals_probe res;
	res.flags = ZSTD_TRUNCATED;
	u8 lsh0 = *in++;
	u8 literals_block_type = lsh0 & 3;
	debug("lsh0=%"PRIx8"\n", lsh0);
	if (literals_block_type >= 2) { /* compressed literals */
		u32 flags = literals_block_type == Treeless_Literals_Block ? ZSTD_TREELESS : 0;
		u8 size_format = lsh0 >> 2 & 3;
		if (end - in < 2) {return res;}
		u32 regen_size = lsh0 >> 4 | (u32)*in++ << 4;
		u32 comp_size;
		if (size_format < 2) {
			if (size_format == 0) {flags |= ZSTD_1STREAM;}
			comp_size = regen_size >> 10 | (u32)*in++ << 2;
			regen_size &= 0x3ff;
		} else {
			if (end - in < size_format) {return res;}
			if (size_format == 2) {
				regen_size |= (u32)in[0] << 12 & 0x3000;
				comp_size = in[0] >> 2 | (u32)in[1] << 6;
			} else {assert(size_format == 3);
				regen_size |= (u32)in[0] << 12 & 0x3f000;
				comp_size = in[0] >> 6 | (u32)in[1] << 2 | (u32)in[2] << 10;
			}
			in += size_format;
		}
		if (end - in < comp_size) {return res;}
		res.start = in;
		res.end = in + comp_size;
		res.size = regen_size;
		res.flags = flags;
		return res;
	} else {
		if (~lsh0 & 4) {
			res.size = lsh0 >> 3;
		} else if (~lsh0 & 8) {
			if (end - in < 1) {return res;}
			res.size = lsh0 >> 4 | (u32)*in++ << 4;
		} else {
			if (end - in < 2) {return res;}
			res.size = lsh0 >> 4 | (u32)*in << 4 | (u32)in[1] << 12;
			in += 2;
		}
		res.start = in;
		if (lsh0 & 1) { /* RLE literals */
			if (end - in < 1) {return res;}
			res.flags = ZSTD_RLE_LITERALS;
			res.end = in + 1;
			return res;
		} else { /* raw literals */
			if (end - in < res.size) {return res;}
			res.flags = ZSTD_RAW_LITERALS;
			res.end = in + res.size;
			return res;
		}
	}
}
