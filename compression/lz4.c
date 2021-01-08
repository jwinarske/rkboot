/* SPDX-License-Identifier: CC0-1.0 */
#include "../include/defs.h"
#include "../include/log.h"
#include "compression.h"
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

#ifndef TINY
#define check(expr, ...) if (unlikely(!(expr))) {info(__VA_ARGS__);return 0;}
#else
#define check(expr, ...) if (unlikely(!(expr))) {fputs("decompression failed\n", stderr);return 0;}
#endif

enum {
	FBINDEP = 32,
	FBCHECKSUM = 16,
	FCSIZE = 8,
	FCCHECKSUM = 4,
	FRESERVED = 2,
	FDICTID = 1,
};

static u8 *decompress_block(const u8 *in, const u8 *end, u8 *out, u8 *out_end, u8 *window_start) {
	assert(out_end - out <= ((u32)1 << 31)); /* don't want to have to check match length for overflow */
	check(in < end, "need at least one token byte\n");
	if (end - in < 6) {
		u8 token = *in++;
		check((end - in) * 16 == token, "underlength block has wrong token value\n");
		token >>= 4;
		check(out_end - out >= token, "not enough space to output underlength block\n");
		while (token--) {*out++ = *in++;}
		return out;
	}
	const u8 *max_final_block_start = end - 6;
	while (1) {
		assert(in <= max_final_block_start);
		assert(out_end - out >= 5);

		u8 token = *in++;
		u32 copy = token >> 4;
		u32 length = token & 15;
		debug("token copy%"PRIu32" len%"PRIu32"\n", copy, length);
		if (copy == 15) {
			u8 add;
			do {
				copy += add = *in++;	/* can read this before checking for input size because we checked for 6 bytes before */
				check(end - in >= copy, "not enough data for literals");	/* this also covers the next copy byte if necessary, since copy >= 15 */
			} while (add == 255);
		}
		check(out_end - out >= copy, "not enough space for literal copy\n");
		if (end - in < copy + 8) {
			check(end - in == copy, "final sequence is too short");
			check(length == 0, "garbage match length value in final token\n");
			lzcommon_literal_copy(out, in, copy);
			return out + copy;
		}
		debug("copy %"PRIu32" %.*s\n", copy, (int)copy, in);
		lzcommon_literal_copy(out, in, copy);
		in += copy; out += copy;
		u16 dist = in[0] | (u16)in[1] << 8;
		in += 2;
		check(dist != 0, "distance of 0 used\n");
		debug("dist %"PRIu16" ", dist);
		check(dist <= out - window_start, "match offset is too high\n");
		length += 4;
		check(out_end - out >= length + 5, "not enough space to perform match copy\n");	/* last 5 output bytes must be literals */
		if (length == 19) {
			u8 add;
			u32 restsize = out_end - out - 5;
			do {
				length += add = *in++;
				check(restsize >= length, "not enough space to perform match copy\n");
				check(in <= max_final_block_start, "not enough data for final sequence\n");
			} while (add == 255);
		} else {
			check(in <= max_final_block_start, "not enough data for final sequence\n");
		}
		debug("length %"PRIu32"\n", length);
		lzcommon_match_copy(out, dist, length);
		out += length;
		
		if (out - window_start <= 100) {
			debug("%.*s\n", (int)(out - window_start), window_start);
		} else {
			debug("%.100s\n", out - 100);
		}
	}
}

