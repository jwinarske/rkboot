/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include "../include/defs.h"
#include "../include/log.h"

#define check(expr, ...) if (unlikely(!(expr))) {info(__VA_ARGS__);return 0;}
#define MASK31(n) ((1 << (n)) - 1)

struct huff_entry {u8 sym; u8 len;};

struct dectables {
	u8 huff_depth;
	u8 copy_log;
	u8 length_log;
	u8 dist_log;
	u64 dist1, dist2, dist3;
	struct huff_entry huff_table[1 << 11];
	u32 copy_table[1 << 9];
	u32 length_table[1 << 9];
	u32 dist_table[1 << 8];
};

#define DEFINE_LITERALS_FLAGS X(RAW_LITERALS) X(RLE_LITERALS) X(TREELESS) X(TRUNCATED) X(1STREAM)

enum {
#define X(name) ZSTD_##name##_BIT,
	DEFINE_LITERALS_FLAGS
#undef X
	NUM_ZSTD_LITERAL_FLAGS
};
_Static_assert(NUM_ZSTD_LITERAL_FLAGS <= 32, "more than 32 literal flags");
enum {
#define X(name) ZSTD_##name = 1 << ZSTD_##name##_BIT,
	DEFINE_LITERALS_FLAGS
#undef X
	/* flag definition (polarity and order) was chosen such that these generate nice code: they are masks of consecutive bits (good for A64) that does not cross a 16-bit boundary (good for PPC) */
	ZSTD_ABNORMAL_LITERAL_MASK = ZSTD_RAW_LITERALS | ZSTD_RLE_LITERALS | ZSTD_TREELESS | ZSTD_TRUNCATED | ZSTD_1STREAM,
	ZSTD_NO_TREE_DESC_MASK = ZSTD_RAW_LITERALS | ZSTD_RLE_LITERALS | ZSTD_TREELESS | ZSTD_TRUNCATED,
	ZSTD_NON_HUFF_LITERALS_MASK = ZSTD_RAW_LITERALS | ZSTD_RLE_LITERALS,
};


struct literals_probe {
	const u8 *start;
	const u8 *end;
	u32 size;
	u32 flags;
};

struct sequences_state {
	u64 bits;
	const u8 *ptr;
	u32 num_bits;
	u32 dist1, dist2, dist3;
	u32 entry_copy, entry_length, entry_dist;
	size_t bits_left;
};

static inline u8 UNUSED fse_bits(u32 entry) {return entry & 31;}
static inline u8 UNUSED fse_sym(u32 entry) {return entry >> 5 & 63;}
static inline u32 UNUSED fse_base(u32 entry) {return entry >> 11;}

_Bool init_sequences(struct sequences_state *state, const u8 *start, const u8 *ptr, const struct dectables *tables);
_Bool decode_sequences(struct sequences_state *state, const u8 *start, size_t num_seq, u64 *out, const struct dectables *tables);
_Bool finish_sequences(struct sequences_state *state, const u8 *start, struct dectables *tables);

const u8 *fse_decode_distribution(const u8 *in, const u8 *end, u8 *log, u8 max_sym, u32 *weights);
void fse_build_table(u8 log, u8 num_sym, const u32 *distribution, u32 *table);

const u8 *zstd_decode_tree_description(const u8 *in, const u8 *end, struct dectables *tables);
struct literals_probe zstd_probe_literals(const u8 *in, const u8 *end);
_Bool zstd_decompress_literal_stream(const u8 *in, const u8 *end, u8 *out, u8 *out_end, const struct huff_entry *table);
_Bool zstd_decode_huff_4streams(const u8 *in, const u8 *end, u8 *out, u8 *out_end, const struct huff_entry *table);
