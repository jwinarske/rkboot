#include <asm.h>
.section .text.exc, "ax", %progbits

.macro exc_handler name spsel
.align 7
	msr SPSel, #\spsel
	cmp SP, 0
	b.eq 0f
	stp X0, X30, [SP, #-16]!
	adrp x0, \name
	ldr x0, [x0, #:lo12:\name]
	cbz x0, 0f
	bl call_handler
	ldp X0, X30, [SP], #16
	eret
0:	/* cannot handle (no stack or routine set up) */
	adr x0, 1f
	b unhandled_exception
.align 3
.global \name
.type \name, %object
.size \name, 8
\name:
	.space 8, 0 /* handler address, used if non-zero */
1:
.endm

.global __exc_base__
.align 12
__exc_base__:
exc_handler name=sync_exc_handler_sp0, spsel=1
.asciz "sync SP_EL0"
exc_handler name=irq_handler_sp0, spsel=1
.asciz "IRQ SP_EL0"
exc_handler name=fiq_handler_sp0, spsel=1
.asciz "FIQ SP_EL0"
exc_handler name=serror_handler_sp0, spsel=1
.asciz "SError SP_EL0"
exc_handler sync_exc_handler_spx, spsel=0
.asciz "sync SP_ELx"
exc_handler irq_handler_spx, spsel=0
.asciz "IRQ SP_ELx"
exc_handler fiq_handler_spx, spsel=0
.asciz "FIQ SP_ELx"
exc_handler serror_handler_spx, spsel=0
.asciz "SError SP_ELx"
exc_handler sync_exc_handler_aarch64, spsel=1
.asciz "sync aarch64"
exc_handler irq_handler_aarch64, spsel=1
.asciz "IRQ aarch64"
exc_handler fiq_handler_aarch64, spsel=1
.asciz "FIQ aarch64"
exc_handler serror_handler_aarch64, spsel=1
.asciz "SError aarch64"
exc_handler sync_exc_handler_aarch32, spsel=1
.asciz "sync aarch32"
exc_handler irq_handler_aarch32, spsel=1
.asciz "IRQ aarch32"
exc_handler fiq_handler_aarch32, spsel=1
.asciz "FIQ aarch32"
exc_handler serror_handler_aarch32, spsel=1
.asciz "SError aarch32"


SPROC(call_handler, 6)
	stp X29, X30, [SP, #-160]!
	add X29, SP, #0
	stp X1, X2, [SP, #16]
	stp X3, X4, [SP, #32]
	stp X5, X6, [SP, #48]
	stp X7, X8, [SP, #64]
	stp X9, X10, [SP, #80]
	stp X11, X12, [SP, #96]
	stp X13, X14, [SP, #112]
	stp X15, X16, [SP, #128]
	stp X17, X18, [SP, #144]
	/*str x19, [SP, #-8]!
	mov x1, x0
	mov x19, x0
	add x2, SP, #0
	adr x0, msg
	bl printf
	mov x0, x19
	ldr x19, [SP], #8*/
	blr X0
	ldp X1, X2, [SP, #16]
	ldp X3, X4, [SP, #32]
	ldp X5, X6, [SP, #48]
	ldp X7, X8, [SP, #64]
	ldp X9, X10, [SP, #80]
	ldp X11, X12, [SP, #96]
	ldp X13, X14, [SP, #112]
	ldp X15, X16, [SP, #128]
	ldp X17, X18, [SP, #144]
	ldp X29, X30, [SP], #160
	ret
msg: .asciz "exception handler %zx, SP=%zx\n"
ENDFUNC(call_handler)

SPROC(unhandled_exception, 2)
	movz x1, 0xff1a, lsl 16
	ldrb w2, [x0], #1
loop:
	ldr w3, [x1, 0x80]
	cbnz w3, loop
	str w2, [x1]
	ldrb w2, [x0], #1
	cbnz w2, loop
hang:
	wfi
	b hang
ENDFUNC(unhandled_exception)
