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
	MMC_BUS_WIDTH_DDR4 = 5,
	MMC_BUS_WIDTH_DDR8,
};

enum extcsd_card_type {
	MMC_CARD_TYPE_HS200_1V2 = 32,
	MMC_CARD_TYPE_HS200_1V8 = 16,
	MMC_CARD_TYPE_DDR52_1V2 = 8,
	MMC_CARD_TYPE_DDR52_1V8 = 4,
	MMC_CARD_TYPE_HS52 = 2,
	MMC_CARD_TYPE_HS26 = 1,
};

enum extcsd_hs_timing {
	MMC_TIMING_BC,
	MMC_TIMING_HS,
	MMC_TIMING_HS200,
	MMC_TIMING_HS400,
};

#define DEFINE_MMC_R1\
	X(APP_CMD, 5)\
	X(EXCEPTION_EVENT, 6)\
	X(SWITCH_ERR, 7)\
	X(READY_FOR_DATA, 8)\
	/* bits 9:12: current state, see enum mmc_state */\
	X(ERASE_RESET, 13)\
	X(CARD_ECC_DIS, 14)\
	X(WP_ERASE_SKIP, 15)\
	X(CXD_OVERWRITE, 16)\
	X(OVERRUN, 17)\
	X(UNDERRUN, 18)\
	X(ERR, 19)\
	X(CC_ERR, 20)\
	X(CARD_ECC_FAILED, 21)\
	X(ILLEGAL_CMD, 22)\
	X(COM_CRC_ERR, 23)\
	X(LOCK_ERR, 24)\
	X(LOCKED, 25)\
	X(WP_VIOLATION, 26)\
	X(ERASE_PARAM, 27)\
	X(ERASE_SEQ_ERR, 28)\
	X(BLOCK_LEN_ERR, 29)\
	X(ADDRESS_ERR, 30)\
	X(OUT_OF_RANGE, 31)

enum mmc_r1 {
#define X(name, bit) MMC_R1_##name = 1 << bit,
	DEFINE_MMC_R1
#undef X
};

enum {
#define X(name, bit) MMC_R1_POS_##name,
	DEFINE_MMC_R1
#undef X
	NUM_MMC_R1_POS
};

#define DEFINE_MMC_STATE\
	X(IDLE)\
	X(READY)\
	X(IDENT)\
	X(STBY)\
	X(TRAN)\
	X(DATA)\
	X(RCV)\
	X(PRG)\
	X(DIS)\

enum mmc_state {
#define X(name) MMC_STATE_##name,
	DEFINE_MMC_STATE
#undef X
	NUM_MMC_STATE
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
