/* SPDX-License-Identifier: CC0-1.0 */
#include <asm.h>

TEXTSECTION(.text.asm.plat_dump_pgtab)
PROC(plat_dump_pgtab, 2)
	mov x6, x30
	.cfi_register x30, x6
	bl plat_asm_get_uart
	adrp x4, pagetables
	ldr x4, [x4, :lo12:pagetables]
	adrp x5, next_pagetable
	ldr w5, [x5, :lo12:next_pagetable]
	add x5, x4, x5, lsl 12
	1:	tst x4, #0x1f
		b.ne 2f
			3:	/* start of line, flush the FIFO */
				ldr w0, [x2, 0x80]
				cbnz x0, 3b
			mov x0, x4
			mov x1, #8
			bl 8f
			mov x0, #':'
			str w0, [x2]
			mov x0, #' '
			str w0, [x2]
	2:	ldr x0, [x4], #8
		mov x1, #16
		bl 8f
		tbnz x4, 4, 2f
		3:	/* every second entry, flush the FIFO */
			ldr w0, [x2, 0x80]
			cbnz x0, 3b
	2:	tst x4, #0x1f
		b.eq 2f
			/* not end of line, write space */
			mov x0, #' '
			str w0, [x2]
			b 1b	/* pagetables are a multiple of 4 entries long, no check needed */
	2:	mov x0, #'\r'
		str w0, [x2]
		mov x0, #'\n'
		str w0, [x2]
		tst x4, #0xfff
		b.ne 1b
		mov x0, #'\r'
		str w0, [x2]
		mov x0, #'\n'
		str w0, [x2]
		cmp x4, x5
		b.lo 1b
	ret x6
.cfi_endproc
8:	/* write the (reverse) hex repr of x0 to x2, using x1 digits (clobbers x0, x1 and x3) */
	.cfi_startproc
	.cfi_same_value x4
	.cfi_same_value x5
	.cfi_same_value x6
	.cfi_same_value x7
	.cfi_same_value x8
	.cfi_same_value x9
	.cfi_same_value x10
	.cfi_same_value x11
	.cfi_same_value x12
	.cfi_same_value x13
	.cfi_same_value x14
	.cfi_same_value x15
	.cfi_same_value x16
	.cfi_same_value x17
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
ENDFUNC(plat_dump_pgtab)

TEXTSECTION(.text.asm.plat_fail)
.cfi_startproc
plat_asm_fail:	.global plat_asm_fail
	/* clobbers x0–2 */
	.cfi_same_value x3
	.cfi_same_value x4
	.cfi_same_value x5
	.cfi_same_value x6
	.cfi_same_value x7
	.cfi_same_value x8
	.cfi_same_value x9
	.cfi_same_value x10
	.cfi_same_value x11
	.cfi_same_value x12
	.cfi_same_value x13
	.cfi_same_value x14
	.cfi_same_value x15
	.cfi_same_value x16
	bl plat_asm_get_uart
	1:	ldr w1, [x2, 0x80]
		yield
		cbnz x1, 1b
	mov x1, #'!'
	str w1, [x2]
	1:	ldrb w1, [x0], 1
		cbz x1, 1f
		str w1, [x2]
		b 1b
1:	wfi;b 1b
.cfi_endproc

.macro get_uart el:req
TEXTSECTION(.text.asm.plat_get_uart)
PROC(plat_asm_get_uart, 2)
	.cfi_same_value x0
	.cfi_same_value x1
	/* returns in x2 */
	.cfi_same_value x3
	.cfi_same_value x4
	.cfi_same_value x5
	.cfi_same_value x6
	.cfi_same_value x7
	.cfi_same_value x8
	.cfi_same_value x9
	.cfi_same_value x10
	.cfi_same_value x11
	.cfi_same_value x12
	.cfi_same_value x13
	.cfi_same_value x14
	.cfi_same_value x15
	.cfi_same_value x16
	.cfi_same_value x17
	mrs x2, sctlr_el\el
	tbnz x2, 0, 1f
	mov x2, 0xff1a0000
	b 2f
1:	adrp x2, console_uart
	ldr x2, [x2, :lo12:console_uart]
2:	ret
ENDFUNC(plat_asm_get_uart)
.endm
get_uart el=CONFIG_EL
