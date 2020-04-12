.section .text.exc, "ax", %progbits
.macro exc_handler
.align 7
	adr x0, 3f
	movz x1, 0xff1a, lsl 16
	ldrb w2, [x0], #1
0:
	ldr w3, [x1, 0x80]
	cbnz w3, 0b
	str w2, [x1]
	ldrb w2, [x0], #1
	cbnz w2, 0b
2:
	wfi
	b 2b
3:
.endm

.global exc_vector
.align 12
exc_vector:
exc_handler
.asciz "sync SP_EL0"
exc_handler
.asciz "IRQ SP_EL0"
exc_handler
.asciz "FIQ SP_EL0"
exc_handler
.asciz "SError SP_EL0"
exc_handler
.asciz "sync SP_ELx"
exc_handler
.asciz "IRQ SP_ELx"
exc_handler
.asciz "FIQ SP_ELx"
exc_handler
.asciz "SError SP_ELx"
exc_handler
.asciz "sync aarch64"
exc_handler
.asciz "IRQ aarch64"
exc_handler
.asciz "FIQ aarch64"
exc_handler
.asciz "SError aarch64"
exc_handler
.asciz "sync aarch32"
exc_handler
.asciz "IRQ aarch32"
exc_handler
.asciz "FIQ aarch32"
exc_handler
.asciz "SError aarch32"
