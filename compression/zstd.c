/* SPDX-License-Identifier: CC0-1.0 */
#include "zstd_internal.h"
#include <inttypes.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "compression.h"
#include "arch_mem_access.h"

/*
short decoding table format:

- 6 bit value
- 3 bit length
- 7 bits base

long decoding table format:

- 6 bit value
- 5 bit length
- 21 bit base
*/

enum {
	Single_Segment_Flag = 0x20,
	Frame_Header_Descriptor_RESERVED = 8,
	Content_Checksum_Flag = 4,
};

enum {
	Raw_Block = 0,
	RLE_Block,
	Compressed_Block
};

enum {
	Predefined_Mode = 0,
	RLE_Mode,
	FSE_Compressed_Mode,
	Repeat_Mode
};


struct table_settings {
	const char *title;
	u8 log, sym, max_log;
	u8 predef_distr[];
};
enum {TABLE_NOT_AVAILABLE = 255};

static const u8 *handle_decoding_table(const u8 *in, const u8 *end, u8 mode, u8 *log, u32 *table, const struct table_settings *settings) {
	u32 weights[64];
	switch (mode) {
	case Predefined_Mode:
		debug("predef %s\n", settings->title);
		for_range(i, 0, settings->sym) {
			u8 w = settings->predef_distr[i];
			weights[i] = w ? w + 1 : 0;
		}
		*log = settings->log;
		break;
	case RLE_Mode:
		debug("RLE %s\n", settings->title);
		check(end > in, "not enough data for RLE symbol\n");
		u8 sym = *in++;
		check(sym < settings->sym, "symbol value for RLE %s is to high\n", settings->title);
		*log = 0;
		table[0] = (u32)sym << 5;
		return in;
	case FSE_Compressed_Mode:
		debug("FSE %s\n", settings->title);
		assert(settings->sym <= 64);
		in = decode_fse_distribution(in, end, log, settings->sym, weights);
		if (!in) {return 0;}
		break;
	case Repeat_Mode:
		debug("repeat %s\n", settings->title);
		check(*log != TABLE_NOT_AVAILABLE, "no previous %s decoding table for Repeat_Mode\n", settings->title);
		return in;
	default:abort();
	}
	check(*log <= settings->max_log, "accuracy log too high\n");
	build_fse_table(*log, settings->sym, weights, table);
	return in;
}

static const struct table_settings length_settings = {
	.title = "match lengths",
	.log = 6, .max_log = 9,
	.sym = 53,
	.predef_distr = {
		1, 4, 3, 2,  2, 2, 2, 2,
		2, 1, 1, 1,  1, 1, 1, 1,
		1, 1, 1, 1,  1, 1, 1, 1,
		1, 1, 1, 1,  1, 1, 1, 1,
		1, 1, 1, 1,  1, 1, 1, 1,
		1, 1, 1, 1,  1, 1, 0, 0,
		0, 0, 0, 0,  0
	}
};

static const struct table_settings copy_settings = {
	.title = "literal lengths",
	.log = 6, .max_log = 9,
	.sym = 36,
	.predef_distr = {
		4, 3, 2, 2,  2, 2, 2, 2,
		2, 2, 2, 2,  2, 1, 1, 1,
		2, 2, 2, 2,  2, 2, 2, 2,
		2, 3, 2, 1,  1, 1, 1, 1,
		0, 0, 0, 0
	}
};

static const struct table_settings dist_settings = {
	.title = "offsets",
	.log = 5, .max_log = 8,
	.sym = 29,
	.predef_distr = {
		1, 1, 1, 1,  1, 1, 2, 2,
		2, 1, 1, 1,  1, 1, 1, 1,
		1, 1, 1, 1,  1, 1, 1, 1,
		0, 0, 0, 0,  0
	}
};

