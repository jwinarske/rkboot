/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>
#include <plat.h>

HEADER_FUNC timestamp_t get_timestamp() {
	u64 res;
	__asm__ volatile("mrs %0, CNTPCT_EL0" : "=r"(res));
	return res;
}

HEADER_FUNC void udelay(u32 usec) {
	timestamp_t start = get_timestamp();
	do {
		__asm__ volatile("yield");
	} while (get_timestamp() - start < usec * TICKS_PER_MICROSECOND);
}
