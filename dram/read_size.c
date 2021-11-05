// SPDX-License-Identifier: CC0-1.0
#include <rk3399/dram_size.h>

#include <log.h>

#include "ddrinit.h"
#include <rk3399.h>

static u64 rank_size(u32 rows_lower, u32 rows_upper, u32 col_diff) {
	// 0 is 13, 1 is 14, … 6 is 19, 7 is 12
	u32 row_bits = ((((rows_upper & 4) | (rows_lower & 3)) + 1) & 7) + 12;
	u32 col_bits = (col_diff & 3) + 9;
	debug("%"PRIu32" col %"PRIu32" row", col_bits, row_bits);
	return UINT64_C(1) << row_bits << col_bits;
}

u64 dram_size(volatile u32 *pmugrf) {
	u32 osreg2 = pmugrf[PMUGRF_OS_REG2];
	u32 osreg3 = pmugrf[PMUGRF_OS_REG3];
	debug("OS_REG2: %08"PRIx32" OS_REG3: %08"PRIx32"\n", osreg2, osreg3);
	u64 total_size = 0;
	for_range(i, 0, 2) {
		if (~osreg2 & 1 << (28 + i)) {continue;}
		u32 block_infos = osreg2 >> (16 * i) & 0xfff;
		u32 reg3_infos = osreg3 >> (2 * i) & 0x33;
		u64 ch_size = rank_size(block_infos >> 6, reg3_infos >> 3, block_infos >> 9);
		if (block_infos & 0x800) {
			ch_size += rank_size(block_infos >> 4, reg3_infos >> 2, reg3_infos);
		}
		if (osreg2 & 1 << 30 << i) {
			// 3/4-sized dies
			ch_size = (ch_size >> 1) + (ch_size >> 2);
		}
		u32 bank_bits = 3 - (block_infos >> 8 & 1);
		u32 width_bits = 2 >> (block_infos >> 2 & 3);
		debug(" %"PRIu32" bank %"PRIu32" width\n", bank_bits, width_bits);
		total_size += ch_size << bank_bits << width_bits;
	}
	if (total_size >= 0xf8000000) {return 0xf8000000;}
	return (u32)total_size;
}
