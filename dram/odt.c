/* SPDX-License-Identifier: CC0-1.0 */
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
	}
};

const struct odt_preset odt_933mhz = {
	.dram = {
		.dq_odt = 1,
		.ca_odt = 0,
		.pdds = 3,
		.dq_vref = 0x72,
		.ca_vref = 0x72,
	},
	.phy = {
		.rd_odt = ODT_DS_80,
		.wr_dq_drv = ODT_DS_48,
		.wr_ca_drv = ODT_DS_40,
		.wr_ckcs_drv = ODT_DS_40,
		.rd_vref = 20,
		.rd_odt_en = 1,
	}
};

void set_drive_strength(volatile u32 *phy, const struct phy_layout *layout, const struct odt_settings *odt) {
	volatile u32 *ca_base = phy + layout->ca_offs;

	u32 wr_en = (odt->flags / ODT_WR_EN) & 1;
	for_aslice(i) {
		clrset32(ca_base + layout->aslice * i + 6, 0x0100, wr_en << 8);
	}
}

void set_phy_io(volatile u32 *phy, u32 delta, const struct odt_settings *odt) {
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
		debugs("setting boost + slew\n");
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
