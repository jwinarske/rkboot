#pragma once
#include "defs.h"

struct address_range {volatile void *first, *last;};
#define ADDRESS_RANGE_INVALID {.first = (void*)1, .last = 0}
struct mapping {u64 first, last; u8 type;};
void invalidate_dcache_set_sctlr(u64);
void set_sctlr_flush_dcache(u64);
void flush_dcache();
void mmu_setup(const struct mapping *initial_mappings, const struct address_range *critical_ranges);
void mmu_unmap_range(u64 first, u64 last);
void mmu_map_range(u64 first, u64 last, u8 attridx);
