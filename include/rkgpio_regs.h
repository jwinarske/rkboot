/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

struct rkgpio_regs {
	u32 port;
	u32 direction;
	u32 padding0[10];
	u32 interrupt_enable;
	u32 interrupt_mask;
	u32 interrupt_type;
	u32 interrupt_polarity;
	u32 interrupt_status;
	u32 interrupt_raw_status;
	u32 debounce;
	u32 eoi;
	u32 read;
	u32 padding1[3];
	u32 level_sensitive_sync;
};
CHECK_OFFSET(rkgpio_regs, interrupt_enable, 0x30);
CHECK_OFFSET(rkgpio_regs, level_sensitive_sync, 0x60);
