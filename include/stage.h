/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>
#include <aarch64.h>
#include <lib.h>

extern u8 __bss_start__[], __bss_end__[], __bss_noinit__[], __exc_base__[];
extern u8 __start__[], __end__[], __ro_end__[], __data_end__[];
static inline u32 compute_crc32c(u64 *start, u64 *end, u32 seed) {
    while (start < end) {
	__asm__("crc32cx %w0, %w0, %1" : "+r"(seed) : "r"(*start++));
    }
    return seed;
}

struct stage_store {
	u64 sctlr;
	u64 vbar;
	u64 scr;
};

static inline void UNUSED stage_setup(struct stage_store *store) {
#ifdef CONFIG_CRC
	u64 *crc_start = (u64*)__start__, *crc_mid = (u64*)(((u64)__ro_end__ + 0xfff) & ~(u64)0xfff), *crc_end = (u64*)(((u64)__data_end__ + 0xfff) & ~(u64)0xfff);
	u32 crc_ro =  compute_crc32c(crc_start, crc_mid, ~(u32)0);
	printf("CRC32C(%08zx, %08zx): %08x\n", (u64)crc_start, (u64)crc_mid, ~crc_ro);
	u32 crc_all = compute_crc32c(crc_mid, crc_end, crc_ro);
	printf("CRC32C(%08zx, %08zx): %08x\n", (u64)crc_start, (u64)crc_end, ~crc_all);
#endif
}
