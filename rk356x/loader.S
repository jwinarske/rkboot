// SPDX-License-Identifier: CC0-1.0
#include <aarch64.h>
#include <asm.h>

.text 0
	.ascii "RK35"
	// CPU version is r2p0, DSU version is r4p0.
	// TF-A doesn't have any errata code for that CPU version
	// DSU erratum 2313941 is listed as unfixed as of r3p1,
	// but the mitigation (disabling SCLK gating) doesn't seem
	// to be in use by the Cortex-A55 reset path

	// light the red LED
	mov x0, #0xfdd60000
	orr w1, wzr, #0x10001000
	str w1, [x0, #4]
	str w1, [x0, #12]

	stp x29, x30, [sp, #-64]!
	add x29, sp, #0
	stp x27, x28, [x29, #16]
	stp x25, x26, [x29, #32]
	stp x23, x24, [x29, #48]

	// set up UART
	mov x27, #0xfe660000
	mov x1, #0x83	// LCR: 8 data bits, divisor latch access
	str w1, [x27, #12]
	mov x1, #CONFIG_UART_CLOCK_DIV
	str w1, [x27]
	mov x1, #3	// LCR: 8 data bits
	str w1, [x27, #12]
	mov x1, #0x33
	str w1, [x27, #8]	// reset and enable FIFO
	// mux out UART
	mov x0, #0xfdc20000
	mov32 x1, 0x00770011
	str w1, [x0, #0x18]

	mov x0, #'!'
	str w0, [x27]

	adr x0, #0
	bl rhex64
	mov x0, #' '
	str w0, [x27]
	add x0, sp, #0
	bl rhex64
	bl endline

	mrs x0, CNTPCT_EL0
	bl rhex64

	mov32 x2, 0xfdd1c020
	mov x1, #0xffffffff
	str wzr, [x2, #16]	// stop the timer
	stp w1, w1, [x2]	// set end value
	mov x1, #1
	str w1, [x2, #16]	// start the timer

	mov x5, 0xfdcc0000
	mov x6, 0xfdcd0000
	bl dumpmem
	mrs x0, CNTPCT_EL0
	bl rhex64

	add sp, x29, #0
	ldp x23, x24, [x29, #48]
	ldp x25, x26, [x29, #32]
	ldp x27, x28, [x29, #16]
	ldp x29, x30, [sp], #64
	ret

#if ENV_VERBOSITY >= 4
SPROC(dumpmem, 2)
	mov x7, x30
0:	// for each 16B
	mov x0, x5
	bl rhex32
	ldp x0, x4, [x5], #16
	mov x17, #' '
	str w17, [x27]
	bl rhex64
	mov x0, #' '
	str w0, [x27]
	mov x0, x4
	bl rhex64
	bl endline
	cmp x5, x6
	b.lo 0b
	ret x7
ENDFUNC(dumpmem)

SPROC(endline, 2)
	mov x0, #'\r'
	str w0, [x27]
	mov x0, #'\n'
putc:
	str w0, [x27]
uart_idle:
	ldr w0, [x27, 0x80]
	cbnz x0, uart_idle
	ret
ENDFUNC(endline)

SPROC(rhex32, 2)
	mov x1, #8
	b rhex
rhex64:
	mov x1, #16
rhex:
8:	/* write the (reverse) hex repr of x0 to x27, using x1 digits (clobbers x0, x1 and x3) */
	and x3, x0, #15
	cmp x3, #10
	b.lo 1f
		add x3, x3, #('a' - '0' - 10)
1:	add x3, x3, #'0'
	str w3, [x27]
	lsr x0, x0, #4
	sub x1, x1, #1
	cbnz x1, 8b
	ret
ENDFUNC(rhex32)
#endif
