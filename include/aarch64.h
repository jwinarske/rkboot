/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include "defs.h"

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

static inline void UNUSED dmb_st() {__asm__("dmb st");}

static inline void UNUSED dsb_st() {__asm__("dsb st");}
static inline void UNUSED dsb_ishst() {__asm__("dsb ishst");}
