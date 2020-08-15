/* SPDX-License-Identifier: CC0-1.0 */
#include <asm.h>

.section .text.asm.sched_yield
create_runnable:
	.cfi_startproc
	adr x2, sched_resume
	stp xzr, x2, [x29, #16]

	// save non-volatile GPRs
	stp x19, x20, [x29, #32]
	stp x21, x22, [x29, #48]
	stp x23, x24, [x29, #64]
	stp x25, x26, [x29, #80]
	stp x27, x28, [x29, #96]
	ret
	.cfi_endproc

PROC(sched_yield_test, 2)
	create_stackframe 112

	bl create_runnable
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

	mov x19, x1
	mov x20, x0
	mov x21, x2
	mov x22, x3

	add x1, x29, #16
	bl sched_queue_single

	mov x0, x20
	mov x1, x21
	mov x2, x22
	br x19
ENDFUNC(sched_yield_test)
PROC(sched_yield, 2)
	create_stackframe 112

	bl create_runnable
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

	mov x0, #0
	add x1, x29, #16
	bl sched_queue_single
	b sched_next
ENDFUNC(sched_yield)
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
	add sp, x1, #-16
	add x29, x1, #-16
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
	b sched_next
ENDFUNC(sched_start_thread)
