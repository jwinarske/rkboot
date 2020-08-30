/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

HEADER_FUNC void arch_flush_writes() {__asm__("dsb st");}
