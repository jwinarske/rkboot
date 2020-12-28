/* SPDX-License-Identifier: CC0-1.0 */
#include <log.h>
#include <die.h>
#include <arch.h>
#include <aarch64.h>
#include <mmu.h>
#include <stdio.h>
#include <assert.h>
#include <inttypes.h>

#ifdef DEBUG_MSG
#define PRINT_MAPPINGS 1
#endif

u32 next_pagetable = 1;

#define PGTAB_SUBTABLE (3)
#define PGTAB_BLOCK(attridx) (1 | 1 << 10 | (attridx) << 2)
#define PGTAB_PAGE(attridx) (3 | 1 << 10 | (attridx) << 2)
#define PGTAB_CONTIGUOUS ((u64)1 << 52)
#define PGTAB_OUTER_SHAREABLE ((u64)2 << 8)
#define PGTAB_INNER_SHAREABLE ((u64)3 << 8)
#define PGTAB_NSTABLE ((u64)1 << 63)
#define PGTAB_NS (1 << 5)

static u64 *alloc_page_table() {
	assert_msg(next_pagetable < num_pagetables, "ERROR: ran out of pagetables");
	u64 *res = &pagetables[next_pagetable++][0];
	debug("alloc_page_table: %"PRIx64"\n", (u64)res);
	return res;
}

static void UNUSED dump_page_tables() {
	for_range(table, 0, next_pagetable) {
		for (u32 i = 0; i < ARRAY_SIZE(pagetables[table]); i += 4) {
			printf("%3u: %016zx %016zx %016zx %016zx\n", i, pagetables[table][i], pagetables[table][i + 1], pagetables[table][i + 2], pagetables[table][i + 3]);
		}
	}
}

#define MASK64(n) (((u64)1 << (n)) - 1)

static u64 UNUSED map_address(u64 addr) {
	u64 pte_l0 = pagetables[0][addr >> 39 & 0x1ff];
	assert((pte_l0 & 3) == 3);
	u64 pt_l1 = (pte_l0 >> 12 & MASK64(36)) - ((u64)pagetables >> 12);
	assert(pt_l1 < num_pagetables);
	u64 pte_l1 = pagetables[pt_l1][addr >> 30 & 0x1ff];
	if ((pte_l1 & 3) == 1) {
		return (pte_l1 & MASK64(18) << 30) | (addr & MASK64(30));
	}
	assert((pte_l1 & 3) == 3);
	u64 pt_l2 = (pte_l1 >> 12 & MASK64(36)) - ((u64)pagetables >> 12);
	assert(pt_l2 < num_pagetables);
	u64 pte_l2 = pagetables[pt_l2][addr >> 21 & 0x1ff];
	if ((pte_l2 & 3) == 1) {
		return (pte_l2 & MASK64(27) << 21) | (addr & MASK64(21));
	}
	assert((pte_l2 & 3) == 3);
	u64 pt_l3 = (pte_l2 >> 12 & MASK64(36)) - ((u64)pagetables >> 12);
	assert(pt_l3 < num_pagetables);
	u64 pte_l3 = pagetables[pt_l3][addr >> 12 & 0x1ff];
	assert_msg((pte_l3 & 3) == 3, "0x%"PRIx64" not mapped correctly (L3 translation)\n", addr);
	return (pte_l3 & MASK64(36) << 12) | (addr & MASK64(12));
}

static const struct pte_lvl {
	u32 lvl : 2;
	u32 shift : 6;
	u32 contiguous : 4;
	u32 last_level : 1;
	u32 mapping_allowed : 1;
} pte_lvls[] = {
	{.lvl = 0, .shift = 39, .contiguous = 0, .last_level = 0, .mapping_allowed = 0},
	{.lvl = 1, .shift = 30, .contiguous = 4, .last_level = 0, .mapping_allowed = 1},
	{.lvl = 2, .shift = 21, .contiguous = 4, .last_level = 0, .mapping_allowed = 1},
	{.lvl = 3, .shift = 12, .contiguous = 4, .last_level = 1, .mapping_allowed = 1},
};
#define GRANULE_SHIFT 12
#define MAPPING_LEVEL_SHIFT (GRANULE_SHIFT - 3)
#define NUM_MAPPING_LEVELS 4
/* the number of the highest (lowest-number) level that has mappings */
#define MIN_MAPPING_LEVEL 1

