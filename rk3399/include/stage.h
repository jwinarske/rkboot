/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>
#include <aarch64.h>
#include <lib.h>

extern u8 __start__[], __ro_end__[], __data_end__[], __bss_start__[];
extern u8 __bss_noinit__[], __bss_write_through__[], __bss_uncached__[];
extern u8 __end__[];
extern u8 __exc_base__[];
static inline u32 compute_crc32c(u64 *start, u64 *end, u32 seed) {
    while (start < end) {
	__asm__("crc32cx %w0, %w0, %1" : "+r"(seed) : "r"(*start++));
    }
    return seed;
}

void main();
_Noreturn void secondary_cpu_main();
