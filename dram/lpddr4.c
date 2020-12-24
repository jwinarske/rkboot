/* SPDX-License-Identifier: CC0-1.0 */
#include <main.h>
#include "rk3399-dmc.h"

void lpddr4_get_odt_settings(struct odt_settings *odt, const struct odt_preset *preset) {
	debug("odt %zx\n", (u64)odt);
	odt->flags = ODT_TSEL_CLEAN | ODT_SET_BOOST_SLEW | ODT_SET_RX_CM_INPUT
		| ODT_RD_EN * (preset->phy.rd_odt_en == 1);
	odt->padding = 0;
	odt->padding2 = 0;
	odt->ds[ODT_RD][ODT_N] = preset->phy.rd_odt;
	odt->ds[ODT_RD][ODT_P] = ODT_DS_HI_Z;
	odt->ds[ODT_IDLE][ODT_N] = ODT_DS_HI_Z;
	odt->ds[ODT_IDLE][ODT_P] = ODT_DS_HI_Z;
	odt->ds[ODT_WR_DQ][ODT_N] = ODT_DS_34_3;
	odt->ds[ODT_WR_DQ][ODT_P] = preset->phy.wr_dq_drv;
	odt->ds[ODT_WR_CA][ODT_N] = ODT_DS_34_3;
	odt->ds[ODT_WR_CA][ODT_P] = preset->phy.wr_ca_drv;
	odt->ds[ODT_CKCS][ODT_N] = ODT_DS_34_3;
	odt->ds[ODT_CKCS][ODT_P] = preset->phy.wr_ckcs_drv;
	odt->mode_ac = 6;
	odt->value_ac = 3;
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
