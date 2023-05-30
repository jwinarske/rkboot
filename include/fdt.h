// SPDX-License-Identifier: CC0-1.0
#pragma once
#include <stdint.h>
#include <string.h>

#include <memaccess.h>

struct fdt_header {
	uint32_t magic;
	uint32_t totalsize;
	uint32_t struct_offset;
	uint32_t string_offset;
	uint32_t reserved_offset;
	uint32_t version;
	uint32_t last_compatible_version;
	uint32_t boot_cpu;
	uint32_t string_size;
	uint32_t struct_size; /* since v17 */
};

#define FDT_CMD_NODE 1
#define FDT_CMD_NODE_END 2
#define FDT_CMD_PROP 3
#define FDT_CMD_NOP 4
#define FDT_CMD_END 9

static inline _Bool fdt_has_zero_byte(uint32_t v) {
	v = ~v;
	v = (v & 0x0f0f0f0f) & (v >> 4);
	v &= v >> 2;
	v &= v >> 1;
	return v != 0;
}

static inline u32 fdt_token_size(const uint32_t *start, const uint32_t *end) {
	if (start >= end) {return 0;}
	size_t space = (const char*)end - (const char *)start;
	uint32_t cmd = be32(*start++);
	if (cmd == FDT_CMD_NODE) {
		size_t namelen = strnlen((const char *)start, space);
		return namelen != space ? 5 + namelen : 0;
	} else if (cmd == FDT_CMD_NODE_END) {
		return 4;
	} else if (cmd == FDT_CMD_PROP) {
		if (space < 12) {return 0;}
		uint32_t len = be32(*start);
		if (space < 12 + len) {return 0;}
		return 12 + len;
	} else if (cmd == FDT_CMD_NOP) {return 4;}
	return 0;
}

#define DEFINE_FDT_ERROR(X) X(INVALID_TOKEN) X(OVERFLOW)

_Bool dump_fdt(const struct fdt_header *fdt);
