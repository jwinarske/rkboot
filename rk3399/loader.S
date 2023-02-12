// SPDX-License-Identifier: CC0-1.0
#include <rk3399/loader.h>
#include <asm.h>
#include <aarch64/cortex_a53.h>
#include <aarch64.h>
#include <hw/dwc3.h>

// Sub-section use:
// .text 0  pre-relocation (setup) code
// .text 1  pre-relocation (setup) rodata
// .text 2  common code
// .text 3  common rodata
// .text 4  post-relocation (transfer) code
// .text 5  post-relocation (transfer) rodata

// PTE macros
#define PAGE(attridx) (VMSAV8_64_PAGE(attridx) | VMSAV8_64_AF | VMSAV8_64_OSH)
#define MAP_NULL 0
#define MAP_CODE (PAGE(0) | VMSAV8_64_RO)
#define MAP_RO (PAGE(0) | VMSAV8_64_RO | VMSAV8_64_XN)
#define MAP_RW (PAGE(0) | VMSAV8_64_XN)
#define MAP_DEV (PAGE(1) | VMSAV8_64_XN)
#define MAP_UNCACHED (PAGE(2) | VMSAV8_64_XN)

#define TOC_OFFS(n) ((n) - toc - 0x100)
.text 1
.align 3
toc:

.text 0
	.ascii "RK33"
PROC(entry_point, 2)
	mov32 x0, (SCR_EL3_RES1 | SCR_EA | SCR_FIQ | SCR_IRQ)
	mov32 x1, (SCTLR_I | SCTLR_SA | SCTLR_EL23_RES1)
	msr SCR_EL3, x0
	msr SCTLR_EL3, x1
	cortex_a53_init x8, var_rev=0x04
	isb
