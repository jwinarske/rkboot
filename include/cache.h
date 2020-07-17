/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>
#include <stdatomic.h>

enum {MIN_CACHELINE_SIZE = 32, MAX_CACHELINE_SIZE = 128};

HEADER_FUNC void cache_fence() {
	atomic_thread_fence(memory_order_seq_cst);	/* as per ARMv8 ARM, cache maintenance is only ordered by barriers that order both loads and stores */
}

HEADER_FUNC void flush_range(void *ptr, size_t size) {
	cache_fence();
	void *end = ptr + size;
	while (ptr < end) {
		__asm__ volatile("dc civac, %0" : : "r"(ptr) : "memory");
		ptr += MIN_CACHELINE_SIZE;
	}
	cache_fence();
}

HEADER_FUNC void invalidate_range(void *ptr, size_t size) {
	cache_fence();
	void *end = ptr + size;
	while (ptr < end) {
		__asm__ volatile("dc ivac, %0" : : "r"(ptr) : "memory");
		ptr += MIN_CACHELINE_SIZE;
	}
	cache_fence();
}
