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

#define check(expr, ...) if (unlikely(!(expr))) {info(__VA_ARGS__);return 0;}

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

void construct_dectable(u16 num_symbols, const u8 *lengths, const u16 *codes, u16 *dectable, u16 *overlength_offsets, u16 *overlength_symbols) {
	u16 overlength_count[6];
	for_array(i, overlength_count) {overlength_count[i] = 0;}
	for_range(i, 0, 512) {dectable[i] = 10;}
	for_range(sym, 0, num_symbols) {
		u8 len = lengths[sym];
		if (!len) {continue;}
		u16 code = codes[sym];
		if (len <= 9) {
			for_range(i, 0, 512 >> len) {
				dectable[i << len | code] = len | sym << 4;
			}
		} else {
			assert(len <= 15);
			overlength_count[len - 10] += 1;
			dectable[code & 0x1ff] = 10;
		}
	}
	u16 off = 0;
	debug("counts: ");
	for_array(i, overlength_count) {debug(" %u", (unsigned)overlength_count[i]);}
	debug("\n");
	for_range(len, 0, 6) {
		overlength_offsets[len] = off;
		off += overlength_count[len];
		overlength_count[len] = 0;
	}
	overlength_offsets[6] = off;
	debug("offsets: ");
	for_range(i, 0, 7) {debug(" %u", (unsigned)overlength_offsets[i]);}
	debug("\n");
	for_range(sym, 0, num_symbols) {
		u8 len = lengths[sym];
		if (len < 10) {continue;}
		u16 idx = overlength_count[len - 10]++ + overlength_offsets[len - 10];
		debug("sym%u len%u idx%u code %x\n", (unsigned)sym, (unsigned)len, (unsigned)idx, (unsigned)codes[sym]);
		assert(idx < overlength_offsets[len - 9]);
		overlength_symbols[idx] = sym;
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

#define REPORT(ctx) debug(ctx ": 0x%x/%u\n", huff.bits, (unsigned)huff.num_bits)

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
	while ((len = (val = dectable[huff.bits & 0x1ff]) & 0xf) > huff.num_bits) {
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
	if (len < 10) {
		SHIFT(len);
		sym = val >> 4;
	} else {
		for_range(len, 10, 16) {
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
			for_range(i, overlength_offsets[len - 10], overlength_offsets[len - 9]) {
				sym = overlength_symbols[i];
				if (codes[sym] == suffix) {
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
	debug("sym%u extra%u base%u\n", sym, (unsigned)extra, (unsigned)base);
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
	debug("val%u\n", huff.val);
	return huff;
}

#define REQUIRE_BITS(n, context) do {huff.val = (n); huff = require_bits(huff, end); check(huff.num_bits >= (n), "no more bytes to read in " context);} while (0)

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

void lzcommon_match_copy(u8 *dest, u32 dist, u32 length);
void lzcommon_literal_copy(u8 *dest, const u8 *src, u32 length);

_Bool inflate(const u8 **in, const u8 *end, u8 **out_ptr, u8 *out_end, u32 *crc_ptr, const u32 *crc_table) {
	u32 crc = *crc_ptr;
	struct huffman_state huff;
	huff.ptr = *in;
	u8 *out = *out_ptr;
	huff.bits = *huff.ptr++;
	huff.num_bits = 8;
	u8 block_header;
	u8 lengths2[19];
	u16 length_codes[19];
	u16 dectable_dist[512], UNUSED dectable_lit[512];
	u8 lit_lengths[286], dist_lengths[30];
	u16 lit_codes[286], lit_overlength[286], dist_codes[30], dist_overlength[30];
	u16 lit_oloff[7], dist_oloff[7];
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
	u16 lit_base[21], dist_base[26];
	u16 base = 0;
	info("lit_base: ");
	for_array(i, lit_base) {
		debug(" %"PRIu16, base + 11);
		lit_base[i] = base;
		base += 1 << lit_extra[i];
	}
	info("\n");
	base = 0;
	for_array(i, dist_base) {
		dist_base[i] = base;
		base += 1 << dist_extra[i];
	}
	do {
		REQUIRE_BITS(3, "block header");
		block_header = huff.bits & 7;
		SHIFT(3);
		debug("block header %u\n", (unsigned)block_header);
		if ((block_header >> 1) == BTYPE_UNCOMPRESSED) {
			info("stored block\n");
			check(end - huff.ptr >= 4, "input not long enough to read LEN+NLEN\n");
			u16 len = huff.ptr[0] | (u32)huff.ptr[1] << 8;
			huff.ptr += 2;
			u16 nlen = huff.ptr[0] | (u32)huff.ptr[1] << 8;
			huff.ptr += 2;
			check(len == (nlen ^ 0xffff), "length 0x%04x is not ~0x%04x\n", (unsigned)len, (unsigned)nlen);
			huff.bits = 0;
			huff.num_bits = 0;
			check(out_end - out >= len, "not enough output buffer to copy stored block");
			check(end - huff.ptr >= len, "not enough input data to copy stored block");
			while (len--) {
				u8 c = *out++ = *huff.ptr++;
				crc = crc >> 8 ^ crc_table[(u8)crc ^ c];
			}
		} else {
			check((block_header >> 1) <= 2, "reserved block type used\n");
			u32 hlit, UNUSED hdist;
			if ((block_header >> 1) == 2) {
				info("dynamic block\n");
				REQUIRE_BITS(14, "dynamic block header");
				hlit = huff.bits & 31;
				hdist = huff.bits >> 5 & 31;
				u32 hclen = huff.bits >> 10 & 15;
				SHIFT(14);
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
					REQUIRE_BITS(3, "code length code length");
					u32 code = huff.bits & 7;
					debug("length code length %u\n", code);
					lengths2[hclen_order[i]] = code;
					SHIFT(3);
				}
				for_range(i, hclen, 19) {lengths2[hclen_order[i]] = 0;}
				construct_codes(19, lengths2, length_codes);
				for_array(i, length_codes) {
					debug("code length code %u length: %u, code %x\n", i, (unsigned)lengths2[i], (unsigned)length_codes[i]);
				}
				construct_dectable(19, lengths2, length_codes, dectable_dist, dist_oloff, dist_overlength);
				/*for (size_t i = 0; i < 512; i += 4) {
					debug("%04x %04x %04x %04x\n", dectable_dist[i], dectable_dist[i+1], dectable_dist[i+2], dectable_dist[i+3]);
				}*/
				huff.val = hlit;
				huff = decode_lengths(huff, end, dectable_dist, lit_lengths);
				check(huff.ptr, "ran out of data while decoding literal code lengths\n");
				check(huff.val == NO_ERROR, "%s", error_msg[huff.val]);
				for_range(lit, hlit, 286) {lit_lengths[lit] = 0;}
				huff.val = hdist;
				huff = decode_lengths(huff, end, dectable_dist, dist_lengths);
				check(huff.ptr, "ran out of data while decoding distance code lengths\n");
				check(huff.val == NO_ERROR, "%s\n", error_msg[huff.val]);
				for_range(dist, hdist, 30) {dist_lengths[dist] = 0;}
				construct_codes(286, lit_lengths, lit_codes);
				construct_dectable(286, lit_lengths, lit_codes, dectable_lit, lit_oloff, lit_overlength);
				construct_codes(30, dist_lengths, dist_codes);
				construct_dectable(30, dist_lengths, dist_codes, dectable_dist, dist_oloff, dist_overlength);
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
					dectable_lit[i] = rev < 0x5f ? ((rev >> 2) + 256) << 4 | 7
						: rev < 0x17f ? ((rev >> 1) - 0x30) << 4 | 8
						: rev < 0x18b ? ((rev >> 1) - 0xc0 + 280) << 4 | 8
						: rev < 0x18f ? 10
						: (rev - 0x190 + 144) << 4 | 9;
				}
				for_array(i, lit_oloff) {lit_oloff[i] = 0;}
				assert(dectable_lit[0] == 0x1007);
				assert(dectable_lit[0xc] == 0x8);
				info("%x\n", (unsigned)dectable_lit[3]);
				assert(dectable_lit[0x3] == (280 << 4 | 8));
				assert(dectable_lit[0x13] == 0x909);

				for_range(i, 0, 512) {
					u16 rev = (i << 4 & 0x10) | bitrev_nibble[i >> 1 & 15];
					dectable_dist[i] = rev < 30 ? rev << 4 | 5 : 10;
				}
				for_array(i, dist_oloff) {dist_oloff[i] = 0;}
			}
			while (1) {
				huff.val = 265;
				huff = huff_with_extra(huff, end, dectable_lit, lit_extra, lit_base, lit_oloff, lit_overlength, lit_codes);
				check(huff.ptr, "ran out of data during a length/literal symbol\n");
				if (huff.val < 256) {
					debug("literal 0x%02x %c\n", huff.val, huff.val >= 0x20 && huff.val < 0x7f ? (char)huff.val : '.');
					check(out < out_end, "ran out of output buffer while decoding a literal\n");
					*out++ = huff.val;
					crc = crc >> 8 ^ crc_table[(u8)crc ^ huff.val];
				} else if (huff.val > 256) {
					u32 length = huff.val - 257 + 3;
					debug("match length=%u ", length);
					huff.val = 4;
					huff = huff_with_extra(huff, end, dectable_dist, dist_extra, dist_base, dist_oloff, dist_overlength, dist_codes);
					check(huff.ptr, "ran out of data during a distance symbol\n");
					u32 dist = huff.val + 1;
					check(dist <= out - *out_ptr, "match distance of %u beyond start of buffer", dist);
					if (length >= 258) {
						check(length != 258, "file used literal/length symbol 284 with 5 1-bits, which is specced invalid (without it being specially noted no less, WTF)\n");
						length = 258;
					}
					debug(" dist=%"PRIu32" %.*s…\n", dist, length < dist ? (int)length : (int)dist, out - dist);
					check(length < out_end - out, "not enough output buffer to perform a match copy of size %u\n", length);
					lzcommon_match_copy(out, dist, length);
					do {
						crc = crc >> 8 ^ crc_table[(u8)crc ^ *out++];
					} while (--length);
					if (out - *out_ptr > 100) {
						debug("%.100s\n", out - 100);
					} else {
						debug("%.*s\n", (int)(out - *out_ptr), *out_ptr);
					}
				} else {
					break;
				}
			}
		}
	} while (!(block_header & BFINAL));
	*out_ptr = out;
	*in = huff.ptr;
	*crc_ptr = crc;
	return 1;
}

enum {
	FTEXT = 1,
	FHCRC = 2,
	FEXTRA = 4,
	FNAME = 8,
	FCOMMENT = 16,
	FRESERVED_MASK = 0xe0
};

enum compr_probe_status probe_gzip(const u8 *in, const u8 *end, u64 *size) {
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

const u8 *decompress_gzip(const u8 *in, const u8 *end, u8 **out, u8 *out_end) {
	u32 poly = 0xedb88320;
	u32 crc_table[256];
	for_array(i, crc_table) {
		crc_table[i] = crc32_byte(i, poly);
	}
	assert(end - in >= 18);
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
			hcrc_comp = hcrc_comp >> 8 ^ crc_table[(u8)hcrc_comp ^ *ptr];
		} while (++ptr < content);
		check((u16)hcrc_comp == hcrc, "header CRC mismatch, read %04x, computed %04x\n", (unsigned)hcrc, (unsigned)(u16)hcrc_comp);
		content += 2;
	}
	check(end - content > 8, "gzip input not long enough (%zu (%zx) bytes after header) for compressed stream and trailer\n", end - content, end - content);
	u32 crc = ~(u32)0;
	u8 *out_start = *out;
	check(inflate(&content, end, out, out_end, &crc, crc_table), "deflate decompression failed\n");
	check(end - content == 8, "trailer size %zu, expected 8\n", end - content);
	u32 crc_read = content[0] | (u32)content[1] << 8 | (u32)content[2] << 16 | (u32)content[3] << 24;
	crc ^= 0xffffffff;
	check(crc_read == crc, "content CRC mismatch: read %08x, computed %08x\n", crc_read, crc);
	info("CRC32: %08x\n", crc);
	content += 4;
	u32 isize = content[0] | (u32)content[1] << 8 | (u32)content[2] << 16 | (u32)content[3] << 24;
	content += 4;
	u32 isize_comp = (u32)(*out - out_start);
	check(isize_comp == isize, "compressed size mismatch, read %08x, decompressed %08x (mod 1 << 32)\n", isize, isize_comp);
	return content;
}
