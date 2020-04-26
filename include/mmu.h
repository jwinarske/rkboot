#pragma once
#include "defs.h"

extern const struct address_range {volatile void *first, *last;} critical_ranges[];
#define ADDRESS_RANGE_INVALID {.first = (void*)1, .last = 0}
extern const struct mapping {u64 first, last; u8 type;} initial_mappings[];
void invalidate_dcache_set_sctlr(u64);
void set_sctlr_flush_dcache(u64);
void flush_dcache();
void setup_mmu();
