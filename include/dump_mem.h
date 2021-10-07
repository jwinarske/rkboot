/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>
#include <inttypes.h>
#include <stdio.h>

#include <format.h>
#include <plat.h>

static inline void UNUSED dump_mem(volatile const void *mem, size_t size) {
	char buf[16], line[71];
	printf("memory dump starting at %"PRIxPTR":\n", (uintptr_t)mem);
	for (size_t pos = 0; pos < size; pos += ARRAY_SIZE(buf)) {
		const char *start = (const char *)mem + pos;
		u32 len = size - pos > ARRAY_SIZE(buf) ? ARRAY_SIZE(buf) : size - pos;
		for_range(i, 0, len) {buf[i] = start[i];}
		fmt_hex(pos & 0xfff, '0', 3, line, line + 3);
		line[3] = ':';
		line[69] = '\r';
		line[70] = '\n';
		for_range(j, 0, len) {
			char c = buf[j];
			line[j*3 + 4] = ' ';
			fmt_hex((u8)c, '0', 2, line + j*3 + 5, line + j*3 + 7);
			line[j + 53] = c < 0x20 || c > 0x7e ? '.' : c;
		}
		for_range(i, len * 3 + 4, 53) {line[i] = ' ';}
		for_range(i, 53 + len, 69) {line[i] = ' ';}
		plat_write_console(line, 71);
	}
}
