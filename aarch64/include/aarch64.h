// SPDX-License-Identifier: CC0-1.0
#pragma once

#define HCR_VM 1
#define HCR_SWIO 2
#define HCR_PTW 4
#define HCR_FMO 8
#define HCR_IMO 16
#define HCR_AMO 0x20
#define HCR_VF 0x40
#define HCR_VI 0x80
#define HCR_VSE 0x100
#define HCR_FB 0x200
#define HCR_BSU_ISH 0x400
#define HCR_BSU_OSH 0x800
#define HCR_BSU_SY 0xc00
#define HCR_DC 0x1000
#define HCR_TWI 0x2000
#define HCR_TWE 0x4000
#define HCR_TID 0x8000	// bits 15:18 are trap enables for various ID registers
#define HCR_TSC 0x80000
#define HCR_TIDCP 0x100000
#define HCR_TACR 0x200000
#define HCR_TSW 0x400000
#define HCR_TPCP 0x800000
#define HCR_TPU 0x1000000
#define HCR_TTLB 0x2000000
#define HCR_TVM 0x4000000
#define HCR_TGE 0x8000000
#define HCR_TDZ 0x10000000
#define HCR_HCD 0x20000000
#define HCR_TRVM 0x40000000
#define HCR_RW (UINT64_C(1) << 31)
#define HCR_CD (UINT64_C(1) << 32)
#define HCR_ID (UINT64_C(1) << 33)
#define HCR_E2H (UINT64_C(1) << 34) // ARMv8.1-VHE
// various extension bits defaulting to Res0
#define HCR_TEA (UINT64_C(1) << 37)
#define HCR_MIOCNCE (UINT64_C(1) << 38)
// various Res0 bits or extension bits defaulting to it

#define MDCR_SPD32_LEGACY 0
#define MDCR_SPD32_DISABLED 0x8000
#define MDCR_SPD32_ENABLED 0xc000

#define PMCR_LC 0x40
#define PMCR_LP 0x80

#define SCTLR_M 1
#define SCTLR_C 4
#define SCTLR_SA 8
#define SCTLR_I 0x1000
// these constants set all bits that are res1 in the base
// architecture, even if some extensions define a meaning
// for the value of 0
#define SCTLR_EL1_RES1 0x30d00980
#define SCTLR_EL2_RES1_E2H 0x30500980
#define SCTLR_EL23_RES1 0x30c50830

#define SCR_IRQ  2
#define SCR_FIQ 4
#define SCR_EA 8
#define SCR_EL3_RES1 0x30

// each region has size, granule, inner and outer cacheability
// and shareability attributes
#define TCR_TxSZ(x) ((x) & 0x3f)
#define TCR_EPD 0x80	// only on EL1
#define TCR_INNER_UNCACHED 0
#define TCR_INNER_CACHED 0x100
#define TCR_INNER_WRITE_THROUGH 0x200
#define TCR_INNER_WRITE_NO_ALLOC 0x300
#define TCR_OUTER_UNCACHED 0
#define TCR_OUTER_CACHED 0x400
#define TCR_OUTER_WRITE_THROUGH 0x800
#define TCR_OUTER_WRITE_NO_ALLOC 0xc00
#define TCR_NONSHARED 0
#define TCR_OUTER_SHARED 0x2000
#define TCR_INNER_SHARED 0x3000
#define TCR_4K_GRANULE 0
#define TCR_64K_GRANULE 0x4000
#define TCR_16K_GRANULE 0x8000

#define TCR_REGION0(c) (c)
#define TCR_IPS_32 0
#define TCR_IPS_36 (UINT64_C(1) << 32)
#define TCR_IPS_40 (UINT64_C(2) << 32)
#define TCR_IPS_42 (UINT64_C(3) << 32)
#define TCR_IPS_44 (UINT64_C(4) << 32)
#define TCR_IPS_48 (UINT64_C(5) << 32)
#define TCR_IPS_52 (UINT64_C(6) << 32)

// TCR_EL1 features (also available in EL2 in an E2H situation)
#define TCR_REGION1(c) ((c) << 16)
#define TCR_A1 0x400000
#define TCR_ASID_8 0
#define TCR_ASID_16 (UINT64_C(1) << 36)
#define TCR_TBI0 (UINT64_C(1) << 37)
#define TCR_TBI1 (UINT64_C(1) << 38)

// TCR_EL2/3 features (no E2H)
#define TCR_PS(x) ((x) << 16)
#define TCR_EL23_TBI 0x100000
#define TCR_EL3_RES1 UINT64_C(0x80800000)

#define VMSAV8_64_XN (UINT64_C(1) << 54)
#define VMSAV8_64_DBM (UINT64_C(1) << 51)
#define VMSAV8_64_AF 0x400
#define VMSAV8_64_RO 0x80
#define VMSAV8_64_PRIV 0x40
#define VMSAV8_64_OSH 0x200
#define VMSAV8_64_ISH 0x300

#ifdef __ASSEMBLER__
#define UINT64_C(x) x
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
.macro aarch64_misc_init scratch0 scratch1
	mov \scratch0, #MDCR_SPD32_ENABLED
	mov \scratch1, #(PMCR_LP | PMCR_LC)
	msr MDCR_EL3, \scratch0
	msr PMCR_EL0, \scratch1
	msr CPTR_EL3, xzr	// don't enable any traps
.endm
#else
#include "defs.h"
static inline void UNUSED dmb_st() {__asm__("dmb st");}

static inline void UNUSED dsb_sy() {__asm__("dsb sy");}
static inline void UNUSED dsb_st() {__asm__("dsb st");}
static inline void UNUSED dsb_ish() {__asm__("dsb ish");}
static inline void UNUSED dsb_ishst() {__asm__("dsb ishst");}

HEADER_FUNC void aarch64_wfi() {asm("wfi");}
#endif
