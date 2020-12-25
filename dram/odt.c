/* SPDX-License-Identifier: CC0-1.0 */
#include <main.h>
#include <rk3399.h>
#include "rk3399-dmc.h"

const struct odt_preset odt_50mhz = {
	.phy = {
		.rd_vref = 41,
	}
};

const struct odt_preset odt_600mhz = {
	.phy = {
		.rd_vref = 32,
	}
};

const struct odt_preset odt_933mhz = {
	.phy = {
		.rd_vref = 20,
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
}