static inline u64 extr_addr(u64 entry) {
	return entry & MASK64(48 - GRANULE_SHIFT) << GRANULE_SHIFT;
}

static inline u64 *entry2subtable(u64 entry) {
	return (u64 *)extr_addr(entry);
}

struct mmu_multimap {
	u64 addr;
	u64 desc;
};
const struct mmu_multimap *multimap(u64 *table, const struct mmu_multimap *map);

static const struct mmu_multimap UNUSED *multimap_reference(u64 *table, const struct mmu_multimap *map) {
	unsigned level = 0;
	u64 *table_ptrs[4] = {table};
	u64 addr = map->addr;
	u64 desc = map->desc;
	map += 1;
	while (1) {
		debug("multimap: %"PRIx64" %"PRIx64" %"PRIx64" %u %"PRIx64": ", addr, desc, map->addr, level, (u64)table_ptrs[level]);
		assert(addr < map->addr);
		unsigned shift = pte_lvls[level].shift;
		u64 next_addr = addr + (UINT64_C(1) << shift);
		_Bool deeper = level < MIN_MAPPING_LEVEL;
		/* mapping starts at the middle of this entry's range */
		deeper |= (addr & MASK64(shift)) != 0;
		/* the delta of vaddr and paddr is not aligned for this level */
		deeper |= (~desc & 2) && ((addr - extr_addr(desc)) & MASK64(shift));
		/* mapping ends in the middle of this entry's range */
		deeper |= next_addr > map->addr;
		unsigned pos = addr >> shift & MASK64(MAPPING_LEVEL_SHIFT);
		debug("pos%x ", pos);
		u64 *entry = table_ptrs[level] + pos;
		if (deeper) {
			debugs("going deeper\n");
			assert(level < 3);
			u64 *next_table;
			if (~*entry & 1) {
				next_table = alloc_page_table();
				for_range(i, 0, 1 << MAPPING_LEVEL_SHIFT) {next_table[i] = 0;}
				arch_flush_writes();
				*entry = PGTAB_SUBTABLE | (u64)next_table;
			} else {
				assert(*entry & 2);
				next_table = entry2subtable(*entry);
			}
			table_ptrs[++level] = next_table;
		} else {
			debugs("writing entry\n");
			assert(~*entry & 1);
			*entry = level == 3 ? desc : desc ^ 2;
			desc += UINT64_C(1) << (desc & 2 ? shift : GRANULE_SHIFT);
			if (next_addr == map->addr) {
				addr = map->addr;
				desc = map->desc;
				map += 1;
				if (!desc) {return map;}
			}
			while ((next_addr >> pte_lvls[level].shift & MASK64(MAPPING_LEVEL_SHIFT)) == 0) {
				assert(level > 0);
				level -= 1;
			}
			addr = next_addr;
		}
	}
}

static void map_range(u64 *pt, u64 first, u64 last, u64 paddr, u64 flags) {
	assert(last > first);
	debug("mapping 0x%"PRIx64"–0x%"PRIx64" to paddr 0x%"PRIx64" as %"PRIx64"\n", first, last, paddr, flags);
	u64 attridx = flags & 7;
	struct mmu_multimap map[2] = {
		{first, paddr | PGTAB_PAGE(attridx) | (flags >> 3 & 3) << 8 | PGTAB_OUTER_SHAREABLE | (flags & MEM_NON_SECURE)},
		{last + 1, 0}
	};
	multimap(pt, map);
}

void mmu_map_range(u64 first, u64 last, u64 paddr, u64 flags) {
	map_range(pagetables[0], first, last, paddr, flags);
#ifdef SPEW_MSG
	dump_page_tables();
#endif
}

