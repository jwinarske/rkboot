#include "config.h"
#include <asm.h>
#include <aarch64.h>
#include <cortex_a53.h>

TEXTSECTION(.text.entry)
#if CONFIG_FIRST_STAGE
.ascii "RK33"
#endif
entry_point: .global entry_point
#ifdef CONFIG_FIRST_STAGE
	cortex_a53_init x1, var_rev=0x04
	isb
#endif
	stp x29, x30, [sp, #-48]!
	mrs x0, SCTLR_EL3
	mrs x1, VBAR_EL3
	mrs x2, SCR_EL3
	add x29, sp, #0
	stp x0, x1, [x29, #16]
	str x2, [x29, #32]
	orr x1, x0, #SCTLR_I
	ic iallu
	isb
	msr DAIFSet, 15
	mov32 x0, (SCR_EL3_RES1 | SCR_EA | SCR_FIQ | SCR_IRQ)
	msr SCR_EL3, x0
	msr SCTLR_EL3, x1
	isb

#if CONFIG_FIRST_STAGE
mov x1, #0x16e0000	// 24000000 (timer ticks per second)
movk x1, #0x3600
msr CNTFRQ_EL0, x1
mov x2, #0xff860000
movk x2, #0x80a0
mov x1, #0xffffffff
str wzr, [x2, #28]	// stop the timer
stp w1, w1, [x2]		// set to start at -1
mov x1, #1
str w1, [x2, #28]	// start the timer

isb	// make sure the SCTLR and CNTFRQ writes have taken effect

mov x8, #0xff1a0000	// UART address, this stays in X8 for the rest of entry.S
mov x1, #0x83	// LCR: 8 data bits, divisor latch access
str w1, [x8, #12]
mov x1, #CONFIG_UART_CLOCK_DIV
str w1, [x8]
mov x1, #3	// LCR: 8 data bits
str w1, [x8, #12]
mov x1, #0x33
str w1, [x8, #8]	// reset and enable FIFO
mov x1, #0xff770000	// GRF_GPIO4C_IOMUX
movk x1, #0xe028
mov x2, #0x03c00000
movk x2, #0x0140
str w2, [x1]	// mux out UART2

dsb sy

adr x1, greeting
adr x2, post_greeting
b print

.align 2
greeting:
.ascii CONFIG_GREETING
.align 2
post_greeting:

mov x1, #0xff760000
mov x2, #0xff000000
movk x2, #0x0200
str w2, [x1, #0x168]	// clock the SARADC down to 8 MHz
mov x1, #0xff100000	// SARADC
mov x2, #0x29	// power up channel 1, enable interrupt
str w2, [x1, #8]
wait_for_adc:
	yield
	ldr w2, [x1, 8]
	tbz x2, 6, wait_for_adc
ldr x2, [x1]
str wzr, [x1, #8]	// power down SARADC
cmp x2, #255
b.hi prepare_main

recovery_mode:
adr x1, recovery_msg
adr x2, brom_jump
b print
.align 2
recovery_msg:
.ascii "recovery\r\n"
.align 2
brom_jump:
msr SCTLR_EL3, x0
isb
b brom_recovery_mode_entry

print:
ldr w3, [x1], #4
print_loop:
		ands w4, w3, #0xff
		lsr x3, x3, #8
		str w4, [x8]
		cbnz x3, print_loop
	ldr w3, [x1], #4
	cmp x1, x2
	b.ls print_loop
br x2
#endif

prepare_main:
	adrp x1, __exc_base__
	msr VBAR_EL3, x1

	aarch64_misc_init x1, x2
	isb

adr x1, __bss_start__
adr x2, __bss_noinit__
zero_bss:
	stp xzr, xzr, [x1], #16
	cmp x1, x2
	b.ls zero_bss

	msr DAIFClr, #15
	add x0, x29, #16
b main
