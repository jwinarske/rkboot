/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>
#include <inttypes.h>
#include <stdio.h>

static inline void UNUSED dump_mem(void *mem, size_t size) {
	static const size_t columns = 16;
	for_range(i, 0, size / columns) {
		const u8 *start = (const u8 *)(mem + columns*i);
		printf("%08"PRIx64":", (u64)start);
		for_range(j, 0, columns) {
			printf(" %02"PRIx8, start[j]);
		}
		puts("  ");
		for_range(j, 0, columns) {
			printf("%c", start[j] < 0x7f && start[j] >= 32 ? start[j] : '.');
		}
		puts("\n");
	}
}
