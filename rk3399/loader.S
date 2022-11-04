// SPDX-License-Identifier: CC0-1.0
#include <rk3399/loader.h>
#include <asm.h>
#include <aarch64/cortex_a53.h>
#include <aarch64.h>

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

.text 0
	.ascii "RK33"
PROC(entry_point, 2)
	mov32 x0, (SCR_EL3_RES1 | SCR_EA | SCR_FIQ | SCR_IRQ)
	mov32 x1, (SCTLR_I | SCTLR_SA | SCTLR_EL23_RES1)
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
	
	mov x8, #0xff1a0000
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
	ldr x9, mair
	mov32 x10, 0xff8ec000
	mov32 x11, (TCR_EL3_RES1 | TCR_REGION0(TCR_INNER_CACHED | TCR_OUTER_CACHED | TCR_OUTER_SHARED | TCR_4K_GRANULE | TCR_TxSZ(16)) | TCR_PS(0))
	mov32 x12, (SCTLR_EL23_RES1 | SCTLR_I | SCTLR_SA | SCTLR_M | SCTLR_C)
	msr MAIR_EL3, x9
	msr TTBR0_EL3, x10
	msr TCR_EL3, x11
	dsb sy
	isb
	msr SCTLR_EL3, x12
	isb

.text 1
.align 3
mair:
	.8byte 0xbb4400ff

.text 0
	// install secondary mappings
	adr x0, secondary_ptes
	mov32 x1, (LOADER_VA_PGTAB + 0xfe8)
0:	// for each pair of secondary mappings
	ldp x2, x3, [x0], #16
	stp x3, x2, [x1], #-16
	cbnz x3, 0b

.text 1
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
	mov32 x2, LOADER_VA_UART2
	mov x0, #'\n'
	str x0, [x2]

#if ENV_VERBOSITY >= 6
	mov32 x5, LOADER_VA_PGTAB
	add x6, x5, #0x1000
	bl dumpmem
	mov32 x5, LOADER_VA_BROM_DATA
	add x6, x5, #0x2000
	bl dumpmem
#endif

	// top of the stack page, start of the uncached page
	mov32 x8, LOADER_VA_LOADER_UNCACHED

	// clear both stack and uncached page
	sub x0, x8, 0x1000
	mov x1, #0x2000
	bl zero_range

	// stop events from being written out to system RAM
	// by masking the interrupt
	adr x9, otg_data
	ldp w10, w11, [x9]
	ldp w12, w13, [x9, #8]
	str w11, [x10, #8]	// GEVNTSIZ

	// migrate the event buffer
	mov32 x0, (LOADER_VA_BROM_DATA + 0x210)
	add x1, x8, #0x200
	mov x3, #0x100
	bl copy
	stp w12, wzr, [x10]	// GEVNTADR

	// continue event writing
	str w13, [x10, #8]	// GEVNTSIZ
	dsb sy
.text 1
.align 2
otg_data:
	.4byte (LOADER_VA_OTG0 + LOADER_DWC3_GEVNT), 0x80000100, 0xff8eb200, 0x100

.text 0
	mov x0, #'\n'
	str x0, [x2]

	add x5, x8, 0x200
	add x6, x8, 0x300
	bl dumpmem

	// load the BROM's event dequeue pointer
	mov32 x0, (LOADER_VA_BROM_DATA + 0x41c)
	ldr x0, [x0]
	bl rhex32
	mov x0, #' '
	str w0, [x2]
	ldr w0, [x10, #12]
	bl rhex32
	bl endline

	mov32 x5, (LOADER_VA_BROM_DATA + 0x210)
	add x6, x5, #0x100
	bl dumpmem

	mov x0, #4
	str w0, [x10, #12]

0:	wfi
	b 0b
ENDFUNC(entry_point)

.text 2
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
	str w17, [x2]
	bl rhex64
	mov x0, #' '
	str w0, [x2]
	mov x0, x4
	bl rhex64
	bl endline
	cmp x5, x6
	b.lo 0b
	ret x7
ENDFUNC(dumpmem)

SPROC(endline, 2)
	mov x0, #'\r'
	str w0, [x2]
	mov x0, #'\n'
	str w0, [x2]
uart_idle:
	ldr w0, [x2, 0x80]
	cbnz x0, uart_idle
	ret
ENDFUNC(endline)

SPROC(rhex32, 2)
	mov x1, #8
	b rhex
rhex64:
	mov x1, #16
rhex:
8:	/* write the (reverse) hex repr of x0 to x2, using x1 digits (clobbers x0, x1 and x3) */
	and x3, x0, #15
	cmp x3, #10
	b.lo 1f
		add x3, x3, #('a' - '0' - 10)
1:	add x3, x3, #'0'
	str w3, [x2]
	lsr x0, x0, #4
	sub x1, x1, #1
	cbnz x1, 8b
	ret
ENDFUNC(rhex32)
#endif
