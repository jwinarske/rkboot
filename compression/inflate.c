/* SPDX-License-Identifier: CC0-1.0 */
#include "compression.h"
#include "../include/log.h"
#include <assert.h>
#include <string.h>
#include <inttypes.h>

static u32 crc32_byte(u32 input, u32 poly) {
	for_range(i, 0, 8) {
		input = (input >> 1) ^ (input & 1 ? poly : 0);
	}
	return input;
}

typedef u32 bits_t;
enum {GUARANTEED_BITS = 25};
#if __aarch64__ && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define LDU32LEU(var, addr) __asm__("ldr %w0, [%1]" : "=r"(var) : "r"(addr))
#define LDU32LEU_END(var, addr) __asm__("ldr %w0, [%1, #-4]" : "=r"(var) : "r"(addr))
#endif
#ifndef LDU32LEU_END
#error "no unaligned 32-bit little-endian load sequence available"
#endif

#define check(expr, ...) if (unlikely(!(expr))) {info(__VA_ARGS__);return 0;}
enum {UNIMPLEMENTED = 0};

enum {
	BFINAL = 1,
	BTYPE_UNCOMPRESSED = 0,
	BTYPE_FIXED = 1,
	BTYPE_DYNAMIC = 2,
};

#define CONSUME_BYTE(context) check(ptr < end, "no more bits to read "context"\n");\
			bits |= *ptr++ << num_bits;\
			num_bits += 8;\
			info("bits 0x%x/%u @ %u\n", bits, (unsigned)num_bits, (unsigned)(ptr - *in))

void construct_codes(u16 num_symbols, const u8 *lengths, u16 *codes) {
	u16 active_syms = 0;
	for_range(sym, 0, num_symbols) {
		if (lengths[sym]) {
			active_syms += 1;
		} else {
			codes[sym] = 0;
		}
	}
	u16 code = 0;
	u16 codes_allocated = 0;
	for_range(len, 1, 16) {
		for_range(sym, 0, num_symbols) {
			if (lengths[sym] != len) {continue;}
			codes[sym] = code;
			codes_allocated += 1;
			u8 pos = len - 1;
			while (code & 1 << pos) {
				code &= ~(1 << pos);
				if (!pos) {goto end;}
				pos -= 1;
			}
			code |= 1 << pos;
		}
	} end:;
	assert(active_syms == codes_allocated);
}

void construct_dectable(u16 num_symbols, const u8 *lengths, const u16 *codes, u16 *dectable, u16 *overlength_offsets, u32 *overlength_symbols) {
	u16 overlength_count[4];
	for_array(i, overlength_count) {overlength_count[i] = 0;}
	for_range(i, 0, 2048) {dectable[i] = 10;}
	for_range(sym, 0, num_symbols) {
		u8 len = lengths[sym];
		if (!len) {continue;}
		u16 code = codes[sym];
		if (len <= 11) {
			for_range(i, 0, 2048 >> len) {
				dectable[i << len | code] = len | sym << 4;
			}
		} else {
			assert(len <= 15);
			overlength_count[len - 12] += 1;
			dectable[code & 0x7ff] = 12;
		}
	}
	u16 off = 0;
	debug("counts: ");
	for_array(i, overlength_count) {debug(" %u", (unsigned)overlength_count[i]);}
	debug("\n");
	for_range(len, 0, 4) {
		overlength_offsets[len] = off;
		off += overlength_count[len];
		overlength_count[len] = 0;
	}
	overlength_offsets[4] = off;
	debug("offsets: ");
	for_range(i, 0, 5) {debug(" %u", (unsigned)overlength_offsets[i]);}
	debug("\n");
	for_range(sym, 0, num_symbols) {
		u8 len = lengths[sym];
		if (len < 12) {continue;}
		u16 idx = overlength_count[len - 12]++ + overlength_offsets[len - 12];
		debug("sym%u len%u idx%u code %x\n", (unsigned)sym, (unsigned)len, (unsigned)idx, (unsigned)codes[sym]);
		assert(idx < overlength_offsets[len - 11]);
		overlength_symbols[idx] = sym | codes[sym] << 9;
	}
	for_array(len, overlength_count) {
		assert(overlength_offsets[len] + overlength_count[len] == overlength_offsets[len + 1]);
	}
}

