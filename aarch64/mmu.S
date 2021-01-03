.macro extract_address out:req in:req lpa_enabled=0
	/* extracts the address from an VMSAv8-64 descriptor */
	.if \lpa_enabled
		and \out, \in, #0x0000fffffffff000
		orr \out, \out, \out, lsl 36
		and \out, \out, #0x000fffffffff0000
	.else
		and \out, \in, #0x0000fffffffff000
	.endif
.endm
.macro insert_address out:req in:req lpa_enabled=0
	/* rearranges the bits in an address to the form of an VMSAv8-64 descriptor */
	.if \lpa_enabled
		and \out, \in, #0x000fffffffff0000
		orr \out, \out, lsr 36
	.endif
	and \out, \out, #0x0000fffffffff000
.endm

/*
This construction may look like magic, and it kinda is, but it gets a lot clearer if you understand what it's trying to accomplish.
In the C code you can find a dummied-out version of the multimap function.
Understanding its behavior is critical to understanding this construction.
This macro implements the loop body of the multimap function, with all the lookup table values instantiated for a particular translation table level.
Changing the value of the 'level' variable in the C version is represented by jumping to one of the passed-in labels:
* next_level_label for level n must be connected to the entry to the macro for level n+1.
* prev_level_label for level n must be connected to return_label for level n-1.
The function return is represented by jumping to done_label.
An iteration for level n in the C version only accesses table_ptrs[n] and table_ptrs[n+1].
These are assigned registers passed as table_ptr and next_level_ptr, respectively.
To reduce code size, code shared between levels is factored out as subroutines which may modify algorithm state, namely addr, desc and next_map_addr

Notes:
* To reduce the number of registers used, some registers are used for alternative purposes.
CPP #defines are used for clarification in some of these cases.
* code-folding away the development assertions should make the
* assertions check for things that the caller should have checked, dev assertions catch paths that should be unreachable even with bad input (and are left out if dev assertions are disabled)
*/
.macro multimap_level\
	/* state variables in the function */\
	addr:req desc:req next_map_addr:req\
	/* communication between the different levels */\
	table_ptr:req next_level_ptr:req\
	/* clobber registers */\
	pos:req scratch1:req\
	/* info for about this level */\
	shift:req is_mappable:req\
	/* control flow labels; may be blank, implying that the relevant level does not exist*/\
	done_label:req return_label prev_level_label next_level_label\
	/* subroutines */\
	get_next_mapping alloc_table\
	/* global settings */\
	granule_shift:req lpa_enabled=0 subtable_template=#3
#if DEV_ASSERTIONS
/* only use this helper for cases without .else */
#define IF_HAVE_NEXT_LEVEL .if 1
#else
#define IF_HAVE_NEXT_LEVEL .ifnb \next_level_label
#endif
.subsection 0
8:	/* we get here when coming from higher and lower levels, and after switching to the next mapping */
	.if \is_mappable
		IF_HAVE_NEXT_LEVEL
			ubfx \pos, \addr, #\shift, #(\granule_shift - 3)
			tst \addr, #((1 << \shift) - 1)
			b.ne 3f	/* mapping starts in the middle of this entry's range */
			extract_address \scratch1, \desc
			sub \scratch1, \scratch1, \addr
			tst \scratch1, #((1 << \shift) - 1)
			b.ne 3f	/* delta is misaligned for this level */
		.endif
	.endif
1:	/* mapping loop */
	.if \is_mappable && \shift > 23
		/* this belongs to the is_mappable block below, it's just above the ubfx for better ILP on in-order CPUs */
