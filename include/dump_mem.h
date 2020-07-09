/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>
#include <inttypes.h>
#include <stdio.h>

static inline void UNUSED dump_mem(volatile const void *mem, size_t size) {
	u8 buf[16];
	for (size_t pos = 0; pos < size; pos += ARRAY_SIZE(buf)) {
		const u8 *start = (const u8 *)mem + pos;
		u32 len = size - pos > ARRAY_SIZE(buf) ? ARRAY_SIZE(buf) : size - pos;
		for_range(i, 0, len) {buf[i] = start[i];}
		printf("%08"PRIx64":", (u64)start);
		for_range(j, 0, len) {
			printf(" %02"PRIx8, buf[j]);
		}
		puts("  ");
		for_range(j, 0, len) {
			u8 c = start[j];
			printf("%c", c < 0x7f && c >= 32 ? c : '.');
		}
		puts("\n");
	}
}
