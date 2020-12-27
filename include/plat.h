/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

typedef u64 timestamp_t;
typedef u32 phys_addr_t;
static const phys_addr_t PLAT_INVALID_PHYS_ADDR = 0xffffffff;
#define TICKS_PER_MICROSECOND 24
#define NSECS(n) ((u64)(n) * TICKS_PER_MICROSECOND / 1000)
#define USECS(n) ((u64)(n) * TICKS_PER_MICROSECOND)
#define MSECS(n) ((u64)(n) * 1024 * TICKS_PER_MICROSECOND)
#define PRIuTS PRIu64
#define PRIxPHYS PRIx32

static volatile struct uart *const uart = (volatile struct uart *)0xff1a0000;
void plat_write_console(const char *str, size_t len);

_Noreturn void plat_panic();

enum {PLAT_PAGE_SHIFT = 12};

HEADER_FUNC _Bool plat_is_page_aligned(void *ptr) {
	return (uintptr_t)ptr % (1 << PLAT_PAGE_SHIFT) == 0;
}

HEADER_FUNC phys_addr_t plat_virt_to_phys(void *ptr) {
	if ((uintptr_t)ptr >= 0xffe00000) {return PLAT_INVALID_PHYS_ADDR;}
	return (phys_addr_t)(uintptr_t)ptr;
}

struct sched_runqueue *get_runqueue();
