/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

struct exc_state_save {
	u64 fp, lr;
	u64 pad1, pad2;
	u64 locals[19];
};

#define X(a, b) extern void (*volatile a##_handler_##b)(struct exc_state_save *);
#define Y(a) X(sync_exc, a) X(irq, a) X(fiq, a) X(serror, a)
Y(sp0) Y(spx) Y(aarch64) Y(aarch32)
#undef X
#undef Y