struct huffman_state {
	const u8 *ptr;
	u32 bits, val;
	u8 num_bits;
};

#define REPORT(ctx) spew(ctx ": 0x%x/%u\n", huff.bits, (unsigned)huff.num_bits)

static inline struct huffman_state require_bits(struct huffman_state huff, const u8 *end) {
	while (huff.num_bits < huff.val) {
		if (huff.ptr >= end) {return huff;}
		huff.bits |= *huff.ptr++ << huff.num_bits;
		huff.num_bits += 8;
		REPORT("req");
	}
	return huff;
}

#define ERROR_CODES \
	X(OUT_OF_DATA, "out of data")\
	X(FIRST_REP, "first length code is a repetition")\
	X(REP_TOO_LONG, "repetition of length codes beyond number of used codes")\
	X(FILL_TOO_LONG, "0-fill beyond number of used codes")\
	X(CODE_UNASSIGNED, "code unassigned")

enum error_codes {
	NO_ERROR = 0,
#define X(a, b) ERR_##a,
	ERROR_CODES
#undef X
	NUM_ERR_CODES
};

const char *const error_msg[NUM_ERR_CODES] = {
	"no error",
#define X(a, b) b,
	ERROR_CODES
#undef X
};

#define SHIFT(n) do {huff.bits >>= (n); huff.num_bits -= (n);} while (0)
static inline struct huffman_state huff_with_extra(struct huffman_state huff, const u8 *end, const u16 *dectable, const u8 *extra_bit_table, const u16 *baseline_table, const u16 *overlength_offsets, const u16 *overlength_symbols, const u16 *codes) {
	REPORT("sym");
	u16 len, val;
	while ((len = (val = dectable[huff.bits & 0x7ff]) & 0xf) > huff.num_bits) {
		if (huff.ptr >= end) {
			huff.ptr = 0;
			huff.val = ERR_OUT_OF_DATA;
			return huff;
		}
		huff.bits |= *huff.ptr++ << huff.num_bits;
		huff.num_bits += 8;
		REPORT("code");
	}
	u16 sym;
	if (len < 12) {
		SHIFT(len);
		sym = val >> 4;
	} else {
		for_range(len, 12, 16) {
			if (len > huff.num_bits) {
				if (huff.ptr >= end) {
					huff.ptr = 0;
					huff.val = ERR_OUT_OF_DATA;
					return huff;
				}
				huff.bits |= *huff.ptr++ << huff.num_bits;
				huff.num_bits += 8;
				REPORT("overlength code");
			}
			u32 suffix = huff.bits & ((1 << len) - 1);
			for_range(i, overlength_offsets[len - 12], overlength_offsets[len - 11]) {
				u32 x = overlength_symbols[i];
				sym = x & 0x1ff;
				if ((x >> 9) == suffix) {
					SHIFT(len);
					goto found;
				}
			}
		}
		debug("code not assigned");
		huff.ptr = 0;
		huff.val = ERR_CODE_UNASSIGNED;
		return huff;
	} found:;
	if (sym < huff.val) {
		huff.val = sym;
		return huff;
	}
	u32 idx = sym - huff.val;
	u8 extra = extra_bit_table[idx];
	u16 base = baseline_table[idx];
	spew("sym%u extra%u base%u\n", sym, (unsigned)extra, (unsigned)base);
	while (huff.num_bits < extra) {
		if (huff.ptr >= end) {
			huff.ptr = 0;
			huff.val = ERR_OUT_OF_DATA;
			return huff;
		}
		huff.bits |= *huff.ptr++ << huff.num_bits;
		huff.num_bits += 8;
		REPORT("extra");
	}
	huff.val += base + (huff.bits & ((1 << extra) - 1));
	SHIFT(extra);
	spew("val%u\n", huff.val);
	return huff;
}

