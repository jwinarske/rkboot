/* SPDX-License-Identifier: CC0-1.0 */
#include <defs.h>
#include <log.h>
#include <inttypes.h>

void sd_dump_cid(u32 cid0, u32 cid1, u32 cid2, u32 cid3) {
	u32 cid[4] = {cid0, cid1, cid2, cid3};
	for_range(i, 0, 4) {
		info("CID%"PRIu32": %08"PRIx32"\n", i, cid[i]);
	}
	info("card month: %04"PRIu32"-%02"PRIu32", serial 0x%08"PRIx32"\n", (cid[0] >> 12 & 0xfff) + 2000, cid[0] >> 8 & 0xf, cid[0] >> 24 | cid[1] << 8);
	info("mfg 0x%02"PRIx32" oem 0x%04"PRIx32" hwrev %"PRIu32" fwrev %"PRIu32"\n", cid[3] >> 24, cid[3] >> 8 & 0xffff, cid[1] >> 28 & 0xf, cid[1] >> 24 & 0xf);
	char prod_name[6] = {cid[3] & 0xff, cid[2] >> 24 & 0xff, cid[2] >> 16 & 0xff, cid[2] >> 8 & 0xff, cid[2] & 0xff, 0};
	info("product name: %s\n", prod_name);
}
