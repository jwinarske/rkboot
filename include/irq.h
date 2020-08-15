/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>
#include <stdatomic.h>

typedef u64 irq_save_t;
static const irq_save_t IRQ_SAVE_CLEAR = 0;

HEADER_FUNC irq_save_t irq_save() {
	u64 daif = 0;
#if !__STDC_HOSTED__
	asm volatile("mrs %0, DAIF" : "=r"(daif));
#endif
	return daif;
}

HEADER_FUNC void irq_mask() {
#if !__STDC_HOSTED__
	__asm__("msr DAIFSet, #15");
#endif
	atomic_signal_fence(memory_order_acquire);
}

HEADER_FUNC irq_save_t irq_save_mask() {
	irq_save_t irq = irq_save();
	irq_mask();
	return irq;
}

HEADER_FUNC void irq_restore(irq_save_t daif) {
	atomic_signal_fence(memory_order_release);
#if !__STDC_HOSTED__
	__asm__ volatile("msr DAIF, %0" : : "r"(daif));
#endif
}

HEADER_FUNC void irq_unmask() {
	atomic_signal_fence(memory_order_release);
#if !__STDC_HOSTED__
	__asm__("msr DAIFClr, #15");
#endif
}
