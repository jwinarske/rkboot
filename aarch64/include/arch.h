/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>
#include <stdatomic.h>

HEADER_FUNC void arch_flush_writes() {
	asm volatile("dsb st" : : : "memory");
	atomic_signal_fence(memory_order_seq_cst);
}
HEADER_FUNC void arch_relax_cpu() {
	asm volatile("yield" : : : "memory");
	atomic_signal_fence(memory_order_seq_cst);
}
