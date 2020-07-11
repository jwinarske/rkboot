/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

struct rktimer_regs {
	u32 load_count0;
	u32 load_count1;
	u32 value0;
	u32 value1;
	u32 load_count2;
	u32 load_count3;
	u32 interrupt_status;
	u32 control;
};
_Static_assert(sizeof(struct rktimer_regs) == 32, "wrong size for timer struct");