static size_t decompress_block(const u8 *in, const u8 *end, u8 *out, u8 *out_end, struct dectables *tables, u8 *window_start) {
	u8 *out_start = out;
	check(end - in >= 1, "not enough data for Literals_Section_Header\n");
	const u8 *literal_start = in;
	struct literals_probe probe = probe_literals(in, end);
	check(probe.end, "not enough data for literal section\n");
	in = probe.end;
	debug("%"PRIu32" (0x%"PRIx32") bytes of literals\n", probe.size, probe.size);
	if (unlikely(out_end - out < probe.size)) {return DECODE_NEED_MORE_SPACE;}
	check(end - in >= 1, "not enough data for Sequences_Section_Header\n");
	u32 num_seq = *in++;
	if (!num_seq) {
		if (!probe.decode) {
			lzcommon_literal_copy(out, probe.end - probe.size, probe.size);
		} else {
			check(probe.decode(literal_start, probe.end, out, out + probe.size, tables), "failed to decode literals\n");
		}
		return NUM_DECODE_STATUS + probe.size;
	}
	const u8 *literal_ptr;
	if (!probe.decode) {
		debug("raw literals\n");
		literal_ptr = probe.end - probe.size;
	} else {
		u8 *ptr = out_end + LZCOMMON_BLOCK - probe.size;
		check(probe.decode(literal_start, probe.end, ptr, ptr + probe.size, tables), "failed to decode literals\n");
		literal_ptr = ptr;
	}
	spew("%.*s", (int)probe.size, literal_ptr);
	debug("%"PRIu32" sequences\n", num_seq);
	if (num_seq >= 128) {
		if (num_seq == 255) {
			check(end - in >= 3, "not enough data for Sequences_Section_Header\n");
			num_seq = (in[0] | (u32)in[1] << 8) + 0x7f00;
			in += 2;
		} else {
			check(end - in >= 2, "not enough data for Sequences_Section_Header\n");
			num_seq = (num_seq - 128) << 8 | *in++;
		}
	} else {
		check(end - in >= 1, "not enough data for Sequences_Section_Header\n");
	}
	u8 scm = *in++;
	debug("scm0x%"PRIx8"\n", scm);
	check(scm % 4 == 0, "reserved feature used in Sequences_Compression_Modes\n");
	in = handle_decoding_table(in, end, scm >> 6 & 3, &tables->copy_log, tables->copy_table, &copy_settings);
	if (!in) {return 0;}
	in = handle_decoding_table(in, end, scm >> 4 & 3, &tables->dist_log, tables->dist_table, &dist_settings);
	if (!in) {return 0;}
	in = handle_decoding_table(in, end, scm >> 2 & 3, &tables->length_log, tables->length_table, &length_settings);
	if (!in) {return 0;}

	struct sequences_state seqstate;
	check(init_sequences(&seqstate, in, end, tables), "sequence decoding initialization failed");
	u64 sequence_buffer[128];
	while (num_seq) {
		u32 seq_this_round = ARRAY_SIZE(sequence_buffer);
		if (num_seq < seq_this_round) {seq_this_round = num_seq;}
		check(decode_sequences(&seqstate, in, seq_this_round, sequence_buffer, tables), "sequence decoding failed");
		for_range(i, 0, seq_this_round) {
			u64 sequence = sequence_buffer[i];
			u32 copy = sequence & 0x1ffff, length = (u32)(sequence >> 17 & 0x1ffff) + 3;
			u32 dist = sequence >> 34;
			spew("seq copy%"PRIu32" length%"PRIu32" dist%"PRIu32" %.*s\n", copy, length, dist, (int)copy, literal_ptr);

			check(copy <= probe.size, "sequence specifies to copy more literals than available\n");
			if (unlikely(out_end - out < length + probe.size)) {return DECODE_NEED_MORE_SPACE;}
			lzcommon_literal_copy(out, literal_ptr, copy);
			out += copy; literal_ptr += copy; probe.size -= copy;

			spew("available window: %zu\n", (size_t)(out - window_start));
			check(dist <= out - window_start, "match copy distance too far: %"PRIu32" > %zu\n", dist, (size_t)(out - window_start));
			if (dist <= length) {
				spew("match copy: %.*s…\n",(int)dist, out - dist);
			} else {
				spew("match copy: %.*s←\n", (int)length, out - dist);
			}
			lzcommon_match_copy(out, dist, length);
			out += length;

			if (out - window_start < 100) {
				spew("last output: %.*s\n", (int)(out - window_start), window_start);
			} else {
				spew("last output: %.100s\n", out - 100);
			}
		}
		num_seq -= seq_this_round;
	}
	check(finish_sequences(&seqstate, in, tables), "finishing sequence decoding failed");
	lzcommon_literal_copy(out, literal_ptr, probe.size);
	out += probe.size;
	return NUM_DECODE_STATUS + (out - out_start);
}

