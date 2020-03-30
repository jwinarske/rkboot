#pragma once

#include <defs.h>
#include "../config.h"

void yield();
void puts(const char *);
_Noreturn int PRINTF(1, 2) die(const char *fmt, ...);
void PRINTF(1, 2) printf(const char *fmt, ...);
#define log(fmt, ...) printf("[%zu] " fmt, get_timestamp(), __VA_ARGS__)
#ifdef DEBUG_MSG
#define debug(fmt, ...) printf(fmt, __VA_ARGS__)
#define debugs(s) puts(s)
#else
#define debug(fmt, ...)
#define debugs(s)
#endif

void setup_timer();
void udelay(u32 usec);
u64 get_timestamp();
_Noreturn void halt_and_catch_fire();

_Bool setup_pll(volatile u32 *base, u32 freq);

enum {
	SCTLR_M = 1,
	SCTLR_C = 4,
	SCTLR_SA = 8,
	SCTLR_I = 0x1000,
	SCTLR_EL3_RES1 = 0x30c50830
};
void invalidate_dcache_set_sctlr(u64);
void set_sctlr_flush_dcache(u64);
void flush_dcache();
void setup_mmu();
enum {
	MEM_TYPE_DEV_nGnRnE = 0,
	MEM_TYPE_DEV_nGnRE = 1,
	MEM_TYPE_DEV_nGRE = 2,
	MEM_TYPE_DEV_GRE = 3,
	MEM_TYPE_NORMAL = 4
};
extern const struct mapping {u64 first, last; u8 type;} initial_mappings[];

void ddrinit();

static inline void clrset32(volatile u32 *addr, u32 clear, u32 set) {
	*addr = (*addr & ~clear) | set;
}
static inline void apply32v(volatile u32 *addr, u64 op) {
	clrset32(addr, op >> 32, (u32)op);
}
static inline void clrset32m(u32 *addr, u32 clear, u32 set) {
	*addr = (*addr & ~clear) | set;
}
static inline void apply32m(u32 *addr, u64 op) {
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

enum {
	ASSUMPTION_16BIT_CHANNEL = 1,
	ASSUMPTION_Po2_ROWS = 1,
};
