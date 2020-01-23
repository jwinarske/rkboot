#pragma once

#include <types.h>
#include "../config.h"

void yield();
void puts(const char *);
_Noreturn int PRINTF(1, 2) die(const char *fmt, ...);
void PRINTF(1, 2) printf(const char *fmt, ...);
#define log(fmt, ...) printf("[%zu] " fmt, get_timestamp(), __VA_ARGS__)

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

#define NO_ASAN __attribute__((no_sanitize_address))
#define UNUSED __attribute__((unused))
