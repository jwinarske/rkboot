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

struct regshift {u16 reg;u8 shift;};

static void apply32_multiple(const struct regshift *regs, u8 count, volatile u32 *base, u32 delta, u64 op) {
	u32 mask = op >> 32, val = (u32)op;
	for_range(i, 0, count) {
		spew("reg %u (delta %u) mask %x val %x shift %u\n", (u32)regs[i].reg, delta, mask, val, regs[i].shift);
		clrset32(base + (regs[i].reg - delta), mask << regs[i].shift, val << regs[i].shift);
	}
}

void set_phy_io(volatile u32 *phy, const struct odt_settings *odt) {
	static const struct regshift dq_regs[] = {
		{913, 8}, {914, 0}, {914, 16}, {915, 0}
	}; apply32_multiple(dq_regs, ARRAY_SIZE(dq_regs), phy, 0,
		SET_BITS32(12, odt->value_dq | 0x0100 | odt->mode_dq << 9)
	);

	static const struct regshift mode_regs[] = {
		{924, 15}, {926, 6}, {927, 6}, {928, 14},
		{929, 14}, {935, 14}, {937, 14}, {939, 14},
	}; apply32_multiple(mode_regs, ARRAY_SIZE(mode_regs), phy, 0,
		SET_BITS32(3, odt->drive_mode)
	);
}
