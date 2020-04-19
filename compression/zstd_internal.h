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

typedef _Bool (*decode_func)(const u8 *in, const u8 *end, u8 *out, u8 *out_end, struct dectables *tables);

struct literals_probe {
	const u8 *end;
	u32 size;
	u32 flags;
	decode_func decode;
};

struct sequences_state {
	u64 bits;
	const u8 *ptr;
	u8 num_bits;
	u32 dist1, dist2, dist3;
	u32 entry_copy, entry_length, entry_dist;
};

static inline u8 UNUSED fse_bits(u32 entry) {return entry & 31;}
static inline u8 UNUSED fse_sym(u32 entry) {return entry >> 5 & 63;}
static inline u32 UNUSED fse_base(u32 entry) {return entry >> 11;}

_Bool init_sequences(struct sequences_state *state, const u8 *start, const u8 *ptr, const struct dectables *tables);
_Bool decode_sequences(struct sequences_state *state, const u8 *start, size_t num_seq, u64 *out, const struct dectables *tables);
_Bool finish_sequences(struct sequences_state *state, const u8 *start, struct dectables *tables);

const u8 *decode_fse_distribution(const u8 *in, const u8 *end, u8 *log, u8 max_sym, u32 *weights);
void build_fse_table(u8 log, u8 num_sym, const u32 *distribution, u32 *table);
const u8 *decode_tree_description(const u8 *in, const u8 *end, struct dectables *tables);
struct literals_probe probe_literals(const u8 *in, const u8 *end);
