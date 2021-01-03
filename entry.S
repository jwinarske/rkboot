#include "config.h"
#include <asm.h>
#include <aarch64.h>
#include <mmu.h>
#include <cortex_a53.h>

#if CONFIG_FIRST_STAGE
	#if defined(CONFIG_EL) && CONFIG_EL != 3
		#error first stage must run in EL3
	#endif
	#define CONFIG_EL 3
#elif CONFIG_EL != 3 && CONFIG_EL != 2
	#error entry only implemented at EL2 and EL3
#endif

.macro entry el:req
TEXTSECTION(.text.entry)
#if CONFIG_FIRST_STAGE
.ascii "RK33"
#endif
entry_point: .global entry_point
	.if \el == 2
		mov x20, x0
	.endif
#if CONFIG_FIRST_STAGE
	cortex_a53_init x1, var_rev=0x04
	isb
#if CONFIG_FIRST_STAGE > 1
	/* save BROM execution state on stack.
	can't assume we even have a stack otherwise */
	stp x29, x30, [sp, #-48]!
	mrs x0, SCTLR_EL3
	mrs x1, VBAR_EL3
	mrs x2, SCR_EL3
	add x29, sp, #0
	stp x0, x1, [x29, #16]
	stp x2, x19, [x29, #32]
#endif
#endif
	ic iallu
	isb
	msr DAIFSet, 15
.if \el == 3
	mov32 x0, (SCR_EL3_RES1 | SCR_EA | SCR_FIQ | SCR_IRQ)
	mov32 x1, (SCTLR_I | SCTLR_SA | SCTLR_EL23_RES1)
	msr SCR_EL3, x0
.elseif \el == 2
	mov x0, #(HCR_FMO | HCR_IMO | HCR_AMO)
	mov32 x1, (SCTLR_I | SCTLR_SA | SCTLR_EL23_RES1)
	msr HCR_EL2, x0
.else
	.error don't support EL\el entry yet
.endif
	msr SCTLR_EL\el, x1
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
	msr VBAR_EL\el, x1

	.if \el == 3
		aarch64_misc_init x1, x2
	.endif
	isb

	adrp x19, pagetables
	ldr x19, [x19, :lo12:pagetables]
	mov x1, x19
	add x2, x19, #0x1000
	1:	stp xzr, xzr, [x1], #16
		cmp x1, x2
		b.lo 1b
	adrp x1, initial_mappings
	add x1, x1, :lo12:initial_mappings
	1:	mov x0, x19
		bl mmu_multimap
		ldr x2, [x0, #8]
		mov x1, x0
		cbnz x2, 1b

.if \el == 2 || \el == 3
	ldr x0, =(MMU_MAIR_VAL)
	ldr x1, =(MMU_TCR_VAL)
.else
	.error MMU setup unknown for EL\el
.endif
	msr TTBR0_EL\el, x19
	msr MAIR_EL\el, x0
	msr TCR_EL\el, x1
	tlbi alle\el

	mov32 x0, (SCTLR_EL23_RES1 | SCTLR_I | SCTLR_SA | SCTLR_M | SCTLR_C)
	bl invalidate_dcache_set_sctlr

	adr x1, __bss_start__
	adr x2, __bss_noinit__
	1:	stp xzr, xzr, [x1], #16
		cmp x1, x2
		b.lo 1b

	adrp x29, boot_stack_end
	ldr x29, [x29, #:lo12:boot_stack_end]
#if CONFIG_FIRST_STAGE > 1
	stp xzr, xzr, [x29, #-32]!
	add x0, sp, #0
	mrs x1, SPSel
	stp x0, x1, [x29, #16]
#else
	stp xzr, xzr, [x29, #-16]!
#endif
	msr SPSel, 1
	add sp, x29, #0
	msr DAIFClr, #15
#if CONFIG_FIRST_STAGE > 1
	bl main
	msr DAIFSet, #15
	ldp x29, x0, [sp, #16]
	msr SPSel, x0
	add sp, x29, #0
	ldp x0, x1, [x29, #16]
	ldp x2, x19, [x29, #32]
	msr VBAR_EL3, x1
	msr SCR_EL3, x2
	orr x0, x0, #SCTLR_I
	bl set_sctlr_flush_dcache
	tlbi alle3
	dsb sy
	ldr x0, [x29, #16]
	msr SCTLR_EL3, x0
	add sp, x29, #0
	ldp x29, x30, [sp], #48
	isb
	ret
#else
	.if \el == 2
		mov x0, x20
	.endif
	mov x30, #0
	b main
#endif

.if \el == 3
TEXTSECTION(.text.asm.next_stage)
next_stage:	.global next_stage
	msr DAIFSet, #15
	mov x19, x0
	mov x20, x1
	mov x21, x2
	mov x22, x3
	mov x23, x4
	add sp, x5, #0
	mov w1, #0xffff4800	/* this is the BROM exception vector base, just to have it safely hang/ignore any exceptions that do occur, instead of going to our handlers that will not work anymore after handoff */
	msr VBAR_EL3, x1
	mov32 x0, (SCTLR_I | SCTLR_SA | SCTLR_EL23_RES1)
	bl set_sctlr_flush_dcache
	tlbi alle3
	dsb sy
	mov x0, x19
	mov x1, x20
	mov x2, x21
	mov x3, x22
	mov32 x4, (SCTLR_SA | SCTLR_EL23_RES1)
	msr SCTLR_EL3, x4
	isb
	ic iallu
	isb
	br x23
.endif
.endm

entry el=CONFIG_EL
