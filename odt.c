#include <main.h>
#include <rk3399.h>
#include "rk3399-dmc.h"

const struct odt_preset odt_50mhz = {
	.dram = {
		.dq_odt = 0,
		.ca_odt = 0,
		.pdds = 6,
		.dq_vref = 0x72,
		.ca_vref = 0x72,
	},
	.phy = {
		.rd_odt = ODT_DS_HI_Z,
		.wr_dq_drv = ODT_DS_40,
		.wr_ca_drv = ODT_DS_40,
		.wr_ckcs_drv = ODT_DS_40,
		.rd_vref = 41,
		.rd_odt_en = 0,
		.soc_odt = 0,
	}
};

const struct odt_preset odt_600mhz = {
	.dram = {
		.dq_odt = 1,
		.ca_odt = 0,
		.pdds = 6,
		.dq_vref = 0x72,
		.ca_vref = 0x72,
	},
	.phy = {
		.rd_odt = ODT_DS_HI_Z,
		.wr_dq_drv = ODT_DS_48,
		.wr_ca_drv = ODT_DS_40,
		.wr_ckcs_drv = ODT_DS_40,
		.rd_vref = 32,
		.rd_odt_en = 0,
		.soc_odt = 0
	}
};

#define CS0_MR22_VAL		0
#define CS1_MR22_VAL		3

void set_drive_strength(volatile u32 *pctl, volatile u32 *phy, const struct phy_layout *layout, const struct odt_settings *odt) {
	u64 cs0_op = SET_BITS32(8, odt->soc_odt | CS0_MR22_VAL << 3);
	apply32v(pctl+145, cs0_op);
	apply32v(pctl+146, cs0_op << 16 | cs0_op);
	u64 cs1_op = SET_BITS32(8, odt->soc_odt | CS1_MR22_VAL << 3);
	apply32v(pctl+159, cs1_op << 16);
	apply32v(pctl+160, cs1_op << 16 | cs1_op);
	
	static const char *const arr[] = {"rd", "idle", "dq", "ca", "ckcs"};
	for_array(i, arr) {printf("%s n=%u p=%u\n", arr[i], (u32)odt->ds[i][ODT_N], (u32)odt->ds[i][ODT_P]);}

	u32 tsel_dq = (u32)odt->ds[ODT_WR_DQ][ODT_N]
		| (u32)odt->ds[ODT_WR_DQ][ODT_P] << 4;
	u32 tsel_val = (u32)odt->ds[ODT_RD][ODT_N]
		| (u32)odt->ds[ODT_RD][ODT_P] << 4
		| tsel_dq << 8
		| (u32)odt->ds[ODT_IDLE][ODT_N] << 16
		| (u32)odt->ds[ODT_IDLE][ODT_P] << 20;
	for_dslice(i) {clrset32(phy+layout->dslice*i + 6, 0xffffff, tsel_val);}
	for_dslice(i) {clrset32(phy+layout->dslice*i + 7, 0xffffff, tsel_val);}

	for_array(i, arr) {printf("%s n=%u p=%u\n", arr[i], (u32)odt->ds[i][ODT_N], (u32)odt->ds[i][ODT_P]);}
	
	u32 tsel_ca = (u32)odt->ds[ODT_WR_CA][ODT_N]
		| (u32)odt->ds[ODT_WR_CA][ODT_P] << 4;
	volatile u32 *ca_base = phy + layout->ca_offs;
	printf("tsel_ca: %x", tsel_ca);
	if (odt->flags & ODT_TSEL_CLEAN) {
		for_aslice(i) {ca_base[layout->aslice * i + 32] = 0x30000 | tsel_ca;}
	} else {
		for_aslice(i) {clrset32(ca_base + layout->aslice * i + 32, 0xff, tsel_ca);}
	}

	u32 delta = layout->global_diff;
	clrset32(phy + (928 - delta), 0xff, tsel_ca);
	if (odt->flags & ODT_SET_RST_DRIVE) {
		clrset32(phy + (937 - delta), 0xff, tsel_ca);
	}
	clrset32(phy + (935 - delta), 0xff, tsel_ca);
	
	u32 tsel_ckcs = (u32)odt->ds[ODT_CKCS][ODT_N]
		| (u32)odt->ds[ODT_CKCS][ODT_P] << 4;
	clrset32(phy + (939 - delta), 0xff, tsel_ckcs);
	clrset32(phy + (929 - delta), 0xff, tsel_ckcs);

	clrset32(phy + (924 - delta), 0xff, tsel_ca);

	clrset32(phy + (925 - delta), 0xff, tsel_dq);

	u32 enable = odt->flags & ODT_TSEL_ENABLE_MASK;
	for_dslice(i) {clrset32(phy + layout->dslice*i + 5, 0x00070000, enable << 16);}
	for_dslice(i) {clrset32(phy + layout->dslice*i + 6, 0x07000000, enable << 24);}

	u32 wr_en = (odt->flags / ODT_WR_EN) & 1;
	for_aslice(i) {
		clrset32(ca_base + layout->aslice * i + 6, 0x0100, wr_en << 8);
	}

	static const u16 regs[] = {933, 938, 936, 940, 934, 930};
	for_range(i, 0, ARRAY_SIZE(regs)) {
		clrset32(phy + (regs[i] - delta), 0x20000, wr_en << 17);
	}
}

