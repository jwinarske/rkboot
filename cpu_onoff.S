/* SPDX-License-Identifier: CC0-1.0 */
#include <asm.h>
#include <aarch64.h>
#include <mmu.h>
#include <cortex_a53.h>


TEXTSECTION(.text.reset_entry)
PROC(reset_entry, 2)
	mov32 x19, SCTLR_SA | SCTLR_EL23_RES1
	msr SCTLR_EL3, x19
	isb
	mrs x19, MIDR_EL1
	mov32 x20, 0x410fd034	/* Cortex-A53 r0p4 */
	cmp x19, x20
	b.eq a53_init
	/* unknown processor, spin forever */
halt:
1:
	wfi
	b 1b

a53_init:
	cortex_a53_init x19, 0x04
	mov32 x19, SCTLR_I | SCTLR_SA | SCTLR_EL23_RES1
	msr SCTLR_EL3, x19
	isb
	/* invalidate L1D cache */
	mov x7, #0
	bl aarch64_invalidate_cache_level
	/* fall through */

any_core_init:
	/* exception setup */
	mov x20, #(SCR_EL3_RES1 | SCR_EA | SCR_FIQ | SCR_IRQ)
	adrp x21, __exc_base__
	msr SCR_EL3, x20
	msr VBAR_EL3, x21
	isb

	aarch64_misc_init x20, x21

	/* start MMU */
	tlbi alle3
	//ldr x20, =(MMU_MAIR_VAL)
	mov64 x20, MMU_MAIR_VAL
	msr MAIR_EL3, x20

	//ldr x22, =(MMU_TCR_VAL)
	mov32 x22, MMU_TCR_VAL
	msr TCR_EL3, x22

	adrp x21, pagetables
	ldr x21, [x21, :lo12:pagetables]
	msr TTBR0_EL3, x21

	mrs x19, SCTLR_EL3
	mov x21, #(SCTLR_M | SCTLR_C)
	orr x19, x19, x21
	dsb sy;isb; dsb sy
	msr SCTLR_EL3, x19
	isb; dsb sy

	adrp x1, percpu_index
	add x1, x1, :lo12:percpu_index
	mrs x3, MPIDR_EL1
find_cpu:
	ldp x2, x0, [x1], 16
	cbz x0, halt
	cmp x2, x3
	b.ne find_cpu

	ldr x1, [x0]
	msr SPSel, 1
	add sp, x1, #0
	stp xzr, xzr, [sp, -16]!
	b secondary_cpu_main
ENDFUNC(reset_entry)

TEXTSECTION(.text.cortex_a53_exit)
PROC(cortex_a53_exit, 2)
	mov x2, x30
	/* disable MMU and D$ */
	mov32 x19, SCTLR_I | SCTLR_SA | SCTLR_EL23_RES1
	dsb sy
	isb
	msr SCTLR_EL3, x19
	isb

	/* flush L1D cache */
	mov x7, #0
	bl aarch64_flush_cache_level

	/* disable and invalidate I$ */
	mov32 x19, SCTLR_SA | SCTLR_EL23_RES1
	msr SCTLR_EL3, x19
	isb
	ic iallu

	/* exit intra-cluster coherency */
	mrs x19, CORTEX_A53_CPUECTLR_EL1
	orr x19, x19, #CORTEX_A53_CPUECTLR_EL1_SMPEN
	msr CORTEX_A53_CPUECTLR_EL1, x19
	isb
	dsb sy

	tbz x0, 0, 1f

	//mov x1, 3
	//msr RMR_EL3, x1
	mov x4, 0xff760000
	mov32 x1, 0x00050005
	str w1, [x4, 0x400]

	mov x30, x2
	ret
1:
	wfi
	b 1b
ENDFUNC(cortex_a53_exit)