#if ENV_VERBOSITY >= 4
	stp x29, x30, [sp, #-64]!
	add x29, sp, #0
	stp x27, x28, [x29, #16]
	stp x25, x26, [x29, #32]
	stp x23, x24, [x29, #48]
#endif

	adr x28, toc + 0x100
	// init architectural counter
	ldp w1, w2, [x28, #TOC_OFFS(timer_data)]
	msr CNTFRQ_EL0, x1
	mov x1, #0xffffffff
	str wzr, [x2, #28]	// stop the timer
	stp w1, w1, [x2]		// set to start at -1
	mov x1, #1
	str w1, [x2, #28]	// start the timer

.text 1
.align 3
timer_data:
	.4byte 0x16e3600	// 24000000 (timer ticks per second)
	.4byte 0xff8680a0	// STimer11 address

.text 0
	mov x8, #0xff1a0000
	mov x1, #0x83	// LCR: 8 data bits, divisor latch access
	str w1, [x8, #12]
	mov x1, #CONFIG_UART_CLOCK_DIV
	str w1, [x8]
	mov x1, #3	// LCR: 8 data bits
	str w1, [x8, #12]
	mov x1, #0x33
	str w1, [x8, #8]	// reset and enable FIFO
	ldp w1, w2, [x28, #TOC_OFFS(uart_setup)]
	str w2, [x1]	// mux out UART2

.text 1
.align 2
uart_setup:
	.4byte 0xff77e028	// GRF_GPIO4C_IOMUX
	.4byte 0x03c00140	// mux out UART2 TX and RX

.text 0
	// print greeting
	adr x9, greeting
0:	// for each 8 bytes of greeting
	ldr x11, [x9], #8
1:	// for each byte of greeting
	str w11, [x8]	// UART will ignore all but low byte
	cmp x11, #' '
	lsr x11, x11, #8
	cbnz x11, 1b
	b.hs 0b

.text 1
.align 3
greeting:
	.ascii CONFIG_GREETING "\r"

.text 0
	// zero out the top 16 KiB of main SRAM for page tables
	mov32 x9, 0xff8ec000
0:	// for each 16 B of page table
	stp xzr, xzr, [x9], #16
	tbz x9, 16, 0b	// stop at 0xff8f0000

	// set initial PTEs: identity-map this first page and map
	// 0xff8ef000, the last-level page table, to 0xff9ff000
	adr x9, initial_ptes
	adr x10, initial_pte_addrs
	mov x11, #5
0:	// for each initial PTE
	ldr x0, [x9], #8
	ldr w1, [x10], #4
	subs x11, x11, #1
	str x0, [x1]
	b.ne 0b

.text 1
.align 3
initial_ptes:
	.8byte 0xff8ed003, 0xff8ee003, 0xff8ef003, (0xff8c2000 | MAP_CODE)
	.8byte (0xff8ef000 | MAP_RW)

initial_pte_addrs:
	.4byte 0xff8ec000, 0xff8ed018, 0xff8eefe0, 0xff8ef610
	.4byte 0xff8efff8

.text 0
	// enable MMU
	ldr x9, mmu_setup
	mov32 x10, 0xff8ec000
	ldp w11, w12, [x28, TOC_OFFS(mmu_setup + 8)]
	msr MAIR_EL3, x9
	msr TTBR0_EL3, x10
	msr TCR_EL3, x11
	dsb sy
	isb
	msr SCTLR_EL3, x12
	isb

.text 1
.align 3
mmu_setup:
	.8byte 0xbb4400ff
	.4byte (TCR_EL3_RES1 | TCR_REGION0(TCR_INNER_CACHED | TCR_OUTER_CACHED | TCR_OUTER_SHARED | TCR_4K_GRANULE | TCR_TxSZ(16)) | TCR_PS(0))
	.4byte (SCTLR_EL23_RES1 | SCTLR_I | SCTLR_SA | SCTLR_M | SCTLR_C)

.text 0
	// install secondary mappings
	adr x0, secondary_ptes
	ldp w1, w27, [x28, TOC_OFFS(pgtab_uart_addrs)]
0:	// for each pair of secondary mappings
	ldp x2, x3, [x0], #16
	stp x3, x2, [x1], #-16
	cbnz x3, 0b

.text 1
.align 2
pgtab_uart_addrs:
	.4byte (LOADER_VA_PGTAB + 0xfe8), LOADER_VA_UART2
.align 4
secondary_ptes:
#define X(n, addr, flags) (addr | flags),
	.8byte ENUM_LOADER_MAPPINGS(X) 0
#undef X
// the PTEs above are copied in pairs, this ensures that at least
// the second one is a zero (terminating condition).
.align 4

.text 0
	// another life-sign
	mov x0, #'\n'
	str x0, [x27]

#if ENV_VERBOSITY >= 8
	mov32 x5, LOADER_VA_PGTAB
	add x6, x5, #0x1000
	bl dumpmem
	mov32 x5, LOADER_VA_BROM_DATA
	add x6, x5, #0x2000
	bl dumpmem
#endif

	// top of the stack page, start of the uncached page
	mov32 x26, LOADER_VA_LOADER_UNCACHED
	// setup stack
	sub sp, x26, #LOADER_STACKFRAME_SIZE
	sub x29, x26, #LOADER_STACKFRAME_SIZE

	// clear both stack and uncached page
	sub x0, x26, 0x1000
	mov x1, #0x2000
	bl zero_range

	// stop events from being written out to system RAM
	// by masking the interrupt
	adr x9, otg_data
	ldp w10, w11, [x9]
	ldp w12, w13, [x9, #8]
	add x14, x10, #8	// GEVNTSIZ
	str w11, [x14]
	ldar w11, [x14]	// sync with DMA

	// migrate the event buffer
	mov32 x0, (LOADER_VA_BROM_DATA + 0x210)
	add x1, x26, #0x200
	mov x3, #0x100
	bl copy
	stp w12, wzr, [x10]	// GEVNTADR

	// continue event writing
	stlr w13, [x14]	// GEVNTSIZ
.text 1
.align 2
otg_data:
	.4byte (LOADER_VA_OTG0 + DWC3_GEVNT), 0x80000100, 0xff8eb200, 0x100

.text 0
#if ENV_VERBOSITY >= 7
	add x5, x26, 0x200
	add x6, x26, 0x300
	bl dumpmem
#endif

	// load the BROM's event dequeue pointer
	mov32 x0, (LOADER_VA_BROM_DATA + 0x41c)
	ldr x0, [x0]
	add x0, x0, #4
	strb w0, [x29, #LOADER_EVTBUF_POS_OFF]
#if ENV_VERBOSITY >= 5
	and x0, x0, #0xff
	bl rhex32
	mov x0, #' '
	str w0, [x27]
	ldr w0, [x10, #12]
	bl rhex32
	bl endline
#endif

	mov x0, #4
	str w0, [x10, #12]

	mov32 x9, LOADER_VA_OTG0 + DWC3_DEPCMD

	// configure EP2OUT and EP1IN
	ldp w0, w1, [x28, #TOC_OFFS(ep2out_config)]
	ldp w3, w4, [x28, #TOC_OFFS(ep1in_config)]
	mov x2, #(DWC3_DEPCMD_SET_EP_CFG | DWC3_DEPCMD_ACT | DWC3_DEPCMD_IOC)
	stp wzr, w4, [x9, #0x30]
	str w3, [x9, #0x38]
	stp w2, wzr, [x9, #0x3c]
	stp w1, w0, [x9, #0x44]
	str w2, [x9, #0x4c]
.text 1
.align 2
ep2out_config:
	.4byte (DWC3_DEPCFG0(512, 0, 0, 0) | DWC3_DEPCFG0_BULK_EP)
	.4byte (DWC3_DEPCFG1(0, 0, 2, 0) | DWC3_DEPCFG1_INTEN_XFER_COMPLETE | DWC3_DEPCFG1_INTEN_FIFO_ERR)
ep1in_config:
	.4byte (DWC3_DEPCFG0(512, 1, 0, 0) | DWC3_DEPCFG0_BULK_EP)
	.4byte (DWC3_DEPCFG1(0, 0, 1, 1) | DWC3_DEPCFG1_INTEN_XFER_COMPLETE | DWC3_DEPCFG1_INTEN_FIFO_ERR)

.text 0
	add x2, x9, #0x4c
	bl wait_depcmd
	add x2, x9, #0x3c
	bl wait_depcmd

	// make dwc3 assign a transfer resource to EP2OUT and EP1IN
	mov x0, #1
	mov x2, #(DWC3_DEPCMD_SET_XFER_RSC_CFG | DWC3_DEPCMD_IOC | DWC3_DEPCMD_ACT)
	stp wzr, wzr, [x9, #0x40]
	stp wzr, wzr, [x9, #0x30]
	str w0, [x9, #0x48]
	str w0, [x9, #0x38]
	str w2, [x9, #0x4c]
	str w2, [x9, #0x3c]
	add x2, x9, #0x4c
	bl wait_depcmd
	add x2, x9, #0x3c
	bl wait_depcmd

	// enable EP0, EP1IN and EP2OUT
	mov x0, #0x1b
	str w0, [x9, #(DWC3_DALEPENA - DWC3_DEPCMD)]

	// complete status phase of the request that started us
	mov x0, #((DWC3_DTRB_STATUS3 | DWC3_DTRB_HWO | DWC3_DTRB_LAST | DWC3_DTRB_IOC) << 32)
	stp xzr, x0, [x26]
	mov32 x0, LOADER_PA_UNCACHED
	stp wzr, w0, [x9, #0x10]
	str wzr, [x9, #0x18]
	add x1, x9, #0x1c
	mov x0, #(DWC3_DEPCMD_START_XFER | DWC3_DEPCMD_IOC | DWC3_DEPCMD_ACT)
	stlr w0, [x1]

	// accept 512 B on EP2OUT
	ldp x0, x1, [x28, #TOC_OFFS(command_trb)]
	stp w0, wzr, [x26, #0x10]
	str x1, [x26, #0x18]
	lsr x0, x0, #32
	stp wzr, w0, [x9, #0x40]
	str wzr, [x9, #0x48]
	add x1, x9, #0x4c
	mov x0, #(DWC3_DEPCMD_START_XFER | DWC3_DEPCMD_IOC | DWC3_DEPCMD_ACT)
	stlr w0, [x1]
.text 1
.align 3
command_trb:
	// this is written zero-extended to the first 8 bytes of the DTRB
	.4byte (LOADER_PA_UNCACHED + 0x400)
	// physical address of the DTRB
	.4byte (LOADER_PA_UNCACHED + 0x10)
	.4byte 0x200	// size
	.4byte (DWC3_DTRB_NORMAL | DWC3_DTRB_HWO | DWC3_DTRB_IOC | DWC3_DTRB_LAST)	// control word

.text 0
	add x2, x9, #0x1c
	bl wait_depcmd
	add x2, x9, #0x4c
	bl wait_depcmd

#if ENV_VERBOSITY >= 5
	bl endline

	add x5, x26, #LOADER_EVTBUF_OFF
	add x6, x26, #(LOADER_EVTBUF_OFF + 256)
	bl dumpmem
#endif

	mov32 x8, LOADER_VA_OTG0 + DWC3_GEVNT
next_dwc3_event:
	add x0, x8, #12
	ldar w0, [x0]
	cbz x0, next_dwc3_event

	// bump dequeue position, compute pointer
	ldrb w0, [x29, #LOADER_EVTBUF_POS_OFF]
	add x1, x0, #4
	add x0, x0, x26
	add x0, x0, #LOADER_EVTBUF_OFF
	strb w1, [x29, #LOADER_EVTBUF_POS_OFF]

	ldr w10, [x0]	// load event
	mov x1, #4
	str w1, [x8, #12]	// ack 4 bytes
#if ENV_VERBOSITY >= 4
	mov x0, '*'
	str w0, [x27]
	mov x0, x10
	bl rhex32
	bl endline
#endif

	// === decode event ===
	// we don't want to deal with device events (link state
	// change, disconnect, reset, …): just disconnect if they
	// appear
	tbnz x10, 0, halt
	ubfx x11, x10, 6, 4
	cmp x11, #7	// command completion
	b.eq next_dwc3_event
	and x11, x10, 0xfff
	cmp x11, #(DWC3_DEPEVT(0) | DWC3_DEPEVT_IN | DWC3_DEPEVT_XFER_COMPLETE)
	b.eq  next_dwc3_event
	cmp x11, #(DWC3_DEPEVT(2) | DWC3_DEPEVT_XFER_COMPLETE)
	b.eq ep2out_complete
	cmp x11, #(DWC3_DEPEVT(1) | DWC3_DEPEVT_IN | DWC3_DEPEVT_XFER_COMPLETE)
	b.eq next_dwc3_event

halt:
#if ENV_VERBOSITY >= 4
	mov x0, #'@'
	str w0, [x27]
#endif
	add x0, x8, #(DWC3_DCFG - DWC3_GEVNT)
	str wzr, [x0, #4]

0:	wfi
	b 0b

ep2out_complete:
	ldp x0, x1, [x28, TOC_OFFS(out_dtrb)]
	stp w0, wzr, [x26, #0x20]
	str x1, [x26, #0x28]
	lsr x0, x0, #32
	stp wzr, w0, [x9, #0x30]
	str wzr, [x9, #0x38]
	add x1, x9, #0x3c
	mov x0, #(DWC3_DEPCMD_START_XFER | DWC3_DEPCMD_IOC | DWC3_DEPCMD_ACT)
	stlr w0, [x1]
#if ENV_VERBOSITY >= 5
	mov x5, x26
	add x6, x26, #0x500
	bl dumpmem
#endif

.text 1
.align 3
out_dtrb:
	.4byte (LOADER_PA_UNCACHED + 0x200)
	// physical address of the DTRB
	.4byte (LOADER_PA_UNCACHED + 0x20)
	.4byte 0x100	// size
	.4byte (DWC3_DTRB_NORMAL | DWC3_DTRB_HWO | DWC3_DTRB_IOC | DWC3_DTRB_LAST)	// control word

.text 0
	b next_dwc3_event
ENDFUNC(entry_point)

.text 2
SPROC(wait_depcmd_, 2)
#if ENV_VERBOSITY >= 6
	mov x7, x30
#if ENV_VERBOSITY >= 7
	bl rhex32
#endif
	mov x0, #'.'
	bl putc
	mov x30, x7
#endif
wait_depcmd:
#if ENV_VERBOSITY >= 4
#endif
	ldr w0, [x2]
	tbnz x0, 10, wait_depcmd_
	ret
ENDFUNC(wait_depcmd_)

SPROC(copy, 2)
0:	// for each 16 B
	ldp x4, x5, [x0], #16
	stp x4, x5, [x1], #16
	subs x3, x3, #16
	b.hi 0b
	ret
ENDFUNC(copy)

SPROC(zero_range, 2)
0:	// for each 64 B (known dc zva block size, alignment of
	// range is assumed)
	dc zva, x0
	add x0, x0, #64
	subs x1, x1, #64
	b.hi 0b
	ret
ENDFUNC(zero_range)

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