void set_phy_io(volatile u32 *phy, const struct phy_layout *layout, const struct odt_settings *odt) {
	u32 delta = layout->global_diff;
	static const struct regshift dq_regs[] = {
		{913, 8}, {914, 0}, {914, 16}, {915, 0}
	}; apply32_multiple(dq_regs, ARRAY_SIZE(dq_regs), phy, delta,
		SET_BITS32(12, odt->value_dq | 0x0100 | odt->mode_dq << 9)
	);
	
	apply32v(phy + (915 - delta), SET_BITS32(12, odt->value_ac | 0x0100 | odt->mode_ac << 9) << 16);
	static const struct regshift mode_regs[] = {
		{924, 15}, {926, 6}, {927, 6}, {928, 14},
		{929, 14}, {935, 14}, {937, 14}, {939, 14},
	}; apply32_multiple(mode_regs, ARRAY_SIZE(mode_regs), phy, delta,
		SET_BITS32(3, odt->drive_mode)
	);
	
	if (odt->flags & ODT_SET_BOOST_SLEW) {
		puts("setting boost + slew\n");
		static const struct regshift boost_regs[] = {
			{925, 8}, {926, 12}, {927, 14}, {928, 20},
			{929, 22}, {935, 20}, {937, 20}, {939, 20},
		}; apply32_multiple(boost_regs, ARRAY_SIZE(boost_regs), phy, delta,
			SET_BITS32(8, 0x11)
		);
		static const struct regshift slew_regs[] = {
			{924, 8}, {926, 0}, {927, 0}, {928, 8},
			{929, 8}, {935, 8}, {937, 8}, {939, 8},
		}; apply32_multiple(slew_regs, ARRAY_SIZE(slew_regs), phy, delta,
			SET_BITS32(6, 0x09)
		);
	}

	apply32_multiple(speed_regs, ARRAY_SIZE(speed_regs), phy, delta,
		SET_BITS32(2, 2)
	);
	
	if (odt->flags & ODT_SET_RX_CM_INPUT) {
		static const struct regshift rx_cm_input_regs[] = {
			{924, 14}, {926, 11}, {927, 13}, {928, 19},
			{929, 21}, {935, 19}, {937, 19}, {939, 19},
		}; apply32_multiple(rx_cm_input_regs, ARRAY_SIZE(rx_cm_input_regs), phy, delta,
			SET_BITS32(1, 1)
		);
	}
}
