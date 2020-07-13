/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include "defs.h"

struct address_range {volatile void *first, *last;};
#define ADDRESS_RANGE_INVALID {.first = (void*)1, .last = 0}

enum {
	MEM_TYPE_DEV_nGnRnE = 0,
	MEM_TYPE_DEV_nGnRE = 1,
	MEM_TYPE_DEV_nGRE = 2,
	MEM_TYPE_DEV_GRE = 3,
	MEM_TYPE_NORMAL = 4,
	MEM_TYPE_UNCACHED = 5,
	MEM_TYPE_WRITE_THROUGH = 6,
	MEM_ACCESS_RW_PRIV = 0 << 3,
	MEM_ACCESS_RW_UNPRIV = 1 << 3,
	MEM_ACCESS_RO_PRIV = 2 << 3,
	MEM_ACCESS_RO_UNPRIV = 3 << 3,
};
struct mapping {u64 first, last; u64 flags;};
extern u8 __start__[], __ro_end__[], __end__[];
#define MAPPING_BINARY_EXPLICIT(start) \
	{.first = start, .last = (u64)&__ro_end__ - 1, .flags = MEM_TYPE_NORMAL | MEM_ACCESS_RO_PRIV}, \
	{.first = (u64)&__ro_end__, .last = (u64)&__end__ - 1, .flags = MEM_TYPE_NORMAL | MEM_ACCESS_RW_PRIV}
#define MAPPING_BINARY_SRAM MAPPING_BINARY_EXPLICIT(0xff8c2000)
#define MAPPING_BINARY MAPPING_BINARY_EXPLICIT((u64)&__start__)

void invalidate_dcache_set_sctlr(u64);
void set_sctlr_flush_dcache(u64);
void flush_dcache();
void mmu_setup(const struct mapping *initial_mappings, const struct address_range *critical_ranges);
void mmu_unmap_range(u64 first, u64 last);
void mmu_map_range(u64 first, u64 last, u64 paddr, u64 flags);

static inline void UNUSED mmu_map_mmio_identity(u64 first, u64 last) {
	mmu_map_range(first, last, first, MEM_TYPE_DEV_nGnRnE);
}
