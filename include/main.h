/* SPDX-License-Identifier: CC0-1.0 */
#pragma once

#include <defs.h>
#include "../config.h"
#include <aarch64.h>
#include <output.h>

void setup_timer();
void udelay(u32 usec);
u64 get_timestamp();
_Noreturn void halt_and_catch_fire();

_Bool setup_pll(volatile u32 *base, u32 freq);
void invalidate_dcache_set_sctlr(u64);
void set_sctlr_flush_dcache(u64);
void flush_dcache();
void setup_mmu();
extern const struct mapping {u64 first, last; u8 type;} initial_mappings[];

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

#define STRINGIFY(x) #x
#define assert_msg(expr, ...) do {if (unlikely(!(expr))) {die(__VA_ARGS__);}}while(0)
#define assert(expr) assert_msg(expr,  "%s:%s:%u: ASSERTION FAILED: %s\n", __FILE__, __FUNCTION__, __LINE__, #expr)
#define assert_unimpl(expr, feature) assert_msg(expr, __FILE__":"STRINGIFY(__LINE__)": UNIMPLEMENTED: "feature)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#define bounds_checked(arr, i) ((unlikely ((i) >= ARRAY_SIZE(arr)) ? die(__FILE__":"STRINGIFY(__LINE__)": ERROR: "#arr "[" #i "] out of bounds") : 0), arr[i])

#define for_range(i, a, b) for (u32 i = a; i < b; ++i)
#define for_array(i, arr) for (u32 i = 0; i < ARRAY_SIZE(arr); ++i)

static inline u32 UNUSED ubfx32(u32 v, u32 shift, u32 length) {
	return v >> shift & ((1 << length) - 1);
}

static inline void UNUSED mmio_barrier() {__asm__ volatile("dsb sy");}

enum {
	ASSUMPTION_16BIT_CHANNEL = 1,
	ASSUMPTION_Po2_ROWS = 1,
};