static enum compr_probe_status probe(const u8 *in, const u8 *end, u64 *size) {
	if (end - in < 4) {return COMPR_PROBE_NOT_ENOUGH_DATA;}
	u32 magic = in[0] | (u32)in[1] << 8 | (u32)in[2] << 16 | (u32)in[3] << 24;
	if (magic != 0xfd2fb528) {return COMPR_PROBE_WRONG_MAGIC;}
	if (end - in < 6) {return COMPR_PROBE_NOT_ENOUGH_DATA;}
	u8 frame_header_desc = in[4];
	if (frame_header_desc & Frame_Header_Descriptor_RESERVED) {
		info("reserved zstd feature used, cannot decode\n");
		return COMPR_PROBE_RESERVED_FEATURE;
	}
	if (frame_header_desc & 3) {
		info("dictionaries not implemented, cannot decompress\n");
		return COMPR_PROBE_UNIMPLEMENTED_FEATURE;
	}
	u8 fcs_field_size = frame_header_desc >> 6;
	if ((frame_header_desc & Single_Segment_Flag) || fcs_field_size != 0) {
		fcs_field_size = 1 << fcs_field_size;
	}
	u8 frame_header_size = fcs_field_size
		+ !(frame_header_desc & Single_Segment_Flag); /* window descriptor */
	in += 5;
	if (end - in < frame_header_size) {return COMPR_PROBE_NOT_ENOUGH_DATA;}
	u64 UNUSED window_size = 0;
	if (~frame_header_desc & Single_Segment_Flag) {
		window_size = (8 | (in[0] & 7)) << 7 << (in[0] >> 3);
		in += 1;
	}
	*size = 0;
	switch (fcs_field_size) {
	case 8:
		*size |= (u64)in[4] << 32 | (u64)in[5] << 40 | (u64)in[6] << 48 | (u64)in[7] << 56;
		FALLTHROUGH;
	case 4:
		*size |= (u64)in[2] << 16 | (u64)in[3] << 24;
		FALLTHROUGH;
	case 2: 
		*size |=  (u64)in[1] << 8;
		if (fcs_field_size == 2) {*size += 0x100;}
		FALLTHROUGH;
	case 1:
		*size |= *in;
		return COMPR_PROBE_SIZE_KNOWN;
	case 0: return COMPR_PROBE_SIZE_UNKNOWN;
	default: abort();
	}
}

struct xxh64_state {
	_Alignas(8) u8 buf[32];
	u64 state[4];
	u64 len;
	u8 offset;
	_Bool long_hash;
};

struct zstd_dec_state {
	struct decompressor_state st;
	struct dectables tables;
	struct xxh64_state xxh64;
	u64 window_size;
	_Bool have_content_checksum;
};

#define XXH64_SEED 0

#define U64(x) x##ull
static const u64 xxh64_primes[5] = {
	U64(11400714785074694791),
	U64(14029467366897019727),
	U64(1609587929392839161),
	U64(9650029242287828579),
	U64(2870177450012600261)
};

u64 xxh64_step(u64 state) {
	return (state << 31 | state >> 33) * xxh64_primes[0];
}

u64 xxh64_shuffle(u64 val) {
	return xxh64_step(val * xxh64_primes[1]);
}

u64 xxh64_round(u64 state, u64 val) {
	return xxh64_step(state + (val * xxh64_primes[1]));
}

u64 xxh64_mergestep(u64 state) {
	return state * xxh64_primes[0] + xxh64_primes[3];
}

u64 xxh64_merge(u64 state, u64 val) {
	return xxh64_mergestep(state ^ xxh64_shuffle(val));
}

static u64 xxh64_finalize(const struct xxh64_state *state) {
	u64 xxh64 = XXH64_SEED + xxh64_primes[4];
	if (state->long_hash) {
		debug("xxh64 merge\n");
		u64 a, b, c, d;
		a = state->state[0];
		b = state->state[1];
		c = state->state[2];
		d = state->state[3];
		xxh64 = (a << 1 | a >> 63) + (b << 7 | b >> 57) + (c << 12 | c >> 52) + (d << 18 | d >> 46);
		xxh64 = xxh64_merge(xxh64, a);
		xxh64 = xxh64_merge(xxh64, b);
		xxh64 = xxh64_merge(xxh64, c);
		xxh64 = xxh64_merge(xxh64, d);
	}
	xxh64 += state->len;
	u8 single_rounds = state->offset >> 3;
	assert(single_rounds < 4);
	const u8 *ptr = state->buf;
	while (single_rounds--) {
		u64 dw = ldle64a((const u64 *)ptr);
		debug("xxh64 8byte: 0x%"PRIx64"\n", dw);
		xxh64 ^= xxh64_shuffle(dw);
		ptr += 8;
		xxh64 = xxh64_mergestep(xxh64 << 27 | xxh64 >> 37);
	}
	if (state->offset & 4) {
		u32 w = ldle32a((const u32 *)ptr);
		debug("xxh64 4byte: 0x%"PRIx32"\n", w);
		xxh64 ^= w * xxh64_primes[0];
		ptr += 4;
		xxh64 = (xxh64 << 23 | xxh64 >> 41) * xxh64_primes[1] + xxh64_primes[2];
	}
	u8 byte_rounds = state->offset & 3;
	while (byte_rounds--) {
		debug("xxh64 one byte: 0x%"PRIx8"\n", *ptr);
		xxh64 ^= *ptr++ * xxh64_primes[4];
		xxh64 = (xxh64 << 11 | xxh64 >> 53) * xxh64_primes[0];
	}
	xxh64 ^= xxh64 >> 33;
	xxh64 *= xxh64_primes[1];
	xxh64 ^= xxh64 >> 29;
	xxh64 *= xxh64_primes[2];
	xxh64 ^= xxh64 >> 32;
	return xxh64;
}

