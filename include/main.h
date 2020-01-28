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
#define assert_msg(expr, msg) do {if (unlikely(!(expr))) {die(msg);}}while(0)
#define assert(expr) assert_msg(expr,  __FILE__":"STRINGIFY(__LINE__)": ASSERTION FAILED: "#expr)
#define assert_unimpl(expr, feature) assert_msg(expr, __FILE_":"STRINGIFY(__LINE__)": UNIMPLEMENTED: "feature)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#define bounds_checked(arr, i) ((unlikely ((i) >= ARRAY_SIZE(arr)) ? die(__FILE__":"STRINGIFY(__LINE__)": ERROR: "#arr "[" #i "] out of bounds") : 0), arr[i])

#define for_range(i, a, b) for (u32 i = a; i < b; ++i)
#define for_array(i, arr) for (u32 i = 0; i < ARRAY_SIZE(arr); ++i)