static struct huffman_state decode_lengths(struct huffman_state huff, const u8 *end, u16 *dectable, u8 *lengths) {
	u32 num_symbols = huff.val;
	static const u8 clength_extra[3] = {2, 3, 7};
	enum {REP_BASE = 16, FILL_BASE = REP_BASE + 4};
	static const u16 clength_base[3] = {0, 4, 12};
	for_range(sym, 0, num_symbols) {
		huff.val = REP_BASE;
		huff = huff_with_extra(huff, end, dectable, clength_extra, clength_base, 0, 0, 0);
		if (!huff.ptr) {return huff;}
		u32 length_code = huff.val;
		if (length_code < REP_BASE) {
			lengths[sym] = length_code;
			debug("sym %u %c len %u\n", sym, (sym >= 0x20 && sym <= 0x7e ? (char) sym : '.'), length_code);
		} else if (length_code < FILL_BASE) {
			u32 rep = length_code - REP_BASE + 3;
			if (sym == 0) {
				huff.val = ERR_FIRST_REP;
				return huff;
			}
			u32 end = sym + rep;
			if (end > num_symbols) {
				huff.val = ERR_REP_TOO_LONG;
				return huff;
			}
			debug("rep %u\n", rep);
			while (sym < end) {
				lengths[sym] = lengths[sym - 1];
				sym += 1;
			}
			sym -= 1;
		} else {
			u32 rep = length_code - FILL_BASE + 3;
			u32 end = sym + rep;
			if (end > num_symbols) {
				huff.val = ERR_FILL_TOO_LONG;
				return huff;
			}
			debug("0fill %u\n", rep);
			while (sym < end) {lengths[sym++] = 0;}
			sym -= 1;
		}
	}
	huff.val = NO_ERROR;
	return huff;
}

static const u8 lit_extra[21] = {
	1, 1, 1, 1,
	2, 2, 2, 2,
	3, 3, 3, 3,
	4, 4, 4, 4,
	5, 5, 5, 5,
	0
};
static const u8 dist_extra[26] = {
	1, 1, 2, 2, 3, 3,
	4, 4, 5, 5, 6, 6,
	7, 7, 8, 8, 9, 9,
	10, 10, 11, 11, 12, 12,
	13, 13
};

enum {
	FTEXT = 1,
	FHCRC = 2,
	FEXTRA = 4,
	FNAME = 8,
	FCOMMENT = 16,
	FRESERVED_MASK = 0xe0
};

static enum compr_probe_status probe(const u8 *in, const u8 *end, size_t UNUSED *size) {
	if (end - in < 2) {return COMPR_PROBE_NOT_ENOUGH_DATA;}
	if (in[0] != 31 || in[1] != 139) {return COMPR_PROBE_WRONG_MAGIC;}
	if (end - in < 18) {return COMPR_PROBE_NOT_ENOUGH_DATA;}
	if (in[2] != 8) {
		info("unknown gzip compression format %u, only know 8 (deflate)\n", in[2]);
		return COMPR_PROBE_RESERVED_FEATURE;
	}
	u8 flags = in[3];
	const u8 *content = in + 10;
	if (flags & FRESERVED_MASK) {
		info("reserved flags used\n");
		return COMPR_PROBE_RESERVED_FEATURE;
	}
	if (flags & FEXTRA) {
		u16 length = content[0] | (u16)content[1] << 8;
		content += 2;
		if (end - content < length) {return COMPR_PROBE_NOT_ENOUGH_DATA;}
		content += length;
	}
	if (flags & FNAME) {
		content += strnlen((const char *)content, end - content);
		if (end <= content) {return COMPR_PROBE_NOT_ENOUGH_DATA;}
		content += 1;
	}
	if (flags & FCOMMENT) {
		content += strnlen((const char *)content, end - content);
		if (end <= content) {return COMPR_PROBE_NOT_ENOUGH_DATA;}
		content += 1;
	}
	if (flags & FHCRC) {
		if (end - content < 2) {return COMPR_PROBE_NOT_ENOUGH_DATA;}
	}
	return COMPR_PROBE_SIZE_UNKNOWN;
}

