/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#define SPROC(name, alignment) .align alignment; name:
#define PROC(name, alignment) .global name;SPROC(name, alignment)
#define ENDFUNC(name) .type name, %function;.size name, .-name
