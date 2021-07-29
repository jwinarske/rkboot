/* SPDX-License-Identifier: CC0-1.0 */
#include <asm.h>

.section .text.asm.memset
PROC(memset, 2)
	cbz x2, memset_out
	mov x3, x0
	and x1, x1, #0xff
	orr x4, xzr, #0x0101010101010101
	mul x1, x1, x4
	tbz x2, 0, aligned2
		strb w1, [x3], #1
		sub x2, x2, #1
aligned2:
	tbz x2, 1, aligned4
		subs x2, x2, #2
		b.lo out2
		strh w1, [x3], #2
aligned4:
	tbz x2, 2, aligned8
		subs x2, x2, #4
		b.lo out4
		str w1, [x3], #4
aligned8:
	subs x2, x2, 8
	b.lo out8
	loop:
		str x1, [x3], #8
		subs x2, x2, #8
		b.hs loop
out8:
	tbz x2, 2, out4
		str w1, [x3], #4
out4:
	tbz x2, 1, out2
		strh w1, [x3], #2
out2:
	tbz x2, 0, memset_out
		strb w1, [x3], #1
memset_out:
	ret
ENDFUNC(memset)

.section .text.asm.memcpy
PROC(memcpy, 2)
	subs x2, x2, #1
	b.lo memcpy_out
	mov x3, x0
memcpy_loop:
	ldrb w4, [x1], #1
	strb w4, [x3], #1
	subs x2, x2, #1
	b.hs memcpy_loop
memcpy_out:
	ret
ENDFUNC(memcpy)