struct gzip_dec_state {
	struct decompressor_state st;
	u32 crc_table[256];

	u32 crc;
	u32 isize;
	u32 bits;
	u8 num_bits;
	_Bool last_block;
	u8 lit_lengths[286], dist_lengths[30];
	u16 lit_codes[286], dist_codes[30];
	u32 lit_overlength[286], dist_overlength[30];
	u16 lit_oloff[5], dist_oloff[5];
	u16 lit_base[21], dist_base[26];

	u16 dectable_dist[2048], dectable_lit[2048];
};

static size_t trailer(struct decompressor_state *state, const u8 *in, const u8 *end) {
	struct gzip_dec_state *st = (struct gzip_dec_state *)state;

	if (unlikely(end - in < 8)) {return DECODE_NEED_MORE_DATA;}
	u32 crc_read = in[0] | (u32)in[1] << 8 | (u32)in[2] << 16 | (u32)in[3] << 24;
	u32 crc = st->crc ^ 0xffffffff;
	check(crc_read == crc, "content CRC mismatch: read %08x, computed %08x\n", crc_read, crc);
	info("CRC32: %08x\n", crc);
	in += 4;
	u32 isize = in[0] | (u32)in[1] << 8 | (u32)in[2] << 16 | (u32)in[3] << 24;
	in += 4;
	check(st->isize == isize, "compressed size mismatch, read %08x, decompressed %08x (mod 1 << 32)\n", isize, st->isize);
	st->st.decode = 0;
	return NUM_DECODE_STATUS + 8;
}

static decompress_func block_start;

static size_t raw_block(struct decompressor_state *state, const u8 *in, const u8 *end) {
	struct gzip_dec_state *st = (struct gzip_dec_state *)state;

	debug("stored block\n");
	if (unlikely(end - in < 4)) {return DECODE_NEED_MORE_DATA;}
	u16 len = in[0] | (u32)in[1] << 8;
	u16 nlen = in[2] | (u32)in[3] << 8;
	check(len == (nlen ^ 0xffff), "length 0x%04x is not ~0x%04x\n", (unsigned)len, (unsigned)nlen);
	in += 4;
	if (unlikely(end - in < len)) {return DECODE_NEED_MORE_DATA;}
	end = in + len;
	u8 *out = st->st.out;
	if (st->st.out_end - out < len) {return DECODE_NEED_MORE_SPACE;}
	u32 crc = st->crc;
	while (in < end) {
		u8 c = *out++ = *in++;
		crc = crc >> 8 ^ st->crc_table[(u8)crc ^ c];
	}
	st->st.decode = !st->last_block ?  block_start : trailer;
	st->st.out = out;
	st->crc = crc;
	st->isize += len;
	return NUM_DECODE_STATUS + 4 + len;
}