static size_t decode_trailer(struct decompressor_state *state, const u8 *in, const u8 *end) {
	struct zstd_dec_state *st = (struct zstd_dec_state *)state;
	st->st.decode = 0;
	if (st->have_content_checksum) {
		if (end - in < 4) {return DECODE_NEED_MORE_DATA;}
		u64 xxh64 = xxh64_finalize(&st->xxh64);
		u32 csum = in[0] | (u32)in[1] << 8 | (u32)in[2] << 16 | (u32)in[3] << 24;
		if (unlikely((xxh64 & 0xffffffff) != csum)) {
			info("checksum mismatch: read %"PRIx32", computed %"PRIx64"\n", csum, xxh64);
			return DECODE_ERR;
		}
		info("XXH64: %"PRIx64"\n", xxh64);
		return NUM_DECODE_STATUS + 4;
	} else {
		info("no checksum\n");
	}
	return NUM_DECODE_STATUS;
}

static void xxh64_update(struct xxh64_state *state, const u8 *buf, size_t size) {
	state->len += size;
	u8 xxh_offset = state->offset;
	if (size <= 32 && xxh_offset + size < 32) {
		lzcommon_literal_copy(state->buf + xxh_offset, buf, size);
		state->offset = xxh_offset + size;
	} else {
		u8 fillup = 32 - xxh_offset;
		assert(size >= fillup);
		lzcommon_literal_copy(state->buf + xxh_offset, buf, 32 - xxh_offset);
		size -= fillup;
		buf += fillup;
		state->state[0] = xxh64_round(state->state[0], ldle64a((u64 *)state->buf));
		state->state[1] = xxh64_round(state->state[1], ldle64a((u64 *)(state->buf + 8)));
		state->state[2] = xxh64_round(state->state[2], ldle64a((u64 *)(state->buf + 16)));
		state->state[3] = xxh64_round(state->state[3], ldle64a((u64 *)(state->buf + 24)));
		state->long_hash = 1;
		while (size >= 32) {
			u64 a, b, c, d;
#ifdef LDLE64U
			LDLE64U(buf, a); buf += 8;
			LDLE64U(buf, b); buf += 8;
			LDLE64U(buf, c); buf += 8;
			LDLE64U(buf, d); buf += 8;
#else
			memcpy(state->buf, buf, 32);
			buf += 32;
			a = ldle64a((const u64 *)state->buf);
			b = ldle64a((const u64 *)(state->buf+8));
			c = ldle64a((const u64 *)(state->buf+16));
			d = ldle64a((const u64 *)(state->buf+24));
#endif
			size -= 32;
			state->state[0] = xxh64_round(state->state[0], a);
			state->state[1] = xxh64_round(state->state[1], b);
			state->state[2] = xxh64_round(state->state[2], c);
			state->state[3] = xxh64_round(state->state[3], d);
		}
		u8 offset = 0;
		while (size--) {
			state->buf[offset++] = *buf++;
		}
		state->offset = offset;
	}
}

