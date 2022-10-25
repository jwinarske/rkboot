// SPDX-License-Identifier: CC0-1.0
#pragma once
#define TEXTSECTION(name) .section name, "ax", %progbits
#define SPROC(name, alignment) .align alignment; name:.cfi_startproc
#define PROC(name, alignment) .global name;SPROC(name, alignment);
#define ENDFUNC(name) .cfi_endproc;.type name, %function;.size name, .-name
#define ENDFUNC_NESTED(name) .type name, %function;.size name, .-name

#ifdef __ASSEMBLER__
#define UINT64_C(val) val

.macro create_stackframe size
	stp x29, x30, [sp, #-\size]!
	.cfi_def_cfa sp, \size
	.cfi_offset x29, -\size
	.cfi_offset x30, -\size + 8
	add x29, sp, #0
	.cfi_def_cfa x29, \size
.endm
.macro do2thru17 inst:req reg:req offset:req
	\inst x2, x3, [\reg, #(\offset)]
	\inst x4, x5, [\reg, #(\offset + 16)]
	\inst x6, x7, [\reg, #(\offset + 32)]
	\inst x8, x9, [\reg, #(\offset + 48)]
	\inst x10, x11, [\reg, #(\offset + 64)]
	\inst x12, x13, [\reg, #(\offset + 80)]
	\inst x14, x15, [\reg, #(\offset + 96)]
	\inst x16, x17, [\reg, #(\offset + 112)]
.endm
.macro do19thru28 inst:req reg:req offset:req
	\inst x19, x20, [\reg, #(\offset)]
	\inst x21, x22, [\reg, #(\offset + 16)]
	\inst x23, x24, [\reg, #(\offset + 32)]
	\inst x25, x26, [\reg, #(\offset + 48)]
	\inst x27, x28, [\reg, #(\offset + 64)]
.endm

// these exist because the CPP can't concatenate the contents of
// macros (in this case the EL macro passed from the command
// line) to identifiers like TPIDR_EL##EL or something :(
.macro mrs_per_el dest:req sysreg:req el:req
	mrs \dest, \sysreg\()_EL\el
.endm
.macro msr_per_el sysreg:req el:req dest:req
	msr \sysreg\()_EL\el, \dest
.endm
#endif