#define REFILL_LOOP do {\
	spew("0x%"PRIx32"/%"PRIu8" refill\n", bits, num_bits);\
	while (num_bits < GUARANTEED_BITS && ptr < end) {\
		bits |= *ptr++ << num_bits;\
		num_bits += 8;\
	}\
	spew("0x%"PRIx32"/%"PRIu8"\n", bits, num_bits);\
} while (0)
//#undef LDU32LEU_END
#ifdef LDU32LEU_END
#define REFILL do {\
	u8 read_bits = 32 - num_bits, read_bytes = read_bits / 8;\
	if (unlikely(ptr == end)) {break;}\
	read_bytes = end - ptr >= read_bytes ? read_bytes : end - ptr;\
	spew("0x%"PRIx32"/%"PRIu8" read %"PRIu8" bytes\n", bits, num_bits, read_bytes);\
	num_bits += 8*read_bytes;\
	ptr += read_bytes;\
	LDU32LEU_END(bits, ptr);\
	bits >>= 32 - num_bits;\
	spew("0x%"PRIx32"/%"PRIu8"\n", bits, num_bits);\
} while (0)
#else
#define REFILL REFILL_LOOP
#endif

static size_t huff_block(struct decompressor_state *state, const u8 *in, const u8 *end) {
	struct gzip_dec_state *st = (struct gzip_dec_state *)state;
	if (unlikely(end - in < 4)) {return DECODE_NEED_MORE_DATA;}
	const u8 *ptr = in;
	bits_t bits = st->bits;
	u8 num_bits = st->num_bits;
	assert(num_bits < 8);
	u8 *out = st->st.out, *out_start = out, *out_end = st->st.out_end;
	const u8 *ptr_save;
	u32 crc = st->crc, bits_save;
	u8 num_bits_save;
	size_t res;
	REFILL_LOOP;
	debug("huffman block\n");

	while (1) {
		ptr_save = ptr; bits_save = bits; num_bits_save = num_bits;
		_Static_assert(GUARANTEED_BITS >= 15 + 5, "bits container too small");
		u16 val = st->dectable_lit[bits & 0x7ff], len = val & 0xf;
		u32 lit;
		if (len > 11) {
			len = 12;
			while (1) {
				if (unlikely(len > num_bits)) {
					res = DECODE_NEED_MORE_DATA;
					goto interrupted;
				}
				bits_t mask = ((bits_t)1 << len) - 1, suffix = bits & mask;
				for_range(i, st->lit_oloff[len - 12], st->lit_oloff[len - 11]) {
					u32 x = st->lit_overlength[i];
					lit = x & 0x1ff;
					if ((x >> 9) == suffix) {
						goto found;
					}
				}
				if (unlikely(++len == 4)) {return DECODE_ERR;}
			}found:;
		} else if (unlikely(len > num_bits)) {
			res = DECODE_NEED_MORE_DATA;
			goto interrupted;
		} else {
			lit = val >> 4;
		}
		bits >>= len; num_bits -= len;
		if (lit < 256) {
			spew("literal 0x%02x %c\n", lit, lit >= 0x20 && lit < 0x7f ? (char)lit : '.');
			if (unlikely(out >= out_end)) {
				res = DECODE_NEED_MORE_SPACE;
				goto interrupted;
			}
			*out++ = lit;
			crc = crc >> 8 ^ st->crc_table[(u8)crc ^ lit];
		} else if (lit > 256) {
			u32 length = lit - 257 + 3;
			if (length >= 11) {	/* TODO: fold this into the decoding table */
				u8 num_extra = lit_extra[length - 11];
				spew("%"PRIu8" extra bits\n", num_extra);
				length = st->lit_base[length - 11] + (bits & ((1 << num_extra) - 1));
				if (unlikely(num_extra > num_bits)) {
					res = DECODE_NEED_MORE_DATA;
					goto interrupted;
				}
				bits >>= num_extra; num_bits -= num_extra;
			}
			spew("match length=%u ", length);
			REFILL;

			_Static_assert(GUARANTEED_BITS >= 15, "Bit container is not big enough");
			u16 val = st->dectable_dist[bits & 0x7ff], len = val & 0xf;
			u32 sym;
			if (len > 11) {
				len = 12;
				while (1) {
					if (unlikely(len > num_bits)) {
						res = DECODE_NEED_MORE_DATA;
						goto interrupted;
					}
					bits_t mask = ((bits_t)1 << len) - 1, suffix = bits & mask;
					for_range(i, st->dist_oloff[len - 12], st->dist_oloff[len - 11]) {
						u32 x = st->dist_overlength[i];
						sym = x & 0x1ff;
						if ((x >> 9) == suffix) {
							goto found2;
						}
					}
					if (unlikely(++len == 4)) {return DECODE_ERR;}
				}found2:;
			} else if (unlikely(len > num_bits)) {
				res = DECODE_NEED_MORE_DATA;
				goto interrupted;
			} else {
				sym = val >> 4;
			}
			bits >>= len; num_bits -= len;
			u32 dist = sym + 1;
			if (dist >= 5) {	/* TODO: fold this into the decoding table */
				REFILL;
				u8 num_extra = dist_extra[dist - 5];
				spew("%"PRIu8" extra bits\n", num_extra);
				dist = st->dist_base[dist - 5] + (bits & ((1 << num_extra) - 1));
				if (unlikely(num_extra > num_bits)) {
					res = DECODE_NEED_MORE_DATA;
					goto interrupted;
				}
				bits >>= num_extra; num_bits -= num_extra;
			}
			check(dist <= out - st->st.window_start, "match distance of %u beyond start of buffer", dist);
			if (length >= 258) {
				check(length != 258, "file used literal/length symbol 284 with 5 1-bits, which is specced invalid (without it being specially noted no less, WTF)\n");
				length = 258;
			}
			spew(" dist=%"PRIu32" %.*s…\n", dist, length < dist ? (int)length : (int)dist, out - dist);
			if (unlikely(out_end - out < length)) {
				res = DECODE_NEED_MORE_SPACE;
				goto interrupted;
			}
			do {
				u8 c = *out = *(out - dist);
				out += 1;
				crc = crc >> 8 ^ st->crc_table[(u8)crc ^ c];
			} while (--length);
			if (out - st->st.window_start > 100) {
				spew("%.100s\n", out - 100);
			} else {
				spew("%.*s\n", (int)(out - st->st.window_start), st->st.window_start);
			}
		} else {
			st->st.decode = !st->last_block ? block_start : trailer;
			st->num_bits = num_bits % 8;
			st->bits = bits;
			res = NUM_DECODE_STATUS + (ptr - in - num_bits / 8);
			goto end;
		}
		REFILL;
	}
	interrupted:;
	if (out > out_start) {res = NUM_DECODE_STATUS + (ptr_save - in - num_bits_save / 8);}
	st->num_bits = num_bits_save % 8;
	st->bits = bits_save;
	end:;
	st->st.out = out;
	st->isize += out - out_start;
	st->crc = crc;
	assert(st->num_bits < 8);
	if (out - st->st.window_start > 32768) {
		st->st.window_start = out - 32768;
	}
	return res;
}

