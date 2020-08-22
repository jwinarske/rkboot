/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

#define MMC_SWITCH_SET_BYTE(idx, val) (0x03000000 | val << 8 | idx << 16)

enum {
	EXTCSD_DATA_SECTOR_SIZE = 61,
	EXTCSD_BUS_WIDTH = 183,
	EXTCSD_HS_TIMING = 185,
	EXTCSD_REV = 192,
	EXTCSD_STRUCTURE = 194,
	EXTCSD_CARD_TYPE = 196,
	EXTCSD_SEC_CNT = 212,
	EXTCSD_GENERIC_CMD6_TIME = 248,
};

enum extcsd_bus_width {
	MMC_BUS_WIDTH_1,
	MMC_BUS_WIDTH_4,
	MMC_BUS_WIDTH_8,
};

enum extcsd_card_type {
	MMC_CARD_TYPE_HS52 = 2,
	MMC_CARD_TYPE_HS26 = 1,
};

enum extcsd_hs_timing {
	MMC_TIMING_BC,
	MMC_TIMING_HS,
	MMC_TIMING_HS200,
	MMC_TIMING_HS400,
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
