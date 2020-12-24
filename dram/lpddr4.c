/* SPDX-License-Identifier: CC0-1.0 */
#include <main.h>
#include "rk3399-dmc.h"

void lpddr4_get_odt_settings(struct odt_settings *odt, const struct odt_preset *preset) {
	debug("odt %zx\n", (u64)odt);
	odt->flags = ODT_TSEL_CLEAN | ODT_SET_BOOST_SLEW | ODT_SET_RX_CM_INPUT
		| ODT_RD_EN * (preset->phy.rd_odt_en == 1);
	if (preset->phy.rd_vref < 37) {
		odt->mode_dq = 7;
		odt->value_dq = ((u32)preset->phy.rd_vref * 1000 - 3300) / 521;
		odt->drive_mode = 5;
	} else {
		odt->mode_dq = 6;
		odt->value_dq = ((u32)preset->phy.rd_vref * 1000 - 15300) / 521;
		odt->drive_mode = 4;
	}
}
