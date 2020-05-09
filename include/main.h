/* SPDX-License-Identifier: CC0-1.0 */
#pragma once

#include <defs.h>
#include "../config.h"
#include <aarch64.h>
#include <lib.h>
#include <mmu.h>

void setup_timer();
void udelay(u32 usec);
u64 get_timestamp();

_Bool setup_pll(volatile u32 *base, u32 freq);

void ddrinit();

static inline void clrset32(volatile u32 *addr, u32 clear, u32 set) {
	*addr = (*addr & ~clear) | set;
}
static inline void UNUSED apply32v(volatile u32 *addr, u64 op) {
	clrset32(addr, op >> 32, (u32)op);
}
static inline void UNUSED clrset32m(u32 *addr, u32 clear, u32 set) {
	*addr = (*addr & ~clear) | set;
}
static inline void UNUSED apply32m(u32 *addr, u64 op) {
	clrset32(addr, op >> 32, (u32)op);
}

#define bounds_checked(arr, i) ((unlikely ((i) >= ARRAY_SIZE(arr)) ? die(__FILE__":"STRINGIFY(__LINE__)": ERROR: "#arr "[" #i "] out of bounds") : 0), arr[i])

static inline u32 UNUSED ubfx32(u32 v, u32 shift, u32 length) {
	return v >> shift & ((1 << length) - 1);
}

static inline void UNUSED mmio_barrier() {__asm__ volatile("dsb sy");}

enum {
	ASSUMPTION_16BIT_CHANNEL = 1,
	ASSUMPTION_Po2_ROWS = 1,
};
