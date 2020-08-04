/* SPDX-License-Identifier: CC0-1.0 */
#include "zstd_internal.h"
#include "compression.h"
#include <inttypes.h>
#include <assert.h>

const u8 *zstd_decode_tree_description(const u8 *in, const u8 *end, struct dectables *tables) {
	check(end - in >= 1, "not enough data for Huffman tree header\n");
	u32 sum = 0, maxbits = 0;
	u8 weights[256];
	u8 header = *in++, num;
	if (header < 128) {
		debug("FSE huff weights\n");
		u32 fse_weights[12];
		const u8 *weights_end = in + header, *huff_start = weights_end;
		u8 fse_log;
		in = decode_fse_distribution(in, end, &fse_log, ARRAY_SIZE(fse_weights), fse_weights);
		if (!in) {return 0;}
		check(fse_log <= 6, "accuracy log of %"PRIu8" higher than allowed by spec\n", fse_log);
		u32 fse_table[64];
		build_fse_table(fse_log, ARRAY_SIZE(fse_weights), fse_weights, fse_table);
		check(end - in >= header, "not enough data for FSE-compressed Huffman weights\n");
		check(header > 2, "not enough data to initialize Huffman weight states\n");
		u32 bits = *--weights_end << 8;
		bits |= *--weights_end;
		check(bits >= 0x100, "last byte is 0\n");
		u32 num_bits = __builtin_clz(1) - __builtin_clz(bits);
		if (num_bits < 2*fse_log) {
			check(in < weights_end, "not enough data to initialize Huffman weight states\n");
			bits = bits << 8 | *--weights_end;
			num_bits += 8;
			assert(num_bits > 2 * fse_log);
		}
		debug("0x%"PRIx32"/%"PRIu8"\n", bits, num_bits);
		u32 entry_even, entry_odd;
#define ADD_WEIGHT(entry) do {\
			sum += 1 << (weights[num++] = entry >> 5 & 63) >> 1;\
			spew("weight %"PRIu8": %"PRIu8" sum%"PRIu32"\n", num - 1, weights[num - 1], sum);\
		} while (0)
#define UPDATE_ENTRY(entry) (entry = fse_table[(bits >> (num_bits -= fse_bits(entry)) & MASK31(fse_bits(entry))) + fse_base(entry)])
		num = 0;
		{
			u8 state_even = bits >> (num_bits -= fse_log) & MASK31(fse_log);
			u8 state_odd = bits >> (num_bits -= fse_log) & MASK31(fse_log);
			assert(state_even < (1 << fse_log) && state_odd < (1 << fse_log));
			entry_even = fse_table[state_even];
			entry_odd = fse_table[state_odd];
		}
		while (1) {
			while (num_bits <= 24 && in < weights_end) {
				bits = bits << 8 | *--weights_end;
				num_bits += 8;
			}
			spew("0x%"PRIx32"/%"PRIu8"\n", bits, num_bits);
			if (num_bits <= 24) {break;}
			ADD_WEIGHT(entry_even);
			ADD_WEIGHT(entry_odd);
			check(num < 254, "Huffman weight bitstream is too long\n");
			UPDATE_ENTRY(entry_even);
			UPDATE_ENTRY(entry_odd);
		}
		while (1) {
			check(num <= 253, "too many huffman weights\n");
			ADD_WEIGHT(entry_even);
			if (num_bits < fse_bits(entry_even)) {
				ADD_WEIGHT(entry_odd);
				break;
			}
			UPDATE_ENTRY(entry_even);

			check(num <= 253, "too many huffman weights\n");
			ADD_WEIGHT(entry_odd);
			if (num_bits < fse_bits(entry_odd)) {
				ADD_WEIGHT(entry_even);
				break;
			}
			UPDATE_ENTRY(entry_odd);
		}
		in = huff_start;
	} else {
		debug("direct huff weights\n");
		num = header - 127;
		check(end - in >= (num + 1) / 2, "not enough data for direct Huffman weights\n");
		for_range(i, 0, num / 2) {
			u8 byte = *in++, w1 = byte >> 4, w2 = byte & 15;
			weights[2 *i] = w1;
			sum += 1 << w1 >> 1;
			weights[2 * i + 1] = w2;
			sum += 1 << w2 >> 1;
		}
		if (num & 1) {
			u8 w = weights[num - 1] = *in++ >> 4;
			sum += 1 << w >> 1; 
		}
	}
	check(sum, "non-RLE literal blocks must use at least two symbols\n");
	u32 lastweight = __builtin_ctz(sum) + 1;
	sum += 1 << lastweight >> 1;
	debug("adding implied weight %"PRIu8", now sum=%"PRIu32"\n", lastweight, sum);
	weights[num] = lastweight;
	for_range(i, (u32)num + 1, 256) {weights[i] = 0;}
	maxbits = lastweight + 1;
	while (sum > (1 << maxbits)) {maxbits += 1;}
	check(sum == 1 << maxbits, "Huffman tree not complete\n");
	debug("tree depth=%"PRIu8"\n", maxbits);
	check(maxbits <= 11, "Huffman tree is more than 11-deep\n");
	u8 order[256];
	for_array(i, order) {
		u8 w = weights[i];
		u8 j = i;
		if (i > 0) {
			do {
				if (weights[j - 1] <= w) {
					break;
				}
				weights[j] = weights[j - 1];
				order[j] = order[j - 1];
			} while (--j);
			weights[j] = w;
		}
		order[j] = i;
	}
	tables->huff_depth = maxbits;
	u16 code = 0;
	for_array(i, order) {
		u8 sym = order[i];
		u8 weight = weights[i];
		if (!weight) {continue;}
		u8 len = maxbits + 1 - weight;
		u16 num_entries = 1 << (11 - len);
		spew("sym0x%02"PRIx8" weight%"PRIu8" len%"PRIu8" %"PRIu16"entries code0x%03"PRIx16"\n", sym, weight, len, num_entries, code<<1);
		assert(code % num_entries == 0);
		for_range(j, 0, num_entries) {
			assert(code < 2048);
			tables->huff_table[code].sym = sym;
			tables->huff_table[code++].len = len;
		}
	}
	assert(code == 2048);
	return in;
}