#define NEXT_ENTRY_ADDR \next_level_ptr	/* when we are at level n, level n+1 is not live, so we can clobber its table address */
		mov NEXT_ENTRY_ADDR, #(1 << \shift)
	.endif

	ubfx \pos, \addr, #\shift, #(\granule_shift - 3)

	.if \is_mappable	/* if this level is not mappable, we MUST go deeper always */
		.if \shift <= 23
			add NEXT_ENTRY_ADDR, \addr, #(1 << \shift)
		.else
			add NEXT_ENTRY_ADDR, NEXT_ENTRY_ADDR, \addr
		.endif
		IF_HAVE_NEXT_LEVEL
			cmp NEXT_ENTRY_ADDR, \next_map_addr
			b.hi 3f	/* mapping ends in the middle of this entry's range */
		.endif

		/* we have decided to make an entry instead of going deeper */
	#if ASSERTIONS
		ldr \scratch1, [\table_ptr, \pos, lsl #3]
		tbnz \scratch1, 0, 9f	/* is the entry currently empty? */
		.subsection 1
		9:	bl overlapping_mapping
		.subsection 0
	#endif
		.ifnb \next_level_label
			eor \scratch1, \desc, #2	/* this is not the last level, mappings need to be block descriptors, not page descriptors */
			str \scratch1, [\table_ptr, \pos, lsl #3]
		.else
			str \desc, [\table_ptr, \pos, lsl #3]
		.endif

		mov \addr, NEXT_ENTRY_ADDR
		.if \shift > 23
			mov NEXT_ENTRY_ADDR, #(1 << \shift)
		.endif
		.if \lpa_enabled
			extract_address \scratch1, \desc, lpa_enabled=1
			and \desc, \desc, #0xffff000000000fff
			.if \shift <= 23
				add \scratch1, \scratch1, #(1 << \shift)
			.else
				add
			.endif
			insert_address \scratch1, \scratch1, lpa_enabled=1
			orr \desc, \desc, \scratch1
		.else
			.if \shift <= 23
				add \desc, \desc, #(1 << \shift)
			.else
				add \desc, \desc, NEXT_ENTRY_ADDR
			.endif
		.endif

		cmp \addr, \next_map_addr
		b.eq 7f	/* our current mapping has ended */
	#if DEV_ASSERTIONS
		b.hs 9f
		.subsection 1
		9:	bl addr_invariant_failed
		.subsection 0
	#endif

		eor \scratch1, \pos, #((1 << (\granule_shift - 3)) - 1)
		cbnz \scratch1, 1b	/* not at the end of the table, stay at this level */

		/* end of table reached, go back a level */
		.ifnb \prev_level_label
			b \prev_level_label
		.else
		#if DEV_ASSERTIONS
			bl wrapping_mapping
		#endif
		.endif
	.endif
3:	/* go deeper */
	.ifnb \next_level_label
		/* this is only reached when there is a next level (all referrers are enclosed in IF_HAVE_NEXT_LEVEL) */
		ldr \scratch1, [\table_ptr, \pos, lsl #3]
		tbz \scratch1, 0, 4f

		/* already have a subtable */
		extract_address \next_level_ptr, \scratch1, lpa_enabled=\lpa_enabled
		b \next_level_label

	4:	/* must add a new table */
		bl \alloc_table
		mov \next_level_ptr, x0
		insert_address x0, x0
		orr x0, x0, \subtable_template
		str x0, [\table_ptr, \pos, lsl 3]
		b \next_level_label
	.else
	#if DEV_ASSERTIONS
		bl misaligned_mapping
	#endif
	.endif

	.if \is_mappable	/* can't reach the end of a mapping if we can't map at this level */
	7:	/* end of current mapping (\addr == \next_map_addr) */
		bl \get_next_mapping
		b 8b
	.endif

	.ifnb \return_label
	\return_label :
		.ifnb \prev_level_label
			ubfx \pos, \addr, #\shift, #(\granule_shift - 3)
			cbnz \pos, 8b
			b \prev_level_label
		.else
		#if ASSERTIONS
			ubfx \pos, \addr, #\shift, #(\granule_shift - 3)
			cbnz \pos, 8b
			bl wrapping_mapping
		#else
			b 8b
		#endif
		.endif
	.endif
.endm

.text
mmu_multimap: .global mmu_multimap
.cfi_startproc
	mov x15, x30
.cfi_register x30, x15
	mov x10, x0
	mov x7, x1
	ldp x4, x5, [x7], #16
	ldr x6, [x7], #8
#if ASSERTIONS
	orr x0, x4, x6
	tst x0, #0xfff
	b.ne misaligned_mapping
#endif
multimap_l0:
	multimap_level addr=x4, desc=x5, next_map_addr=x6, \
		pos=x8, scratch1=x9, table_ptr=x10, next_level_ptr=x11,\
		shift=39, granule_shift=12, is_mappable=0,\
		done_label=done, next_level_label=multimap_l1, return_label=multimap_l0_return,\
		alloc_table=alloc_page_table
multimap_l1:
	multimap_level addr=x4, desc=x5, next_map_addr=x6, \
		pos=x8, scratch1=x9, table_ptr=x11, next_level_ptr=x12,\
		shift=30, granule_shift=12, is_mappable=1,\
		done_label=done, next_level_label=multimap_l2, return_label=multimap_l1_return, prev_level_label=multimap_l0_return,\
		get_next_mapping=next_mapping, alloc_table=alloc_page_table
multimap_l2:
	multimap_level addr=x4, desc=x5, next_map_addr=x6, \
		pos=x8, scratch1=x9, table_ptr=x12, next_level_ptr=x13,\
		shift=21, granule_shift=12, is_mappable=1,\
		done_label=done, next_level_label=multimap_l3, return_label=multimap_l2_return, prev_level_label=multimap_l1_return,\
		get_next_mapping=next_mapping, alloc_table=alloc_page_table
multimap_l3:
	multimap_level addr=x4, desc=x5, next_map_addr=x6, \
		pos=x8, scratch1=x9, table_ptr=x13, next_level_ptr=x14,\
		shift=12, granule_shift=12 is_mappable=1,\
		done_label=done, prev_level_label=multimap_l2_return,\
		get_next_mapping=next_mapping
done:
	mov x0, x7
	ret x15

next_mapping:
	ldr x5, [x7], #8
	tbz x5, 0, done
	ldr x6, [x7], #8

#if ASSERTIONS
	/* this test should ensure that the "go deeper" path is really unreachable for the last level, if the same is also checked for the initial mapping boundaries */
	tst x6, #0xfff
	b.ne misaligned_mapping
#endif
	ret

alloc_page_table:
	adrp x2, num_pagetables
	adrp x3, next_pagetable
	ldr x2, [x2, :lo12:num_pagetables]
	ldr w0, [x3, :lo12:next_pagetable]
	cmp x0, x2
	b.hs 9f
	add x1, x10, x0, lsl 12
	add x0, x0, #1
	add x2, x1, #0x1000
	str w0, [x3, :lo12:next_pagetable]
	mov x0, x1
	1: 	stp xzr, xzr, [x1], 16
		cmp x1, x2
		b.lo 1b
	dsb ish
	ret
9:	adr x0, 9f
	bl plat_asm_fail
9:	.asciz "ran out of pagetables\r\n"; .align 2
.cfi_endproc

.subsection 2
#if ASSERTIONS
misaligned_mapping:
	adr x0, 9f
	b plat_asm_fail
9:	.asciz "BUG: misaligned mapping boundary\r\n"; .align 2

wrapping_mapping:
	adr x0, 9f
	b plat_asm_fail
9:	.asciz "BUG: wrapping mapping\r\n"; .align 2

overlapping_mapping:
	adr x0, 9f
	b plat_asm_fail
9:	.asciz "BUG: overlapping memory mappings\r\n"; .align 2
#endif

#if DEV_ASSERTIONS
addr_invariant_failed:
	adr x0, 9f
	b plat_asm_fail
9:	.asciz "BUG: invariant addr < next_map_addr violated\r\n"; .align 2
#endif
