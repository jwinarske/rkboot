#include <asm.h>
.section .text.exc, "ax", %progbits

.macro exc_handler name spsel
.align 7
	msr SPSel, #\spsel
	cmp SP, 0
	b.eq 0f
	stp X29, X30, [SP, #-192]!
	add X29, SP, #0
	stp X0, X1, [SP, #32]
	adrp x1, \name
	ldr x1, [x1, #:lo12:\name]
	cbz x1, 0f
	bl call_handler
	ldp X0, X1, [SP, #32]
	ldp X29, X30, [SP], #192
	eret
0:	/* cannot handle (no stack or routine set up) */
	adr x0, 1f
	b unhandled_exception
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
exc_handler sync_exc_handler_spx, spsel=1
.asciz "sync SP_ELx"
exc_handler irq_handler_spx, spsel=1
.asciz "IRQ SP_ELx"
exc_handler fiq_handler_spx, spsel=1
.asciz "FIQ SP_ELx"
exc_handler serror_handler_spx, spsel=1
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
	stp X2, X3, [SP, #48]
	stp X4, X5, [SP, #64]
	stp X6, X7, [SP, #80]
	stp X8, X9, [SP, #96]
	stp X10, X11, [SP, #112]
	stp X12, X13, [SP, #128]
	stp X14, X15, [SP, #144]
	stp X16, X17, [SP, #160]
	stp X18, X30, [SP, #176]
	add x0, sp, #0
	blr X1
	ldp X2, X3, [SP, #48]
	ldp X4, X5, [SP, #64]
	ldp X6, X7, [SP, #80]
	ldp X8, X9, [SP, #96]
	ldp X10, X11, [SP, #112]
	ldp X12, X13, [SP, #128]
	ldp X14, X15, [SP, #144]
	ldp X16, X17, [SP, #160]
	ldp X18, X30, [SP, #176]
	ret
msg: .asciz "exception handler %zx, SP=%zx\n"
ENDFUNC(call_handler)

.macro unh_exc el
SPROC(unhandled_exception, 2)
	mrs x1, SCTLR_EL\el
	tbnz x1, 0, 1f
	mov x1, 0xff1a0000
	b 2f
1:	adrp x1, console_uart
	ldr x1, [x1, :lo12:console_uart]
2:	ldrb w2, [x0], #1
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
.endm
unh_exc el=CONFIG_EL

.section .bss, "wa", %nobits
.macro vector name
.align 3
.global \name
.type \name, %object
.size \name, 8
\name:
	.space 8, 0 /* handler address, used if non-zero */
.endm
.macro vectors suffix
	vector sync_exc_handler_\suffix
	vector irq_handler_\suffix
	vector fiq_handler_\suffix
	vector serror_handler_\suffix
.endm
vectors sp0
vectors spx
vectors aarch64
vectors aarch32
