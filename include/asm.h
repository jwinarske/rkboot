/* SPDX-License-Identifier: CC0-1.0 */
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
.macro do19thru28 inst:req reg:req offset:req
	\inst x19, x20, [\reg, #(\offset)]
	\inst x21, x22, [\reg, #(\offset + 16)]
	\inst x23, x24, [\reg, #(\offset + 32)]
	\inst x25, x26, [\reg, #(\offset + 48)]
	\inst x27, x28, [\reg, #(\offset + 64)]
.endm
#endif