static u64 unmap_one(u64 *pt, u64 first, u64 last) {
	debug("unmap_one 0x%016"PRIx64"–0x%016"PRIx64"\n", first, last);
	for_array(lvl, pte_lvls) {
		u32 shift = pte_lvls[lvl].shift;
		u64 mask = MASK64(shift);
		_Bool end_aligned = (last & mask) == mask, not_also_last = first >> shift < last >> shift;
		u32 first_entry = first >> shift & MASK64(MAPPING_LEVEL_SHIFT);
		if (pte_lvls[lvl].mapping_allowed && (first & mask) == 0 && (end_aligned || not_also_last)) {
			if (!end_aligned) {
				/* round down to the nearest block boundary */
				last = (last - ((u64)1 << shift)) | mask;
			}
			u32 last_entry = last >> shift & MASK64(MAPPING_LEVEL_SHIFT);
			u64 template = pte_lvls[lvl].last_level ? 3 : 1;
			for_range(i, first_entry, last_entry + 1) {
				assert((pt[i] & 3) == template);
				pt[i] = 0;
			}
			__asm__ volatile("dsb sy");
			for_range(i, first_entry, last_entry + 1) {
				assert(!(pt[i] & 1));
				u64 addr = first + ((i - first_entry) << shift);
				__asm__ volatile("tlbi vae3, %0" : : "r"(addr));
			}
			return last;
		}
		assert_msg(!pte_lvls[lvl].last_level, "mapping 0x%016"PRIx64"–0x%016"PRIx64" is more fine-grained than the granule\n", first, last);
		u64 entry = pt[first_entry];
		assert((entry & 3) == 3);
		pt = entry2subtable(entry);
		/* clip off to the end of the next page table range */
		if (first >> shift < last >> shift) {last = first | mask;}
	}
	assert(UNREACHABLE);
}

void mmu_unmap_range(u64 first, u64 last) {
	assert(last > first);
	while ((first = unmap_one(pagetables[0], first, last)) < last) {first += 1;}
}

static UNUSED const struct mmu_multimap bin_multimap[] = {
	{(u64)&__start__, (u64)&__start__ + (PGTAB_PAGE(MEM_TYPE_NORMAL) | MEM_ACCESS_RO_PRIV)},
	{(u64)&__ro_end__, (u64)&__ro_end__ + (PGTAB_PAGE(MEM_TYPE_NORMAL) | MEM_ACCESS_RW_PRIV)},
	{(u64)&__end__, 0}
};

void mmu_setup(const struct mapping *initial_mappings, const struct address_range *critical_ranges) {
	for_range(i, 0, 512) {pagetables[0][i] = 0;}
	multimap(pagetables[0], bin_multimap);
	for (const struct mapping *map = initial_mappings; map->last; ++map) {
		map_range(pagetables[0], map->first, map->last, map->first, map->flags);
	}
#ifdef SPEW_MSG
	dump_page_tables();
#endif
#ifndef NDEBUG
	u64 first, last;
	for (size_t pos = 0; (first = (u64)critical_ranges[pos].first) <= (last = (u64)critical_ranges[pos].last); ++pos) {
		for (u64 addr = first & ~(u64)0xfff; addr <= last; addr += 0x1000) {
			u64 mapped = map_address(addr);
			debug("%08zx maps to %08zx\n", addr, mapped);
			assert(mapped == addr);
		}
	}
#endif
	__asm__ volatile("msr mair_el3, %0" : : "r"(MMU_MAIR_VAL));
#ifdef DEBUG_MSG
	u64 mair;
	__asm__ volatile("mrs %0, mair_el3" : "=r"(mair));
	debug("MAIR is %zx\n", mair);
#endif
	u64 tcr = MMU_TCR_VAL;
	u64 ttbr0 = (u64)&pagetables[0];
	debug("writing 0x%016zx to TCR_EL3, 0x%016zx to TTBR0_EL3\n", tcr, ttbr0);
	__asm__ volatile("msr tcr_el3, %0" : : "r"(tcr));
	__asm__ volatile("dsb ish;isb;msr ttbr0_el3, %0" : : "r"(ttbr0) : "memory", "cc");
	__asm__ volatile("msr sctlr_el3, %0" : : "r"((u64)SCTLR_EL3_RES1 | SCTLR_I | SCTLR_SA));
#ifdef DEBUG_MSG
	__asm__ volatile("mrs %0, ttbr0_el3;mrs %1, tcr_el3": "=r"(ttbr0), "=r"(tcr));
	printf("TTBR0 is %zx, TCR=%zx\n", ttbr0, tcr);
#endif
	u64 clidr;
	__asm__ volatile("mrs %0, clidr_el1" : "=r"(clidr));
	debug("CLIDR_EL1=%016zx\n", clidr);
	debugs("starting MMU\n");
	invalidate_dcache_set_sctlr((u64)SCTLR_EL3_RES1 | SCTLR_I | SCTLR_SA | SCTLR_M | SCTLR_C);
	puts("welcome to MMU land\n");
}
