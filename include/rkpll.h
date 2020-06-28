/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

enum {RKPLL_SLOW = 0, RKPLL_NORMAL = 1, RKPLL_DEEP_SLOW = 2};

void rkpll_configure(volatile u32 *base, u32 mhz);

static inline _Bool UNUSED rkpll_switch(volatile u32 *base) {
	if (base[2] >> 31) {
		base[3] = SET_BITS16(2, RKPLL_NORMAL) << 8;
		return 1;
	}
	return 0;
}
