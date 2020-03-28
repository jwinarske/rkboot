#include <main.h>
#include <uart.h>

static u64 __attribute__((aligned(4096))) pagetables[4][512];
static u32 current_pagetable = 4;

enum {
	TCR_NONSHARED = 0,
	TCR_INNER_SHAREABLE = 3 << 12,
	TCR_FULLY_INNER_CACHEABLE = 1 << 8,
	TCR_FULLY_OUTER_CACHEABLE = 1 << 10,
	TCR_4K_GRANULE = 0,
	TCR_TBI = 1 << 20,
	TCR_RES1 = (u64)1 << 31 | 1 << 23
};
#define TCR_REGION0(c) (c)
#define TCR_PS(x) ((x) << 16)
#define TCR_TxSZ(x) (x)

#define PGTAB_SUBTABLE (3)
#define PGTAB_BLOCK(attridx) (1 | 1 << 10 | (attridx) << 2)
#define PGTAB_PAGE(attridx) (3 | 1 << 10 | (attridx) << 2)
#define PGTAB_CONTIGUOUS ((u64)1 << 52)
#define PGTAB_OUTER_SHAREABLE ((u64)2 << 8)
#define PGTAB_NSTABLE ((u64)1 << 63)
#define PGTAB_NS (1 << 5)

static u64 UNUSED *get_page_table() {
	assert(current_pagetable < 4);
	return &pagetables[current_pagetable++][0];
}

static void UNUSED dump_page_tables() {
	for_range(table, 0, current_pagetable) {
		for (u32 i = 0; i < ARRAY_SIZE(pagetables[table]); i += 4) {
			printf("%3u: %016zx %016zx %016zx %016zx\n", i, pagetables[table][i], pagetables[table][i + 1], pagetables[table][i + 2], pagetables[table][i + 3]);
		}
	}
}

#define MASK64(n) (((u64)1 << (n)) - 1)

static u64 UNUSED map_address(u64 addr) {
	u64 pte_l0 = pagetables[0][addr >> 39 & 0x1ff];
	assert((pte_l0 & 3) == 3);
	u64 pt_l1 = (pte_l0 >> 12 & MASK64(36)) - ((u64)&pagetables >> 12);
	assert(pt_l1 < ARRAY_SIZE(pagetables));
	u64 pte_l1 = pagetables[pt_l1][addr >> 30 & 0x1ff];
	if ((pte_l1 & 3) == 1) {
		return (pte_l1 & MASK64(18) << 30) | (addr & MASK64(30));
	}
	assert((pte_l1 & 3) == 3);
	u64 pt_l2 = (pte_l1 >> 12 & MASK64(36)) - ((u64)&pagetables >> 12);
	assert(pt_l2 < ARRAY_SIZE(pagetables));
	u64 pte_l2 = pagetables[pt_l2][addr >> 21 & 0x1ff];
	if ((pte_l2 & 3) == 1) {
		return (pte_l2 & MASK64(27) << 21) | (addr & MASK64(21));
	}
	assert((pte_l2 & 3) == 3);
	u64 pt_l3 = (pte_l2 >> 12 & MASK64(36)) - ((u64)&pagetables >> 12);
	assert(pt_l3 < ARRAY_SIZE(pagetables));
	u64 pte_l3 = pagetables[pt_l3][addr >> 12 & 0x1ff];
	assert((pte_l3 & 3) == 3);
	return (pte_l3 & MASK64(36) << 12) | (addr & MASK64(12));
}

void setup_mmu() {
	for_range(i, 0, 512) {pagetables[0][i] = (u64)&pagetables[1] | PGTAB_SUBTABLE;}
	for_range(i, 0, 512) {
		if (i % 4 != 3) {
			pagetables[1][i] = (i % 4) << 30 | PGTAB_BLOCK(0) | PGTAB_OUTER_SHAREABLE;
		} else {
			pagetables[1][i] = (u64)&pagetables[2] | PGTAB_SUBTABLE;
		}
	}
	for (u64 i = 0xc0000000; i < 0xff800000; i += 1 << 21) {
		pagetables[2][i >> 21 & 0x1ff] = i | PGTAB_BLOCK(0);
	}
	pagetables[2][0xff800000 >> 21 & 0x1ff] = (u64)&pagetables[3] | PGTAB_SUBTABLE;
	for (u64 i = 0xffa00000; i < ((u64)1 << 32); i += 1 << 21) {
		pagetables[2][i >> 21 & 0x1ff] = i | PGTAB_BLOCK(0);
	}
	for (u64 i = 0xff800000; i < 0xffa00000; i += 1 << 12) {
		pagetables[3][i >> 12 & 0x1ff] = i | (i < 0xff8c0000 || i >= 0xff8f0000 ?
			PGTAB_PAGE(0) : PGTAB_PAGE(4));
	}
#ifdef DEBUG_MSG
	dump_page_tables();
#endif
	for (u64 i = 0xff8c0000; i < 0xff8f0000; i += 0x1000) {
		u64 mapped = map_address(i);
		debug("%08zx maps to %08zx\n", i, mapped);
		assert(mapped == i);
	}
	assert(map_address((u64)&uart) == (u64)&uart);
	__asm__ volatile("msr mair_el3, %0" : : "r"((u64)0xff0c080400));
#ifdef DEBUG_MSG
	u64 mair;
	__asm__ volatile("mrs %0, mair_el3" : "=r"(mair));
	debug("MAIR is %zx\n", mair);
#endif
	u64 tcr = TCR_RES1 | TCR_REGION0(TCR_FULLY_INNER_CACHEABLE | TCR_FULLY_OUTER_CACHEABLE | TCR_INNER_SHAREABLE | TCR_4K_GRANULE | TCR_TxSZ(16)) | TCR_PS(5) | TCR_TBI;
	u64 ttbr0 = (u64)&pagetables[0];
	printf("writing 0x%016zx to TCR_EL3, 0x%016zx to TTBR0_EL3\n", tcr, ttbr0);
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
