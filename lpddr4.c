#include <main.h>
#include "rk3399-dmc.h"

struct mr_adjustments {
	struct mr_adj_regset {
		u8 pctl_regs[2], pctl_shift;
		u8 pi_regs[2], pi_shifts[2];
		u8 pad;
	} loc[3];
	u16 pctl_mask, pi_mask;
	char name[8];
};

static const struct mr_adjustments dq_odt_adj = {{
	{{139, 153}, 24, {132, 139}, {0, 16}, 0},
	{{140, 154}, 0, {129, 137}, {16, 0}, 0},
	{{140, 154}, 8, {127, 134}, {0, 16}, 0},
}, 0x7, 0x7, "DQ ODT"};

static const struct mr_adjustments ca_odt_adj = {{
	{{139, 152}, 28, {132, 139}, {4, 20}, 0},
	{{140, 154}, 4, {129, 137}, {20, 4}, 0},
	{{140, 154}, 12, {127, 134}, {4, 20}, 0},
}, 0x7, 0x7, "CA ODT"};

static const struct mr_adjustments mr3_adj = {{
	{{138, 152}, 0, {131, 139}, {16, 0}, 0},
	{{138, 152}, 16, {129, 136}, {0, 16}, 0},
	{{139, 153}, 0, {126, 134}, {16, 0}, 0},
}, 0xffff, 0xffff, "MR3"};

static const struct mr_adjustments mr12_adj = {{
	{{140, 154}, 16, {132, 139}, {8, 24}, 0},
	{{141, 155}, 0, {129, 137}, {24, 8}, 0},
	{{141, 155}, 16, {127, 134}, {8, 24}, 0},
}, 0xffff, 0xff, "MR12"};

static const struct mr_adjustments mr14_adj = {{
	{{142, 156}, 16, {132, 140}, {16, 0}, 0},
	{{143, 157}, 0, {130, 137}, {0, 16}, 0},
	{{143, 157}, 16, {127, 135}, {16, 0}, 0},
}, 0xffff, 0xff, "MR14"};

static void mr_adjust(volatile u32 *pctl, volatile u32 *pi, const struct mr_adjustments *adj, u32 regset, u32 val) {
	printf("setting %s to %x, pctl mask 0x%x\n", (const char *)&adj->name[0], val, (u32)adj->pctl_mask);
	const struct mr_adj_regset *rs = &adj->loc[regset];
	u32 pctl_mask = (u32)adj->pctl_mask << rs->pctl_shift;
	u32 pctl_value = (val & adj->pctl_mask) << rs->pctl_shift;
	for_range(i, 0, 2) {
		printf("pctl%u mask %x value %x\n", (u32)rs->pctl_regs[i], pctl_mask, pctl_value);
		volatile u32 *reg = &pctl[rs->pctl_regs[i]];
		printf("read %08x\n", *reg);
		*reg = (*reg & ~pctl_mask) | pctl_value;
		printf("readback %08x\n", *reg);
	}
	u32 pi_mask = adj->pi_mask, pi_value = val & pi_mask;
	for_range(i, 0, 2) {
		for_range(j, 0, 2) {
			volatile u32 *reg = &pi[rs->pi_regs[j] + 15*i];
			u8 shift = rs->pi_shifts[j];
			*reg = (*reg & ~(pi_mask << shift)) | pi_value << shift;
		}
	}
}

/*#define ODT_DS_VARIES 0xff

static const struct odt_settings lpddr4_odt_settingsodt = {
	.flags = ODT_TSEL_CLEAN | ODT_SET_BOOST_SLEW | ODT_SET_RX_CM_INPUT,
	.soc_odt = 0,
	.ds = {
		[ODT_RD] = {ODT_DS_HI_Z, ODT_DS_VARIES},
		[ODT_IDLE] = {ODT_DS_HI_Z, ODT_DS_HI_Z},
		[ODT_WR_DQ] = {ODT_DS_VARIES, ODT_DS_34_3},
		[ODT_WR_CA] = {ODT_DS_VARIES, ODT_DS_34_3},
		[ODT_CKCS] = {ODT_DS_VARIES, ODT_DS_34_3},
	},
	.mode_dq = 7, .value_dq = 0, /* varies based on rd_vref *
	.mode_ac = 6, .value_ac = 3,
	.drive_mode = 5 /*varies based on rd_ref *
};*/

void lpddr4_get_odt_settings(struct odt_settings *odt, const struct odt_preset *preset) {
	printf("odt %zx\n", (u64)odt);
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
	yield();
	printf("dqn %x %zx\n", (u32)odt->ds[ODT_WR_DQ][ODT_N], (u64)odt);
	printf("dqn %x\n", (u32)odt->ds[ODT_WR_DQ][ODT_N]);
	printf("mode %x value %x drive_mode %x\n", (u32)odt->mode_dq, (u32)odt->value_dq, (u32)odt->drive_mode);
	printf("dqn %u\n", (u32)odt->ds[ODT_WR_DQ][ODT_N]);
}

void lpddr4_modify_config(struct dram_cfg *cfg) {
	u32 *pctl = &cfg->regs.pctl[0], *pi = &cfg->regs.pi[0];
	struct phy_cfg *phy = &cfg->regs.phy;
	mr_adjust(pctl, pi, &dq_odt_adj, 2, odt_50mhz.dram.dq_odt);
	mr_adjust(pctl, pi, &ca_odt_adj, 2, odt_50mhz.dram.ca_odt);
	mr_adjust(pctl, pi, &mr3_adj, 2, odt_50mhz.dram.pdds << 3 | 1);
	mr_adjust(pctl, pi, &mr12_adj, 2, odt_50mhz.dram.ca_vref);
	mr_adjust(pctl, pi, &mr14_adj, 2, odt_50mhz.dram.dq_vref);

	struct odt_settings odt;
	lpddr4_get_odt_settings(&odt, &odt_50mhz);
	odt.flags |= ODT_SET_RST_DRIVE;
	printf("dqn %x\n", (u32)odt.ds[ODT_WR_DQ][ODT_N]);
	set_drive_strength(pctl, (u32*)phy, &cfg_layout, &odt);
	/*set_phy_io((u32 *)phy, &cfg_layout, &odt);*/
	static const char *const arr[] = {"rd", "idle", "dq", "ca", "ckcs"};
	for_array(i, arr) {printf("%s n=%x p=%x\n", arr[i], (u32)odt.ds[i][ODT_N], (u32)odt.ds[i][ODT_P]);}

	/* read 2-cycle preamble */
	pctl[200] |= 3 << 24;
	for_dslice(i) {phy->dslice[i][7] |= 3 << 24;}
	/* boot frequency 2-cycle preamble */
	for_dslice(i) {phy->dslice[i][2] |= 3 << 16;}
	
	pi[45] |= 3 << 8;
	pi[58] |= 1;

	/* disable power reduction to use bypass mode */
	for_dslice(i) {phy->dslice[i][10] |= 1 << 16;}
	pi[45] |= 1 << 24;
	pi[61] |= 1 << 24;
	pi[76] |= 1 << 24;
	pi[77] |= 1;
}