_Bool zstd_decompress_literal_stream(const u8 *in, const u8 *end, u8 *out, u8 *out_end, const struct huff_entry *table) {
	assert(out < out_end);
	check(end > in, "literal stream is empty");
	u8 last_byte = *--end;
	check(last_byte != 0, "last byte is 0\n");
	u8 num_bits = __builtin_clz(1) - __builtin_clz(last_byte);
	u8 shift = 32 - num_bits;
	u32 bits = num_bits ? last_byte << (32 - num_bits) : 0; /* put the bits in most-significant position */
	debug("0x%08"PRIx32"/%"PRIu8"\n", bits, num_bits);
	do {
		const struct huff_entry *entry;
		u8 len;
		while ((len = (entry = table + (bits >> 21))->len) > (32 - shift)) {
			spew("0x%08"PRIx32"/%"PRIu8"\n", bits, 32 - shift);
			check(in < end, "not enough data to decode literal stream with %zu left\n", out_end - out);
			do {
				bits |= *--end << (shift -= 8);
			} while (shift >= 8 && in < end);
		}
		spew("lit 0x%08"PRIx32"/%"PRIu8" sym%02"PRIx8"\n", bits, 32 - shift, entry->sym);
		shift += len; bits <<= len;
		*out++ = entry->sym;
	} while (out < out_end);
	return out;
}

_Bool zstd_decode_huff_4streams(const u8 *in, const u8 *end, u8 *out, u8 *out_end, const struct huff_entry *table) {
	check(end - in >= 10, "not enough data for 4 Huffman streams\n");
	u16 size1 = in[0] | (u16)in[1] << 8, size2 = in[2] | (u16)in[3] << 8, size3 = in[4] | (u16)in[5] << 8;
	in += 6;
	debug("sizes %"PRIu16" %"PRIu16" %"PRIu16"\n", size1, size2, size3);
	check((u32)size1 + size2 + size3 + 1 <= end - in, "4 Huffman streams go beyond input\n");
	u32 regen_size = out_end - out;
	u32 decomp_size123 = (regen_size + 3) / 4;
	if (!zstd_decompress_literal_stream(in, in + size1, out, out + decomp_size123, table)) {return 0;}
	out += decomp_size123;
	in += size1;
	debug("stream 2\n");
	if (!zstd_decompress_literal_stream(in, in + size2, out, out + decomp_size123, table)) {return 0;}
	out += decomp_size123;
	in += size2;
	debug("stream 3\n");
	if (!zstd_decompress_literal_stream(in, in + size3, out, out + decomp_size123, table)) {return 0;}
	out += decomp_size123;
	in += size3;
	debug("stream 4\n");
	return zstd_decompress_literal_stream(in, end, out, out_end, table);
}
