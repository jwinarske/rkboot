/* SPDX-License-Identifier: CC0-1.0 */
#include <asm.h>
.global printf

// FIXME: handle ARMv8.3-CCIDX

/* clobbers registers r8–15; takes cache ID in r7, which it preserves */
.macro apply_one_cache_level cacheop
	msr csselr_el1, x7
	isb
	mrs x15, ccsidr_el1
	and w13, w15, #7	// w13 = log2(linelength) - 4
	ubfx w12, w15, #3, #10	// w12 = max way number, later left-aligned in 32-bit word and counted down in loop
	ubfx w11, w15, #13, #15	// w11 = max set number
	clz w14, w12
	mov w10, #1
	lsl w12, w12, w14	// left-align max way number
	lsl w9, w10, w13	// w9 = set counter decrement
	lsl w10, w10, w14	// w10 = way counter decrement
2:	// each way
	lsl w15, w11, w13	// w15 = set counter
3:	// each set
	orr w8, w12, w7
	orr w8, w8, w15, lsl 4
	dc \cacheop, x8
	subs w15, w15, w9
	b.hs 3b
	subs w12, w12, w10
	b.hs 2b
.endm

/* these functions are used by both the MMU enable/disable helpers below, and platform reset entry functions */
TEXTSECTION(.text.asm.invalidate_cache_level)
PROC(aarch64_invalidate_cache_level, 2)
	apply_one_cache_level isw
	ret
ENDFUNC(aarch64_invalidate_cache_level)

TEXTSECTION(.text.asm.flush_cache_level)
PROC(aarch64_flush_cache_level, 2)
	apply_one_cache_level cisw
	ret
ENDFUNC(aarch64_flush_cache_level)

.macro apply_all_dcache func
	dsb sy
	mrs x17, clidr_el1
	lsr w16, w17, #23	// after this, use w17 to shift through cache levels
	and w16, w16, #0xe	// x16 = LoC << 1
	mov w7, #-2	// w7 = current level of cache << 1
1:	// each level
	add w7, w7, #2
	cmp w7, w16
	b.hs 4f
	and w15, w17, #7
	ubfx w17, w17, #3, #18
	cmp w15, #2
	b.lt 1b
	bl \func
	b 1b
4:	// done
	dsb sy
	isb
.endm

TEXTSECTION(.text.asm.mmu)
PROC(invalidate_dcache_set_sctlr, 2)
	mov x6, x30
	apply_all_dcache func=aarch64_invalidate_cache_level
	msr sctlr_el3, x0
	isb
	mov x30, x6
	ret
ENDFUNC(invalidate_dcache_set_sctlr)

PROC(set_sctlr_flush_dcache, 2)
	msr sctlr_el3, x0
	isb
flush_dcache:
	.global flush_dcache
	mov x6, x30
	apply_all_dcache func=aarch64_flush_cache_level
	mov x30, x6
	ret
ENDFUNC_NESTED(set_sctlr_flush_dcache)
ENDFUNC(flush_dcache)
