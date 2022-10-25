// SPDX-License-Identifier: CC0-1.0
#include <asm.h>
#include <aarch64/cortex_a53.h>
#include <aarch64.h>

.text
	.ascii "RK33"
PROC(entry_point, 2)
	isb
	ic iallu
	mov32 x0, (SCR_EL3_RES1 | SCR_EA | SCR_FIQ | SCR_IRQ)
	mov32 x1, (SCTLR_I | SCTLR_SA | SCTLR_EL23_RES1)
	isb
	msr SCR_EL3, x0
	msr SCTLR_EL3, x1
	cortex_a53_init x8, var_rev=0x04
	isb

	// init architectural counter
	mov32 x1, 0x16e3600	// 24000000 (timer ticks per second)
	mov32 x2, 0xff8680a0
	msr CNTFRQ_EL0, x1
	mov x1, #0xffffffff
	str wzr, [x2, #28]	// stop the timer
	stp w1, w1, [x2]		// set to start at -1
	mov x1, #1
	str w1, [x2, #28]	// start the timer
	
	mov x8, #0xff1a0000	// UART address, this stays in X8 for the rest of entry.S
	mov x1, #0x83	// LCR: 8 data bits, divisor latch access
	str w1, [x8, #12]
	mov x1, #CONFIG_UART_CLOCK_DIV
	str w1, [x8]
	mov x1, #3	// LCR: 8 data bits
	str w1, [x8, #12]
	mov x1, #0x33
	str w1, [x8, #8]	// reset and enable FIFO
	mov32 x1, 0xff77e028	// GRF_GPIO4C_IOMUX
	mov32 x2, 0x03c00140
	str w2, [x1]	// mux out UART2

	// print greeting
	adr x9, greeting
	mov x10, #(greeting_end - greeting)
0:
	ldr x11, [x9], #8
1:
	str w11, [x8]	// UART will ignore all but low byte
	lsr x11, x11, #8
	cbnz x11, 1b
	subs x10, x10, #8
	b.hi 0b

0:	wfi
	b 0b
ENDFUNC(entry_point)

.align 3
greeting:
.ascii CONFIG_GREETING
greeting_end:

.align 2
