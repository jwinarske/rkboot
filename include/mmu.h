/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <aarch64.h>

#define MMU_MAIR_VAL UINT64_C(0x3344ff0c080400)
#define MMU_TCR_VAL (TCR_EL3_RES1 | TCR_REGION0(TCR_INNER_CACHED | TCR_OUTER_CACHED | TCR_OUTER_SHARED | TCR_4K_GRANULE | TCR_TxSZ(16)) | TCR_PS(0))

#ifndef __ASSEMBLER__

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
	MEM_NON_SECURE = 32,
};

#define PGTAB_SUBTABLE (3)
#define PGTAB_BLOCK(attridx) (1 | 1 << 10 | (attridx) << 2)
#define PGTAB_PAGE(attridx) (3 | 1 << 10 | (attridx) << 2)
#define PGTAB_CONTIGUOUS ((u64)1 << 52)
#define PGTAB_OUTER_SHAREABLE ((u64)2 << 8)
#define PGTAB_INNER_SHAREABLE ((u64)3 << 8)
#define PGTAB_NSTABLE ((u64)1 << 63)
#define PGTAB_NS (1 << 5)

struct mmu_multimap {
	u64 addr;
	u64 desc;
};
extern u8 __start__[], __ro_end__[], __end__[];

void flush_dcache();
void mmu_unmap_range(u64 first, u64 last);
void mmu_map_range(u64 first, u64 last, u64 paddr, u64 flags);

HEADER_FUNC void mmu_flush() {__asm__("dsb ishst");}

extern u64 (*const pagetables)[512];
extern const size_t num_pagetables;

static inline void UNUSED mmu_map_mmio_identity(u64 first, u64 last) {
	mmu_map_range(first, last, first, MEM_TYPE_DEV_nGnRnE);
}
#endif