static enum compr_probe_status probe(const u8 *in, const u8 *end, size_t *size) {
	if (end - in < 4) {return COMPR_PROBE_NOT_ENOUGH_DATA;}
	if (in[0] != 4 || in[1] != 34 || in[2] != 77 || in[3] != 24) {
		return COMPR_PROBE_WRONG_MAGIC;
	}
	if (end - in < 7) {return COMPR_PROBE_NOT_ENOUGH_DATA;}
	u8 flags = in[4], bd = in[5];
	in += 6;
	u8 version = flags >> 6;
	if (version != 1) {
		info("version %"PRIu8" not implemented\n", version);
		return COMPR_PROBE_RESERVED_FEATURE;
	}
	if (flags & FRESERVED) {
		info("reserved feature used in flags\n");
		return COMPR_PROBE_RESERVED_FEATURE;
	}
	if (flags & FDICTID) {
		info("dictionaries not implemented\n");
		return COMPR_PROBE_UNIMPLEMENTED_FEATURE;
	}
	if (bd & 0x8f) {
		info("reserved feature used in BD byte\n");
		return COMPR_PROBE_RESERVED_FEATURE;
	}
	u8 block_size_field = bd >> 4 & 7;
	if (block_size_field < 4) {
		info("reserved block size value %"PRIu8" used\n", block_size_field);
		return COMPR_PROBE_RESERVED_FEATURE;
	}
	if (flags & FCSIZE) {
		if (end - in < 9) {return COMPR_PROBE_NOT_ENOUGH_DATA;}
		u64 csize = in[0] | (u64)in[1] << 8 | (u64)in[2] << 16 | (u64)in[3] << 24 | (u64)in[4] << 32 | (u64)in[5] << 40 | (u64)in[6] << 48 | (u64)in[7] << 56;
		in += 8;
		if (csize <= SIZE_MAX) {
			*size = csize;
			return COMPR_PROBE_SIZE_KNOWN;
		} else {
			return COMPR_PROBE_SIZE_UNKNOWN;
		}
	}
	return COMPR_PROBE_SIZE_UNKNOWN;
}

struct lz4_dec_state {
	struct decompressor_state st;
	u32 max_block_size;
	u8 flags;
};

static size_t decode_trailer(struct decompressor_state *state,  const u8 *in,  const u8 *end) {
	struct lz4_dec_state *st = (struct lz4_dec_state *)state;
	st->st.decode = 0;
	if (st->flags & FCCHECKSUM) {
		check(end - in >= 4, "not enough data for content checksum\n");
		/* FIXME: implement */
		return NUM_DECODE_STATUS + 4;
	}
	return NUM_DECODE_STATUS;
}

static size_t decode_block(struct decompressor_state *state,  const u8 *in,  const u8 *end) {
	struct lz4_dec_state *st = (struct lz4_dec_state *)state;
	check(end - in >= 4, "not enough data for block size field\n");
	u32 block_size = in[0] | (u32)in[1] << 8 | (u32)in[2] << 16 | (u32)in[3] << 24;
	in += 4;
	u32 actual_block_size = block_size & 0x7fffffff;
	if (actual_block_size == 0) {
		info("ending\n");
		st->st.decode = decode_trailer;
		return NUM_DECODE_STATUS + 4;
	}
	debug("block size %"PRIu32" (0x%"PRIx32")\n", block_size, actual_block_size);
	check(actual_block_size <= st->max_block_size, "block size of %"PRIu32" too large\n", actual_block_size);
	if (unlikely(end - in < actual_block_size)) {return DECODE_NEED_MORE_DATA;}
	size_t total_size = actual_block_size + 4;
	if (st->flags & FBCHECKSUM) {
		/* FIXME */
		total_size += 4;
	}
	u8 **out = &state->out, *out_end = state->out_end, *window_start = state->window_start;
	if (unlikely(out_end - *out < actual_block_size)) {return DECODE_NEED_MORE_SPACE;}
	if (block_size & 1 << 31) {
		lzcommon_literal_copy(*out, in, actual_block_size);
		*out += actual_block_size;
	} else {
		u8 *out_limit = out_end - *out < st->max_block_size ? out_end : *out + st->max_block_size;
		u8 *block_end = decompress_block(in, in + block_size, *out, out_limit, window_start);
		if (!block_end) {return DECODE_ERR;}
		*out = block_end;
	}
	if (st->flags & FBINDEP) {state->window_start = *out;}
	return NUM_DECODE_STATUS + total_size;
}

static const u8 *init(struct decompressor_state *state, const u8 *in, const u8 *end) {
	assert(end - in >= 7);
	struct lz4_dec_state *st = (struct lz4_dec_state *)state;
	st->flags = in[4];
	u8 bd = in[5];
	info("decompressing LZ4, flags=0x%"PRIx8", bd=%"PRIx8"\n", st->flags, bd);
	u8 block_size_field = bd >> 4 & 7;
	st->max_block_size = 256 << (2*block_size_field);
	/* FIXME: implement header checksum */
	st->st.decode = decode_block;
	if (st->flags & FCSIZE) {in += 8;}
	return in + 7;
}

const struct decompressor lz4_decompressor = {
	.probe = probe,
	.state_size = sizeof(struct decompressor_state),
	.init = init,
};
