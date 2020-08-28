/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

enum {
	SD_OCR_HIGH_CAPACITY = 1 << 30,
	SD_OCR_XPC = 1 << 28,
	SD_OCR_S18R = 1 << 24,
};
enum {
	SD_RESP_BUSY = 1 << 31,
};

struct sd_cardinfo {
	u32 rca, rocr;
	u32 cid[4], csd[4];
	u32 switch_data[16], ssr[16];
};

void sd_dump_cid(const struct sd_cardinfo *card);
void sd_dump_csd(const struct sd_cardinfo *card);
