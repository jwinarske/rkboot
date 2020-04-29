/* SPDX-License-Identifier: CC0-1.0 */
#include "zstd_internal.h"
#include <inttypes.h>
#include <stdlib.h>
#include <assert.h>
#include "compression.h"

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
	debug("%.*s", (int)probe.size, literal_ptr);
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
			debug("seq copy%"PRIu32" length%"PRIu32" dist%"PRIu32" %.*s\n", copy, length, dist, (int)copy, literal_ptr);

			check(copy <= probe.size, "sequence specifies to copy more literals than available\n");
			if (unlikely(out_end - out < length + probe.size)) {return DECODE_NEED_MORE_SPACE;}
			lzcommon_literal_copy(out, literal_ptr, copy);
			out += copy; literal_ptr += copy; probe.size -= copy;

			check(dist <= out - window_start, "match copy distance too far\n");
			if (dist <= length) {
				debug("match copy: %.*s…\n",(int)dist, out - dist);
			} else {
				debug("match copy: %.*s←\n", (int)length, out - dist);
			}
			lzcommon_match_copy(out, dist, length);
			out += length;

			if (out - window_start < 100) {
				debug("last output: %.*s\n", (int)(out - window_start), window_start);
			} else {
				debug("last output: %.100s\n", out - 100);
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

struct zstd_dec_state {
	struct decompressor_state st;
	struct dectables tables;
	u64 window_size;
	_Bool have_content_checksum;
};

static size_t decode_trailer(struct decompressor_state *state, const u8 *in, const u8 *end) {
	struct zstd_dec_state *st = (struct zstd_dec_state *)state;
	st->st.decode = 0;
	if (st->have_content_checksum) {
		if (end - in < 4) {return DECODE_NEED_MORE_DATA;}
		/* FIXME: implement checksum */
		return NUM_DECODE_STATUS + 4;
	}
	return NUM_DECODE_STATUS;
}

static size_t decode_block(struct decompressor_state *state, const u8 *in, const u8 *end) {
	struct zstd_dec_state *st = (struct zstd_dec_state *)state;
	if (unlikely(end - in < 3)) {return DECODE_NEED_MORE_DATA;}
	u8 block_header0 = *in;
	_Bool last_block = block_header0 & 1;
	u32 block_size = block_header0 >> 3 | (u32)in[1] << 5 | (u32)in[2] << 13;
	in += 3;
	size_t total_size = block_size + 3;
	switch (block_header0 >> 1 & 3) {
	case Raw_Block:
		debug("%"PRIu32"-byte Raw_Block\n", block_size);
		if (st->st.out_end - st->st.out < block_size) {return DECODE_NEED_MORE_SPACE;}
		if (end - in < block_size) {return DECODE_NEED_MORE_DATA;}
		lzcommon_literal_copy(st->st.out, in, block_size);
		st->st.out += block_size;
		in += block_size;
		break;
	case RLE_Block:
		debug("%"PRIu32"-byte RLE_Block\n", block_size);
		if (st->st.out_end - st->st.out < block_size) {return DECODE_NEED_MORE_SPACE;}
		if (unlikely(end - in < 1)) {return DECODE_NEED_MORE_DATA;}
		u8 *out = st->st.out;
		if (block_size) {
			*out++ += *in++;
			lzcommon_match_copy(out, 1, block_size - 1);
			*out += block_size - 1;
		}
		total_size = 4;
		break;
	case Compressed_Block:
		debug("%"PRIu32"-byte compressed block\n", block_size);
		if (end - in < block_size) {return DECODE_NEED_MORE_DATA;}
		size_t res = decompress_block(in, in + block_size, st->st.out, st->st.out_end, &st->tables, st->st.window_start);
		if (res < NUM_DECODE_STATUS) {return res;}
		st->st.out += res - NUM_DECODE_STATUS;
	}
	if (last_block) {st->st.decode = decode_trailer;}
	return NUM_DECODE_STATUS + total_size;
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
		st->window_size = (8 | (in[0] & 7)) << 7 << (in[0] >> 3);
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
	return in;
}

const struct decompressor zstd_decompressor = {
	.probe = probe,
	.state_size = sizeof(struct zstd_dec_state),
	.init = init
};