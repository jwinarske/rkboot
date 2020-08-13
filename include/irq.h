/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>
#include <stdatomic.h>

typedef u64 irq_save_t;

HEADER_FUNC irq_save_t irq_save_mask() {
	u64 daif = 0;
#if !__STDC_HOSTED__
	__asm__ volatile("mrs %0, DAIF;msr DAIFSet, #15" : "=r"(daif));
#endif
	atomic_signal_fence(memory_order_acquire);
	return daif;
}

HEADER_FUNC void irq_restore(irq_save_t daif) {
	atomic_signal_fence(memory_order_release);
#if !__STDC_HOSTED__
	__asm__ volatile("msr DAIF, %0" : : "r"(daif));
#endif
}
