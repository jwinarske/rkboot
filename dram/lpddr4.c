/* SPDX-License-Identifier: CC0-1.0 */
#include <main.h>
#include "rk3399-dmc.h"

void lpddr4_get_odt_settings(struct odt_settings *odt, const struct odt_preset *preset) {
	debug("odt %zx\n", (u64)odt);
	odt->flags = ODT_TSEL_CLEAN | ODT_SET_BOOST_SLEW | ODT_SET_RX_CM_INPUT
		| ODT_RD_EN * (preset->phy.rd_odt_en == 1);
	odt->soc_odt = preset->phy.soc_odt;
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

void lpddr4_set_odt(volatile u32 *pctl, volatile u32 *pi, u32 freqset, const struct odt_preset *preset) {
	mr_adjust(pctl, pi, &dq_odt_adj, freqset, preset->dram.dq_odt);
	mr_adjust(pctl, pi, &ca_odt_adj, freqset, preset->dram.ca_odt);
	mr_adjust(pctl, pi, &mr3_adj, freqset, preset->dram.pdds << 3 | 1);
	mr_adjust(pctl, pi, &mr12_adj, freqset, preset->dram.ca_vref);
	mr_adjust(pctl, pi, &mr14_adj, freqset, preset->dram.dq_vref);
}

void lpddr4_modify_config(u32 *pctl, u32 *pi, struct phy_cfg *phy, const struct odt_settings *odt) {
	lpddr4_set_odt(pctl, pi, 2, &odt_50mhz);

	set_drive_strength(pctl, (u32*)phy, &cfg_layout, odt);
	/*set_phy_io((u32 *)phy, &cfg_layout, &odt);*/
	static const char *const arr[] = {"rd", "idle", "dq", "ca", "ckcs"};
	for_array(i, arr) {debug("%s n=%x p=%x\n", arr[i], (u32)odt->ds[i][ODT_N], (u32)odt->ds[i][ODT_P]);}

	/* read 2-cycle preamble */
	pctl[200] |= 3 << 24;
	phy->dslice[7] |= 3 << 24;
	/* boot frequency 2-cycle preamble */
	phy->dslice[2] |= 3 << 16;
	
	pi[45] |= 3 << 8;
	pi[58] |= 1;

	/* disable power reduction to use bypass mode */
	phy->dslice[10] |= 1 << 16;
	pi[45] |= 1 << 24;
	pi[61] |= 1 << 24;
	pi[76] |= 1 << 24;
	pi[77] |= 1;
}
