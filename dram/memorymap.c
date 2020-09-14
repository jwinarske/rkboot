/* SPDX-License-Identifier: CC0-1.0 */
#include <main.h>
#include <rk3399.h>
#include "rk3399-dmc.h"
#include "ddrinit.h"

static void set_memory_map(volatile u32 *pctl, volatile u32 *pi, const struct sdram_geometry *geo) {
	debug("bk%u col%u\n", geo->bank, geo->col);
	u32 bk_diff = 3 - geo->bank, col_diff = 12 - geo->col;
	u32 row_diff = 16 - geo->cs0_row;
	_Bool reduc = geo->width != 2;
	u32 csmask = geo->csmask;
	assert(col_diff <= 4);
	assert(bk_diff <= 1);
	debug("\nset_memory_map: pctl@%zx pi@%zx reduc%u col%u row%u bk%u cs%u\n", (u64)pctl, (u64)pi, (u32)reduc, col_diff, row_diff, bk_diff, csmask);
	u64 col_op = SET_BITS32(4, col_diff);
	u64 bk_row_op = SET_BITS32(2, bk_diff) << 16
		| SET_BITS32(3, row_diff) << 24;

	apply32v(pctl + 191, col_op);
	apply32v(pctl + 190, bk_row_op);
	apply32v(pctl + 196,
		SET_BITS32(2, csmask)
		| SET_BITS32(1, reduc) << 16
	);

	apply32v(pi + 199, col_op);
	apply32v(pi + 155, bk_row_op);

	csmask |= csmask << 2;
	apply32v(pi + 41, SET_BITS32(4, csmask) << 24);
}

void channel_post_init(volatile u32 *pctl, volatile u32 *pi, volatile u32 *msch, const struct msch_config *msch_cfg, struct sdram_geometry *geo) {
	u32 csmask = geo->csmask;
	if (!csmask) {return;}
	assert(csmask < 4);
	assert(csmask & 1);
	geo->col = 12;
	geo->bank = 3;
	geo->cs0_row = geo->cs1_row = 13;

	u32 width_bits = geo->width;
	/* set bus width and CS mask */
	assert(width_bits >= 1 && width_bits <= 2);
	printf("bw=%u ", width_bits);

	set_memory_map(pctl, pi, geo);
	msch[MSCH_DDRSIZE] = 4096/32; /* map full range to CS0 */
	msch[MSCH_DDRCONF] = width_bits == 2 ? 0x0303 : 0x0202; /* map for max row size (16K for 32-bit width, 8K for 16-bit width) */

	u32 col_bits = 12; /* max col bits */
	while (test_mirror(MIRROR_TEST_ADDR, col_bits + width_bits - 1)) {
		if (col_bits == 9) {die("rows too small (<9 bits column address)!\n");}
		col_bits -= 1;
	}
	printf("col=%u ", col_bits);
	geo->col = col_bits;

	u32 bank_shift = width_bits + col_bits;
	assert(bank_shift >= 11 && bank_shift <= 14);
	u32 ddrconf = bank_shift - 11;
	printf("ddrconf=%u ", ddrconf);

	u32 bank_bits = 3;
	if (test_mirror(MIRROR_TEST_ADDR, width_bits + 14)) {
		bank_bits = 2;
	}
	printf("bank=%u ", bank_bits);
	geo->bank = bank_bits;

	u32 row_shift = width_bits + col_bits + bank_bits;
	u32 max_row_bits = 16;
	if (row_shift + 16 > 32) {max_row_bits = 32 - row_shift;}
	geo->cs0_row = max_row_bits;

	set_memory_map(pctl, pi, geo);
	msch[MSCH_DDRCONF] = ddrconf | ddrconf << 8;
	msch[MSCH_DDRSIZE] = 4096/32; /* map full range to CS0 */

	u32 cs0_size;
	u32 cs0_row_bits = max_row_bits;
	udelay(1);
	while (test_mirror(MIRROR_TEST_ADDR, row_shift - 1 + cs0_row_bits)) {
		if (cs0_row_bits == 12) {die("too few CS0 rows (<12 bits row address)!\n");}
		printf(" row mirror");
		cs0_row_bits -= 1;
	}
	printf("cs0row=%u ", cs0_row_bits);
	geo->cs0_row = cs0_row_bits;
	assert(ASSUMPTION_Po2_ROWS);
	cs0_size = 1 << (cs0_row_bits + row_shift - 25);

	u32 cs1_size = 0;
	if (csmask & 2) {
		msch[MSCH_DDRSIZE] = cs0_size | (4096/32 - cs0_size) << 8;
		udelay(3);
		u32 cs1_row_bits = cs0_row_bits;
		u32 test_addr = MIRROR_TEST_ADDR + (cs0_size << 25);
		while (test_mirror(test_addr, row_shift - 1 + cs1_row_bits)) {
			if (cs1_row_bits == 12) {die("too few CS1 rows (<12 bits row address)!\n");}
			cs1_row_bits -= 1;
		}
		printf("cs1row=%u ", cs1_row_bits);
		geo->cs1_row = cs1_row_bits;
		assert(ASSUMPTION_Po2_ROWS);
		cs1_size = 1 << (cs1_row_bits + row_shift -  25);
	}
	u32 ddrsize = cs0_size | cs1_size << 8;
	printf("ddrsize=0x%04x\n", ddrsize);

	set_memory_map(pctl, pi, geo);
	msch[MSCH_DDRCONF] = ddrconf;
	msch[MSCH_DDRSIZE] = ddrsize;

	msch[MSCH_TIMING1] = msch_cfg->timing1;
	msch[MSCH_TIMING2] = msch_cfg->timing2;
	msch[MSCH_TIMING3] = msch_cfg->timing3;
	msch[MSCH_DEV2DEV] = msch_cfg->dev2dev;
	msch[MSCH_DDRMODE] = msch_cfg->ddrmode;
	__asm__ volatile("dsb ish");
}

