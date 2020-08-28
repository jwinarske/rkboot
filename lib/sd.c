/* SPDX-License-Identifier: CC0-1.0 */
#include <sd.h>
#include <inttypes.h>

#include <log.h>

void sd_dump_cid(const struct sd_cardinfo *card) {
	const u32 *cid = card->cid;
	info("CID: %08"PRIx32" %08"PRIx32" %08"PRIx32" %08"PRIx32"\n",
		cid[0], cid[1], cid[2], cid[3]
	);
	info("card month: %04"PRIu32"-%02"PRIu32", serial 0x%08"PRIx32"\n", (cid[0] >> 12 & 0xfff) + 2000, cid[0] >> 8 & 0xf, cid[0] >> 24 | cid[1] << 8);
	info("mfg 0x%02"PRIx32" oem 0x%04"PRIx32" hwrev %"PRIu32" fwrev %"PRIu32"\n", cid[3] >> 24, cid[3] >> 8 & 0xffff, cid[1] >> 28 & 0xf, cid[1] >> 24 & 0xf);
	char prod_name[6] = {cid[3] & 0xff, cid[2] >> 24 & 0xff, cid[2] >> 16 & 0xff, cid[2] >> 8 & 0xff, cid[2] & 0xff, 0};
	info("product name: %s\n", prod_name);
}

void sd_dump_csd(const struct sd_cardinfo *card) {
	const u32 *csd = card->csd;
	info("CSD: %08"PRIx32" %08"PRIx32" %08"PRIx32" %08"PRIx32"\n",
		csd[0], csd[1], csd[2], csd[3]
	);
}
