/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

struct rksaradc_regs {
	u32 data;
	u32 status;
	u32 control;
	u32 delay;
};

enum {
	RKSARADC_POWER_UP = 8,
	RKSARADC_INT_ENABLE = 32,
	RKSARADC_INTERRUPT = 64,
};
