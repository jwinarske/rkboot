/* SPDX-License-Identifier: CC0-1.0 */
#pragma once

enum {
	SCTLR_M = 1,
	SCTLR_C = 4,
	SCTLR_SA = 8,
	SCTLR_I = 0x1000,
	SCTLR_EL3_RES1 = 0x30c50830
};
enum {
	SCR_EA = 8,
	SCR_FIQ = 4,
	SCR_IRQ = 2,
	SCR_EL3_RES1 = 0x30,
};

static inline void dsb_st() {__asm__("dsb st");}
