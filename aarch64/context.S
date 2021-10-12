/* SPDX-License-Identifier: CC0-1.0 */
#include <asm.h>
#include <arch/context.h>

TEXTSECTION(.text.asm.context)
PROC(call_cc, 2)
call_cc_flag_runnablelist: .global call_cc_flag_runnablelist
call_cc_ptr2_int2: .global call_cc_ptr2_int2
call_cc_ptr2_int1: .global call_cc_ptr2_int1
	msr SPSel, #1
	mrs_per_el x9, TPIDR, CONFIG_EL
	str x30, [x9, CTX_PC_OFF]

	do19thru28 stp, x9, CTX_NONVOLATILES_OFF
	mrs x10, SP_EL0
	stp x29, x30, [x9, #(CTX_NONVOLATILES_OFF + 8*(29 - 19))]
	str x10, [x9, #(CTX_NONVOLATILES_OFF + 8*(31 - 19))]

	mov x10, x0
	mov x0, x9
	msr_per_el TPIDR, CONFIG_EL, xzr

	add x11, x0, #CTX_STATUS_OFF
	1:	ldaxr w12, [x11]
		orr x13, x12, #THREAD_YIELDED
		stxr w14, w13, [x11]
		cbnz x14, 1b

	do19thru28 ldp, sp, 16
	ldp x29, x30, [sp], #96
	br x10
ENDFUNC(call_cc)

PROC(arch_sched_run, 2)
	create_stackframe 96
	do19thru28 stp, sp, 16
	add x3, x0, #CTX_STATUS_OFF
	1:	ldaxr w1, [x3]
		and x2, x1, #0xfffffff0
		stxr w4, w2, [x3]
		cbnz x4, 1b
	msr_per_el TPIDR, CONFIG_EL, x0

	ldr x2, [x0, #(CTX_NONVOLATILES_OFF + 8*(31 - 19))]
	do19thru28 ldp, x0, CTX_NONVOLATILES_OFF
	ldp x29, x30, [x0, #(CTX_NONVOLATILES_OFF + 8*(29 - 19))]
	msr SP_EL0, x2

	and x1, x1, #0xf
	cmp x1, #THREAD_PREEMPTED
	beq resume

	// thread yielded: volatile GPRs and PSTATE were not saved
	msr SPSel, #0
	ret	// thread context saves the caller of yield in x30

resume:
	// thread was preempted: restore volatile GPRs and do
	// a full exception return
	msr DAIFSet, #15
	ldr x2, [x0, #CTX_PC_OFF]
	ldr x1, [x0, #(CTX_NONVOLATILES_OFF + 8*(31 - 19))]
	ldr w18, [x0, #CTX_SPSR_OFF]
	msr_per_el ELR, CONFIG_EL, x2
	do2thru17 ldp, x0, CTX_VOLATILES_OFF + 0x10
	msr SP_EL0, x1
	msr_per_el SPSR, CONFIG_EL, x18
	ldr x18, [x0, #(CTX_VOLATILES_OFF + 8*18)]
	ldp x0, x1, [x0, #CTX_VOLATILES_OFF]
	eret
ENDFUNC(arch_sched_run)

PROC(aarch64_abandon_thread, 2)
	msr DAIFClr, #15
	msr SPSel, #1
	mrs_per_el x3, TPIDR, CONFIG_EL
	add x3, x3, CTX_STATUS_OFF
	1:	ldaxr w1, [x3]
		orr w2, w1, #THREAD_DEAD
		stxr w4, w2, [x3]
		cbnz x4, 1b
	msr_per_el TPIDR, CONFIG_EL, xzr
	do19thru28 ldp, sp, 16
	ldp x29, x30, [sp], #96
	ret
ENDFUNC(aarch64_abandon_thread)
