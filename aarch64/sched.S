/* SPDX-License-Identifier: CC0-1.0 */
#include <asm.h>

.section .text.asm.sched_yield
PROC(call_cc, 2)
call_cc_flag_runnablelist: .global call_cc_flag_runnablelist
call_cc_ptr2_int2: .global call_cc_ptr2_int2
call_cc_ptr2_int1: .global call_cc_ptr2_int1
	create_stackframe 112

	msr SPSel, #1
	adr x10, sched_resume
	stp xzr, x10, [x29, #16]

	// save non-volatile GPRs
	stp x19, x20, [x29, #32]
	stp x21, x22, [x29, #48]
	stp x23, x24, [x29, #64]
	stp x25, x26, [x29, #80]
	stp x27, x28, [x29, #96]
	.cfi_offset x19, -80
	.cfi_offset x20, -72
	.cfi_offset x21, -64
	.cfi_offset x22, -56
	.cfi_offset x23, -48
	.cfi_offset x24, -40
	.cfi_offset x25, -32
	.cfi_offset x26, -24
	.cfi_offset x27, -16
	.cfi_offset x28, -8

	mov x10, x0
	add x0, x29, #16
	do19thru28 ldp, sp, 16
	ldp x29, x30, [sp], #96
	br x10
ENDFUNC(call_cc)
PROC(sched_resume, 2)
	.cfi_def_cfa x0, 96
	.cfi_offset x19, -80
	.cfi_offset x20, -72
	.cfi_offset x21, -64
	.cfi_offset x22, -56
	.cfi_offset x23, -48
	.cfi_offset x24, -40
	.cfi_offset x25, -32
	.cfi_offset x26, -24
	.cfi_offset x27, -16
	.cfi_offset x28, -8
	.cfi_offset x29, -112
	.cfi_offset x30, -104
	// restore the stack
	msr SPSel, #0
	add sp, x0, #-16
	add x29, x0, #-16
	.cfi_def_cfa x29, 112

	// restore non-volatile GPRs
	ldp x19, x20, [x29, #32]
	.cfi_same_value x19
	.cfi_same_value x20
	ldp x21, x22, [x29, #48]
	.cfi_same_value x21
	.cfi_same_value x22
	ldp x23, x24, [x29, #64]
	.cfi_same_value x23
	.cfi_same_value x24
	ldp x25, x26, [x29, #80]
	.cfi_same_value x25
	.cfi_same_value x26
	ldp x27, x28, [x29, #96]
	.cfi_same_value x27
	.cfi_same_value x28

	ldp x29, x30, [sp], #112
	.cfi_def_cfa sp, 0
	.cfi_same_value x29
	.cfi_same_value x30
	ret
ENDFUNC(sched_resume)

.section .text.asm.sched_start_thread
PROC(sched_start_thread, 2)
	.cfi_def_cfa x0, 80
	.cfi_undefined x30	// a newly-started thread has no return address
	// set up the stack
	msr SPSel, #0
	add sp, x0, #80
	.cfi_def_cfa sp, 0

	ldp x20, x21, [x0, #16]
	ldp x2, x3, [x0, #48]
	ldp x4, x5, [x0, #64]
	ldp x6, x7, [x0, #80]
	ldp x0, x1, [x0, #32]

	stp xzr, xzr, [sp]	// bottom of the frame chain
	add x29, sp, #0
	blr x20
	// thread function returned: abandon the thread
	msr SPSel, #1
	do19thru28 ldp, sp, 16
	ldp x29, x30, [sp], #96
	ret
ENDFUNC(sched_start_thread)

TEXTSECTION(.text.asm.sched_run)
PROC(arch_sched_run, 2)
	create_stackframe 96
	do19thru28 stp, sp, 16
	ldr x1, [x0, #8]
	br x1
ENDFUNC(arch_sched_run)
