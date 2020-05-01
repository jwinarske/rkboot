/* SPDX-License-Identifier: CC0-1.0 */
#include <inttypes.h>
#include <assert.h>
#include "zstd_internal.h"

#define X(a, b) a | b << 24
	static const u32 ctable_copy[36] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
		X(16, 1), X(18, 1), X(20, 1), X(22, 1),
		X(24, 2), X(28, 2), X(32, 3), X(40, 3),
		X(48, 4), X(64, 6), X(128, 7), X(256, 8),
		X(512, 9), X(1024, 10), X(2048, 11), X(4096, 12),
		X(8192, 13), X(16384, 14), X(32768, 15), X(65536, 16)
	};
	static const u32 ctable_length[53] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
		19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
		X(32, 1), X(34, 1), X(36, 1), X(38, 1),
		X(40, 2), X(44, 2), X(48, 3), X(56, 3),
		X(64, 4), X(80, 4), X(96, 5), X(128, 7),
		X(256, 8), X(512, 9), X(1024, 10), X(2048, 11),
		X(4096, 12), X(8192, 13), X(16384, 14), X(32768, 15),
		X(65536, 16)
	};
#undef X
#ifdef __AARCH64EL__
#define LDLE64U(ptr, bits) __asm__("ldr %0, [%1]" : "=r"(bits) : "r"(ptr))
#endif
#ifdef LDLE64U
#define REFILL(total_bits) do {\
	u32 read_bytes = 8 - (num_bits + 7) / 8;\
	read_bytes = ptr - start < read_bytes ? ptr - start : read_bytes;\
	ptr -= read_bytes;\
	LDLE64U(ptr, bits);\
	num_bits += read_bytes * 8;\
	check(num_bits >= total_bits, "not enough data for sequences bitstream\n");\
} while (0)
#else
#define REFILL(total_bits) do {if (num_bits < total_bits) {\
	check(ptr - start >= (total_bits - num_bits + 7) / 8, "not enough data for sequences bitstream\n");\
	do {\
		bits = bits << 8 | *--ptr;\
		num_bits += 8;\
	} while (num_bits <= 56 && start < ptr);\
}} while (0)
#endif
#define DECODE3(bits_a, var_a, bits_b, var_b, bits_c, var_c) do {\
		spew(#bits_a "%"PRIu32" " #bits_b "%"PRIu32" " #bits_c "%"PRIu32"\n", (u32)(bits_a), (u32)(bits_b), (u32)(bits_c));\
		u32 total_bits = (bits_a) + (bits_b) + (bits_c);\
		REFILL(total_bits);\
		spew("0x%"PRIx64"/%"PRIu8"\n", bits, num_bits);\
		u64 tmp1 = bits >> (num_bits - total_bits);\
		u32 tmp2 = bits >> (num_bits - (bits_a));\
		num_bits -= total_bits;\
		u32 tmp3 = tmp1 >> (bits_c);\
		spew("tmp %"PRIx64" %"PRIx32" %"PRIx32"\n", tmp1, tmp2, tmp3);\
		(var_a) = tmp2 & ((1 << (bits_a)) - 1);\
		(var_b) = tmp3 & ((1 << (bits_b)) - 1);\
		(var_c) = tmp1 & ((1 << (bits_c)) - 1);\
		spew(#var_a "%"PRIu32" " #var_b "%"PRIu32" " #var_c "%"PRIu32"\n", (u32)(var_a), (u32)(var_b), (u32)(var_c));\
	}while (0)

_Bool init_sequences(struct sequences_state *state, const u8 *start, const u8 *ptr, const struct dectables *tables) {
	u32 bits_dist = tables->dist_log, bits_length = tables->length_log, bits_copy = tables->copy_log;
	check(ptr - start >= 1, "not enough data to find the end of sequences\n");
	u8 last_byte = *--ptr;
	check(last_byte != 0, "last byte is 0\n");
	u64 bits = last_byte;
	u32 num_bits = __builtin_clz(1) - __builtin_clz(last_byte);
	/* fill up the bits container, so that as long as we haven't reached the end, we are always at least 8 bytes in and can thus use an unaligned 8-byte load to refill */
	while (num_bits <= 56 && start < ptr) {
		bits = bits << 8 | *--ptr;
		num_bits += 8;
	}

	/* this BS is only needed because the spec writers decided that the initial states should be encoded in the order copy-dist-length, but updates in the order copy-length-dist (if they were the same, we could just use the update code in the loop to read initial states). Thanks Facebook. */
	u32 state_copy, state_length, state_dist;
	DECODE3(bits_copy, state_copy, bits_dist, state_dist, bits_length, state_length);

	state->bits = bits;
	state->num_bits = num_bits;
	state->ptr = ptr;
	state->entry_copy = state_copy << 11; state->entry_length = state_length << 11; state->entry_dist = state_dist << 11;
	state->dist1 = tables->dist1; state->dist2 = tables->dist2; state->dist3 = tables->dist3;
	debug("0x%"PRIx64"/%"PRIu8"\n", bits, num_bits);
	return 1;
}

static inline u64 pack_sequence(struct sequences_state *state, u32 copy, u32 length, u32 dist) {
	u32 used_dist;
	if (dist > 3) {
		state->dist3 = state->dist2;
		state->dist2 = state->dist1;
		state->dist1 = used_dist = dist - 3;
	} else if (copy == 0) {
		if (dist == 3) {
			debug("weird repeat offset, history %"PRIu32" %"PRIu32" %"PRIu32"\n", state->dist1, state->dist2, state->dist3);
			used_dist = state->dist1 - 1;
			state->dist3 = state->dist2;
			state->dist2 = state->dist1;
		} else if (dist == 1) {
			used_dist = state->dist2;
			state->dist2 = state->dist1;
		} else {assert(dist == 2);
			used_dist = state->dist3;
			state->dist3 = state->dist2;
			state->dist2 = state->dist1;
		}
	} else {
		if (dist == 1) {
			used_dist = state->dist1;
		} else if (dist == 2) {
			used_dist = state->dist2;
			state->dist2 = state->dist1;
		} else {
			used_dist = state->dist3;
			state->dist3 = state->dist2;
			state->dist2 = state->dist1;
		}
	}
	state->dist1 = used_dist;
	spew("seq copy%"PRIu32" length%"PRIu32" dist%"PRIu32"\n", copy, length + 3, used_dist);
	assert(copy < 0x20000 && length < 0x20000);
	return copy | (u64)length << 17 | (u64)used_dist << 34;
}

_Bool decode_sequences(struct sequences_state *state, const u8 *start, size_t num_seq, u64 *out, const struct dectables *tables) {
	const u32 *table_copy = tables->copy_table, *table_dist = tables->dist_table, *table_length = tables->length_table;
	u64 bits = state->bits;
	u32 num_bits = state->num_bits;
	const u8 *ptr = state->ptr;
	u32 entry_copy = state->entry_copy, entry_length = state->entry_length, entry_dist = state->entry_dist;
	debug("0x%"PRIx64"/%"PRIu8"\n", bits, num_bits);
	do {
		const u32 bits_copy = entry_copy & 31, bits_length = entry_length & 31, bits_dist = entry_dist & 31;
		u32 state_copy, state_length, state_dist;
		DECODE3(bits_copy, state_copy, bits_length, state_length, bits_dist, state_dist);
		state_copy += fse_base(entry_copy);
		state_length += fse_base(entry_length);
		state_dist += fse_base(entry_dist);
		entry_copy = table_copy[state_copy], entry_dist = table_dist[state_dist], entry_length = table_length[state_length];
		const u32 sym_copy = entry_copy >> 5 & 63, sym_dist = entry_dist >> 5 & 63, sym_length = entry_length >> 5 & 63;
		spew("sym copy%"PRIu8" dist%"PRIu8" length%"PRIu8"\n", sym_copy, sym_dist, sym_length);
		const u32 const_length = ctable_length[sym_length], const_copy = ctable_copy[sym_copy];
		spew("clength0x%"PRIx32" ccopy0x%"PRIx32"\n", const_length, const_copy);
		u32 extra_dist, extra_length, extra_copy;
		const u32 ebits_dist = sym_dist, ebits_length = const_length >> 24, ebits_copy = const_copy >> 24;
		DECODE3(ebits_dist, extra_dist, ebits_length, extra_length, ebits_copy, extra_copy);
		extra_dist += (u32)1 << sym_dist;
		extra_copy += const_copy & 0xffffff;
		extra_length += const_length & 0xffffff;
		*out++ = pack_sequence(state, extra_copy, extra_length, extra_dist);
	} while (--num_seq);
	state->entry_copy = entry_copy; state->entry_length = entry_length; state->entry_dist = entry_dist;
	state->bits = bits; state->num_bits = num_bits; state->ptr = ptr;
	debug("0x%"PRIx64"/%"PRIu8"\n", bits, num_bits);
	return 1;
}

_Bool finish_sequences(struct sequences_state *state, const u8 *start, struct dectables *tables) {
	check(state->num_bits == 0 && state->ptr == start, "sequence bitstream was not fully consumed\n");
	tables->dist1 = state->dist1; tables->dist2 = state->dist2; tables->dist3 = state->dist3;
	return 1;
}