void encode_dram_size(const struct sdram_geometry *geo) {
	u32 osreg2 = (!!geo[0].csmask + !!geo[1].csmask - 1) << 12 | LPDDR4 << 13;
	u32 osreg3 = 0x20000000;
	for_range(i, 0, 2) {
		if (!geo[i].csmask) {continue;}
		assert(ASSUMPTION_16BIT_CHANNEL && ASSUMPTION_Po2_ROWS);
		u32 bw = 2 >> geo[i].width, dbw = 1;
		u32 block_infos = (geo[i].csmask == 3) << 11, reg3_infos = 0;

		u32 col_diff = geo[i].col - 9;
		assert(col_diff <= 3);
		block_infos |= col_diff << 9;	/* cs0*/
		reg3_infos |= col_diff;	/* cs1 */

		u32 bank_diff = 3 - geo[i].bank;
		assert(bank_diff <= 1);
		block_infos |= bank_diff << 8;

		u32 cs0_row = geo[i].cs0_row, cs1_row = geo[i].cs1_row;
		u32 cs0_row_diff = cs0_row - 13, cs1_row_diff = cs1_row - 13;
		assert(cs0_row >= 12 && cs0_row <= 19);
		block_infos |= cs0_row_diff << 6 & 0xc0;
		reg3_infos |= cs0_row_diff << 3 & 0x20;
		assert(cs1_row >= 12 && cs1_row <= 19);
		block_infos |= cs1_row_diff << 4 & 0x30;
		reg3_infos |= cs1_row_diff << 2 & 0x10;

		block_infos |= bw << 2 | dbw;

		osreg2 |= block_infos << 16*i | 1 << (28 + i);
		osreg3 |= reg3_infos << 2*i;
	}
	/* 0x32C1F2C1 */
	printf("osreg2=%08x, osreg3=%08x\n", osreg2, osreg3);
	pmugrf[PMUGRF_OS_REG2] = osreg2;
	pmugrf[PMUGRF_OS_REG3] = osreg3;
}
