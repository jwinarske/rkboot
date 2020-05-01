/* SPDX-License-Identifier: CC0-1.0 */
#include "../include/defs.h"
#include "../include/log.h"
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>

#define check(expr, ...) if (unlikely(!(expr))) {info(__VA_ARGS__);return 0;}

static u8 trunc_length(u32 range) {
	return __builtin_clz(1) - __builtin_clz(range);
}

static u32 trunc_cutoff(u32 range, u8 length) {
	return (2 << length) - range;
}

const u8 *decode_fse_distribution(const u8 *in, const u8 *end, u8 *log_ptr, u8 max_sym, u32 *weights) {
	check(end - in >= 1, "not enough data for FSE accuracy value\n");
	const u8 UNUSED *start = in;
	u32 bits = *in++;
	u8 num_bits = 8;
	u8 log = *log_ptr = (bits & 15) + 5;
	bits >>= 4; num_bits -= 4;
	debug("FSE log: %"PRIu8"\n", log);
	u32 entries_left = 1 << log;
	for_range(sym, 0, max_sym) {
		const u8 length = trunc_length(entries_left + 2);
		spew("left%"PRIu32" len%"PRIu8"\n", entries_left, length);
		u32 mask = (1 << length) - 1;
		u32 cutoff = trunc_cutoff(entries_left + 2, length);
		while (num_bits < length) {
			check(end - in >= 1, "not enough data for symbol probability\n");
			bits |= *in++ << num_bits;
			num_bits += 8;
		}
		spew("prob 0x%"PRIx32"/%"PRIu8"\n", bits, num_bits);
		u32 val = bits & mask;
		bits >>= length; num_bits -= length;
		if (val >= cutoff) {
			if (num_bits < 1) {
				check(end - in >= 1, "not enough data for symbol probability\n");
				bits |= *in++ << num_bits;
				num_bits += 8;
			}
			spew("hibit 0x%"PRIx32"/%"PRIu8"\n", bits, num_bits);
			val += bits & 1 ? (1 << length) - cutoff : 0;
			bits >>= 1; num_bits -= 1;
		}
		spew("sym%"PRIu32" prob%"PRIu32"\n", sym, val);
		weights[sym] = val;
		if (val != 1) {
			assert(val == 0 || val - 1 <= entries_left);
			entries_left -= val == 0 ? 1 : val - 1;
			if (!entries_left) {
				debug("finishing FSE distribution at %"PRIu8" bits into the last of %zu bytes\n", num_bits, in - start);
				/* check(bits == 0, "unused bits are not 0\n"); ← this is not guaranteed by spec, but should be the case for sane encoders */
				for_range(i, sym + 1, max_sym) {weights[i] = 1;}
				return in;
			}
		} else {
			u32 rep; do {
				if (num_bits < 2) {
					check(end - in >= 1, "not enough data for FSE distribution 0-repeat flag\n");
					bits |= *in++ << num_bits;
					num_bits += 8;
					spew("rep 0x%"PRIx32"/%"PRIu8"\n", bits, num_bits);
				}
				rep = bits & 3;
				bits >>= 2; num_bits -= 2;
				spew("rep%"PRIu32"\n", rep);
				check(sym + rep <= max_sym, "0-repeat in FSE distribution goes over number of symbols\n");
				for_range(i, 0, rep) {weights[++sym] = 1;}
			} while (rep == 3);
		}
	}
	return 0;
}

static void UNUSED dump_dectable(u8 log, u32 UNUSED *table) {
	for (u32 i = 0; i < (1 << log); i += 4) {
		debug("%4"PRIu32": %08"PRIx32" %08"PRIx32" %08"PRIx32" %08"PRIx32"\n", i, table[i], table[i + 1], table[i + 2], table[i + 3]);
	}
}

void build_fse_table(u8 log, u8 num_sym, const u32 *distribution, u32 *table) {
	assert(log >= 5 && log <= 20 && num_sym <= 64);
	const u32 mask = (1 << log) - 1;
	const u32 step = 5 << (log - 3) | 3;
	u8 num_lt1 = 0;
	for_range(sym, 0, num_sym) {
		if (distribution[sym] == 0) {
			table[mask - num_lt1] = log | sym << 5;
			num_lt1 += 1;
		}
	}
	u32 pos = 0;
	u8 num_bits[64];
	u32 cutoff[64];
	u32 counter[64];
	for_range(sym, 0, num_sym) {
		u32 prob = distribution[sym];
		counter[sym] = 0;
		if (prob <= 1) {
			/* these should never be accessed */
			num_bits[sym] = log;
			cutoff[sym] = 1 << log;
			continue;
		}
		u8 len = trunc_length(prob - 1);
		num_bits[sym] = log - len;
		cutoff[sym] = trunc_cutoff(prob - 1, len);
		for_range(i, 0, prob - 1) {
			table[pos] = sym;
			do {
				pos += step;
				pos &= mask;
			} while (pos > mask - num_lt1);
		}
	}
	assert(pos == 0);
	for_range(i, 0, (1 << log) - num_lt1) {
		u32 sym = table[i];
		assert(sym < num_sym);
		assert(distribution[sym] > 1);
		u8 this_num_bits = num_bits[sym];
		u32 this_cutoff = cutoff[sym];
		u32 this_counter = counter[sym]++;
		u32 base, read_bits;
		if (this_counter < this_cutoff) {
			read_bits = this_num_bits;
			base = this_counter + (1 << (log - this_num_bits)) - this_cutoff;
		} else {
			read_bits = this_num_bits - 1;
			base = this_counter - this_cutoff;
		}
		base = base << read_bits;
		assert(base < mask);
		table[i] = read_bits | sym << 5 | base << 11;
	}
	dump_dectable(log, table);
}
