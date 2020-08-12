/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#define SPROC(name, alignment) .align alignment; name:.cfi_startproc
#define PROC(name, alignment) .global name;SPROC(name, alignment);
#define ENDFUNC(name) .cfi_endproc;.type name, %function;.size name, .-name
#define ENDFUNC_NESTED(name) .type name, %function;.size name, .-name
