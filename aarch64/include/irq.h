/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>
#include <stdatomic.h>
#include <assert.h>

typedef u64 irq_save_t;
static const irq_save_t IRQ_SAVE_CLEAR = 0;
/* irq-masking spinning lock. bit 0: locked, bit 1: contended */
typedef _Atomic(u8) irq_lock_t;
static const u8 IRQ_LOCK_INIT = 0;

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

HEADER_FUNC irq_save_t irq_lock(irq_lock_t *lock) {
	irq_save_t irq = irq_save_mask();
	u8 val;
	while ((val = atomic_fetch_or_explicit(lock, 1, memory_order_acquire)) != 0) {
		if (val == 1) {
			if (!atomic_compare_exchange_strong_explicit(lock, &val, 3, memory_order_relaxed, memory_order_relaxed)) {
				if (val == 0) {continue;}	/* unlocked in the meantime, try to reacquire */
				/* otherwise: someone already set the contention flag, just continue */
			}
		}
		irq_restore(irq);	/* we're waiting on the lock, let IRQs come in */
		asm("wfe");
		irq_mask();
	}
}

HEADER_FUNC void irq_unlock(irq_lock_t *lock, irq_save_t irq) {
	u8 val = atomic_exchange_explicit(lock, 0, memory_order_release);
	if (val != 1) {
		assert(val == 3);
		asm("sev");	/* wake waiters, but only if a contended lock was released */
	}
	irq_restore(irq);
}
