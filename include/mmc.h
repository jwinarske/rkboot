/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

#define MMC_SWITCH_SET_BYTE(idx, val) (0x03000000 | val << 8 | idx << 16)

enum {
	EXTCSD_DATA_SECTOR_SIZE = 61,
	EXTCSD_BUS_WIDTH = 183,
	EXTCSD_REV = 192,
	EXTCSD_STRUCTURE = 194,
	EXTCSD_SEC_CNT = 212,
};

enum {
	MMC_BUS_WIDTH_8 = 2,
};

struct mmc_cardinfo {
	u32 rocr;
	u32 cxd[8];
	u8 ext_csd[512];
};

HEADER_FUNC _Bool mmc_cardinfo_understood(const struct mmc_cardinfo *card) {
	if (card->cxd[3] >> 30 != 2) {return 0;}
	if (card->ext_csd[EXTCSD_STRUCTURE] != 2) {return 0;}
	return 1;
}

HEADER_FUNC u32 mmc_sector_count(const struct mmc_cardinfo *card) {
	return card->ext_csd[EXTCSD_SEC_CNT]
		| (u32)card->ext_csd[EXTCSD_SEC_CNT + 1] << 8
		| (u32)card->ext_csd[EXTCSD_SEC_CNT + 2] << 16
		| (u32)card->ext_csd[EXTCSD_SEC_CNT + 3] << 24;
}
