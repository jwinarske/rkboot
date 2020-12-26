/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#define TEXTSECTION(name) .section name, "ax", %progbits
#define SPROC(name, alignment) .align alignment; name:.cfi_startproc
#define PROC(name, alignment) .global name;SPROC(name, alignment);
#define ENDFUNC(name) .cfi_endproc;.type name, %function;.size name, .-name
#define ENDFUNC_NESTED(name) .type name, %function;.size name, .-name

#ifdef __ASSEMBLER__
.macro create_stackframe size
	stp x29, x30, [sp, #-\size]!
	.cfi_def_cfa sp, \size
	.cfi_offset x29, -\size
	.cfi_offset x30, -\size + 8
	add x29, sp, #0
	.cfi_def_cfa x29, \size
.endm
#endif