static size_t block_start(struct decompressor_state *state, const u8 *in, const u8 *end) {
	struct gzip_dec_state *st = (struct gzip_dec_state *)state;
	bits_t bits = st->bits;
	u8 num_bits = st->num_bits;
	if (unlikely(end - in < 4)) {return DECODE_NEED_MORE_DATA;}
	const u8 *ptr = in;

	assert(num_bits < 8);
	debug("block header: 0x%"PRIx32"/%"PRIu8"\n", bits & ((1 << num_bits) - 1), num_bits);
	REFILL_LOOP;
	_Static_assert(GUARANTEED_BITS >= 3, "bit container too small");
	if (unlikely(num_bits < 3)) {return DECODE_NEED_MORE_DATA;}
	u8 block_header = bits & 7;
	bits >>= 3; num_bits -= 3;
	debug("block header %u\n", (unsigned)block_header);
	st->last_block = block_header & 1;
	if ((block_header >> 1) == BTYPE_UNCOMPRESSED) {
		st->st.decode = raw_block;
		st->bits = 0;
		st->num_bits = 0;
		return NUM_DECODE_STATUS + (ptr - in - num_bits / 8);
	} else {
		check((block_header >> 1) <= 2, "reserved block type used\n");
		u32 hlit, UNUSED hdist;
		if ((block_header >> 1) == 2) {
			u8 lengths2[19];
			u16 length_codes[19];
			debug("dynamic block\n");
			REFILL;
			_Static_assert(GUARANTEED_BITS >= 17, "bit container too small");
			if (unlikely(num_bits < 14)) {return DECODE_NEED_MORE_DATA;}
			hlit = bits & 31;
			hdist = bits >> 5 & 31;
			u32 hclen = bits >> 10 & 15;
			bits >>= 14; num_bits -= 14;
			hclen += 4;
			hlit += 257;
			hdist += 1;
			debug("hclen%u hlit%u hdist%u\n", hclen, hlit, hdist);
			check(hlit <= 286, "literal alphabet too large\n");
			check(hdist <= 30, "distance alphabet too large\n");
			static const u8 hclen_order[19] = {
				16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
			};
			for_range(i, 0, hclen) {
				REFILL;
				if (unlikely(num_bits < 3)) {return DECODE_NEED_MORE_DATA;}
				u32 code = bits & 7;
				debug("length code length %u\n", code);
				lengths2[hclen_order[i]] = code;
				bits >>= 3; num_bits -= 3;
			}
			for_range(i, hclen, 19) {lengths2[hclen_order[i]] = 0;}
			construct_codes(19, lengths2, length_codes);
			for_array(i, length_codes) {
				debug("code length code %u length: %u, code %x\n", i, (unsigned)lengths2[i], (unsigned)length_codes[i]);
			}
			construct_dectable(19, lengths2, length_codes, st->dectable_dist, st->dist_oloff, st->dist_overlength);
			/*for (size_t i = 0; i < 512; i += 4) {
				debug("%04x %04x %04x %04x\n", dectable_dist[i], dectable_dist[i+1], dectable_dist[i+2], dectable_dist[i+3]);
			}*/
			struct huffman_state huff = {.bits = bits, .ptr = ptr, .num_bits = num_bits};
			huff.val = hlit;
			huff = decode_lengths(huff, end, st->dectable_dist, st->lit_lengths);
			if (unlikely(!huff.ptr)) {return DECODE_NEED_MORE_DATA;}
			check(huff.val == NO_ERROR, "%s", error_msg[huff.val]);
			for_range(lit, hlit, 286) {st->lit_lengths[lit] = 0;}
			huff.val = hdist;
			huff = decode_lengths(huff, end, st->dectable_dist, st->dist_lengths);
			if (unlikely(!huff.ptr)) {return DECODE_NEED_MORE_DATA;}
			check(huff.val == NO_ERROR, "%s\n", error_msg[huff.val]);
			ptr = huff.ptr;
			num_bits = huff.num_bits;
			bits = huff.bits;
			for_range(dist, hdist, 30) {st->dist_lengths[dist] = 0;}
			construct_codes(286, st->lit_lengths, st->lit_codes);
			construct_dectable(286, st->lit_lengths, st->lit_codes, st->dectable_lit, st->lit_oloff, st->lit_overlength);
			construct_codes(30, st->dist_lengths, st->dist_codes);
			construct_dectable(30, st->dist_lengths, st->dist_codes, st->dectable_dist, st->dist_oloff, st->dist_overlength);
		} else {
			info("fixed huffman block\n");
			static const u8 bitrev_nibble[16] = {
				0x0, 0x8, 0x4, 0xc,
				0x2, 0xa, 0x6, 0xe,
				0x1, 0x9, 0x5, 0xd,
				0x3, 0xb, 0x7, 0xf,
			};
			for_range(i, 0, 512) {
				u16 rev = (i << 8 & 0x100) | bitrev_nibble[i >> 1 & 15] << 4 | bitrev_nibble[i >> 5 & 15];
				st->dectable_lit[i] = rev < 0x5f ? ((rev >> 2) + 256) << 4 | 7
					: rev < 0x17f ? ((rev >> 1) - 0x30) << 4 | 8
					: rev < 0x18b ? ((rev >> 1) - 0xc0 + 280) << 4 | 8
					: rev < 0x18f ? 10
					: (rev - 0x190 + 144) << 4 | 9;
			}
			for_array(i, st->lit_oloff) {st->lit_oloff[i] = 0;}
			assert(st->dectable_lit[0] == 0x1007);
			assert(st->dectable_lit[0xc] == 0x8);
			info("%x\n", (unsigned)st->dectable_lit[3]);
			assert(st->dectable_lit[0x3] == (280 << 4 | 8));
			assert(st->dectable_lit[0x13] == 0x909);

			for_range(i, 0, 512) {
				u16 rev = (i << 4 & 0x10) | bitrev_nibble[i >> 1 & 15];
				st->dectable_dist[i] = rev < 30 ? rev << 4 | 5 : 10;
			}
			for_array(i, st->dist_oloff) {st->dist_oloff[i] = 0;}
		}
		st->st.decode = huff_block;
		st->bits = bits;
		st->num_bits = num_bits % 8;
		return NUM_DECODE_STATUS + (ptr - in - num_bits / 8);
	}
}