static size_t decode_block(struct decompressor_state *state, const u8 *in, const u8 *end) {
	struct zstd_dec_state *st = (struct zstd_dec_state *)state;
	if (unlikely(end - in < 3)) {return DECODE_NEED_MORE_DATA;}
	u8 block_header0 = *in;
	_Bool last_block = block_header0 & 1;
	u32 block_size = block_header0 >> 3 | (u32)in[1] << 5 | (u32)in[2] << 13;
	in += 3;
	size_t total_size = block_size + 3, decomp_size;
	u8 *out = st->st.out;
	switch (block_header0 >> 1 & 3) {
	case Raw_Block:
		debug("%"PRIu32"-byte Raw_Block\n", block_size);
		if (st->st.out_end - out < block_size) {return DECODE_NEED_MORE_SPACE;}
		if (end - in < block_size) {return DECODE_NEED_MORE_DATA;}
		lzcommon_literal_copy(out, in, block_size);
		decomp_size = block_size;
		in += block_size;
		break;
	case RLE_Block:
		debug("%"PRIu32"-byte RLE_Block\n", block_size);
		if (st->st.out_end - out < block_size) {return DECODE_NEED_MORE_SPACE;}
		if (unlikely(end - in < 1)) {return DECODE_NEED_MORE_DATA;}
		if (block_size) {
			*out = *in++;
			lzcommon_match_copy(out + 1, 1, block_size - 1);
		}
		decomp_size = block_size;
		total_size = 4;
		break;
	case Compressed_Block:
		debug("%"PRIu32"-byte compressed block\n", block_size);
		if (end - in < block_size) {return DECODE_NEED_MORE_DATA;}
		size_t res = decompress_block(in, in + block_size, out, st->st.out_end, &st->tables, st->st.window_start);
		if (res < NUM_DECODE_STATUS) {return res;}
		decomp_size = res - NUM_DECODE_STATUS;
		break;
	default:
		info("reserved block type used\n");
		return DECODE_ERR;
	}
	if (st->have_content_checksum) {
		xxh64_update(&st->xxh64, out, decomp_size);
	}
	st->st.out = out + decomp_size;
	if (last_block) {st->st.decode = decode_trailer;}
	return NUM_DECODE_STATUS + total_size;
}

void xxh64_init(struct xxh64_state *state) {
	state->offset = 0;
	state->len = 0;
	state->long_hash = 0;
	state->state[0] = XXH64_SEED + xxh64_primes[0] + xxh64_primes[1];
	state->state[1] = XXH64_SEED + xxh64_primes[1];
	state->state[2] = XXH64_SEED;
	state->state[3] = XXH64_SEED - xxh64_primes[0];
}

static const u8 *init(struct decompressor_state *state, const u8 *in, const u8 *end) {
	struct zstd_dec_state *st = (struct zstd_dec_state *)state;
	assert(end - in >= 6);
	u8 frame_header_desc = in[4];
	info("decompressing zstd, frame header descriptor 0x%02x\n", (unsigned)frame_header_desc);
	u8 fcs_field_size = frame_header_desc >> 6;
	if ((frame_header_desc & Single_Segment_Flag) || fcs_field_size != 0) {
		fcs_field_size = 1 << fcs_field_size;
	}
	debug("fcsfs%"PRIu8"\n", fcs_field_size);
	u8 frame_header_size = fcs_field_size
		+ !(frame_header_desc & Single_Segment_Flag); /* window descriptor */
	assert(end - in > 5 + frame_header_size);
	in += 5;

	st->window_size = 0;
	if (~frame_header_desc & Single_Segment_Flag) {
		st->window_size = (u64)(8 | (in[0] & 7)) << 7 << (in[0] >> 3);
		info("window size: 0x%"PRIx64"\n", st->window_size);
		in += 1;
	} else switch (fcs_field_size) {
	case 8:
		st->window_size |= (u64)in[4] << 32 | (u64)in[5] << 40 | (u64)in[6] << 48 | (u64)in[7] << 56;
		FALLTHROUGH;
	case 4:
		st->window_size |= (u64)in[2] << 16 | (u64)in[3] << 24;
		FALLTHROUGH;
	case 2:
		st->window_size |=  (u64)in[1] << 8;
		if (fcs_field_size == 2) {st->window_size += 0x100;}
		FALLTHROUGH;
	case 1:
		st->window_size |= *in;
		break;
	default: abort();	/* must be present if single-segment */
	}
	in += fcs_field_size;

	st->tables.dist_log = st->tables.length_log = st->tables.copy_log = TABLE_NOT_AVAILABLE;
	st->tables.huff_depth = 0;
	st->tables.dist1 = 1; st->tables.dist2 = 4; st->tables.dist3 = 8;
	st->st.decode = decode_block;
	st->have_content_checksum = !!(frame_header_desc & Content_Checksum_Flag);
	xxh64_init(&st->xxh64);
	return in;
}

const struct decompressor zstd_decompressor = {
	.probe = probe,
	.state_size = sizeof(struct zstd_dec_state),
	.init = init
};
