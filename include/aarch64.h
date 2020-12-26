/* SPDX-License-Identifier: CC0-1.0 */
#pragma once

#define SCTLR_M 1
#define SCTLR_C 4
#define SCTLR_SA 8
#define SCTLR_I 0x1000
/* these constants set all bits that are res1 in the base architecture, even if some extensions define a meaning for the value of 0 */
#define SCTLR_EL1_RES1 0x30d00980
#define SCTLR_EL2_RES1_E2H 0x30500980
#define SCTLR_EL2_RES1_HV 0x30c50830
#define SCTLR_EL3_RES1 0x30c50830

#define SCR_IRQ  2
#define SCR_FIQ 4
#define SCR_EA 8
#define SCR_EL3_RES1 0x30

#ifdef __ASSEMBLER__
.macro mov32 reg val
	mov \reg, #((\val) & 0xffff0000)
	movk \reg, #((\val) & 0xffff)
.endm
.macro mov64 reg val
	.if (\val) & 0xffff000000000000
		mov \reg, #((\val) & 0xffff000000000000)
		.if (\val) & 0x0000ffff00000000
			movk \reg, #((\val) >> 32 & 0xffff), lsl 32
		.endif
	.elseif (\val) & 0x0000ffff00000000
		mov \reg, #((\val) & 0x0000ffff00000000)
	.else
		.error "mov64 for a ≤32-bit number"
	.endif
	.if (\val) & 0x00000000ffff0000
		movk \reg, #((\val) >> 16 & 0xffff), lsl 16
	.endif
	.if (\val) & 0x000000000000ffff
		movk \reg, #((\val) & 0xffff)
	.endif
.endm
#else
#include "defs.h"
static inline void UNUSED dmb_st() {__asm__("dmb st");}

static inline void UNUSED dsb_sy() {__asm__("dsb sy");}
static inline void UNUSED dsb_st() {__asm__("dsb st");}
static inline void UNUSED dsb_ish() {__asm__("dsb ish");}
static inline void UNUSED dsb_ishst() {__asm__("dsb ishst");}
#endif