static const u8 *init(struct decompressor_state *state, const u8 *in, const u8 *end) {
	assert(end - in >= 18);
	struct gzip_dec_state *st = (struct gzip_dec_state *)state;
	u32 poly = 0xedb88320;
	for_array(i, st->crc_table) {
		st->crc_table[i] = crc32_byte(i, poly);
	}
	u8 flags = in[3];
	info("decompressing gzip, flags: 0x%x\n", (unsigned)flags);
	const u8 *content = in + 10;
	if (flags & FEXTRA) {
		u16 length = content[0] | (u16)content[1] << 8;
		content += 2;
		assert(end - content <= length);
		content += length;
	}
	if (flags & FNAME) {
		info("filename: %s\n", content);
		content += strnlen((const char *)content, end - content);
		assert(content < end);
		content += 1;
	}
	if (flags & FCOMMENT) {
		info("comment: %s\n", content);
		content += strnlen((const char *)content, end - content);
		assert(content < end);
		content += 1;
	}
	if (flags & FHCRC) {
		check(end - content >= 2, "input not long enough for header CRC\n");
		u16 hcrc = content[0] | (u16)content[1] << 8;
		const u8 *ptr = in;
		u32 hcrc_comp = ~(u32)0;
		do {
			hcrc_comp = hcrc_comp >> 8 ^ st->crc_table[(u8)hcrc_comp ^ *ptr];
		} while (++ptr < content);
		check((u16)hcrc_comp == hcrc, "header CRC mismatch, read %04x, computed %04x\n", (unsigned)hcrc, (unsigned)(u16)hcrc_comp);
		content += 2;
	}
	check(end - content > 8, "gzip input not long enough (%zu (%zx) bytes after header) for compressed stream and trailer\n", end - content, end - content);
	u16 base = 11;
	debug("lit_base: ");
	for_array(i, st->lit_base) {
		debug(" %"PRIu16, base + 11);
		st->lit_base[i] = base;
		base += 1 << lit_extra[i];
	}
	debug("\n");
	base = 5;
	for_array(i, st->dist_base) {
		st->dist_base[i] = base;
		base += 1 << dist_extra[i];
	}
	st->bits = 0;
	st->num_bits = 0;
	st->last_block = 0;
	st->isize = 0;
	st->crc = ~(u32)0;
	st->st.decode = block_start;
	return content;
}

const struct decompressor gzip_decompressor = {
	.probe = probe,
	.state_size = sizeof(struct gzip_dec_state),
	.init = init
};
