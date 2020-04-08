#include "rk3399-dmc.h"

struct dram_cfg init_cfg = {
	.msch = {
		.timing1 = ACT2ACT(34) | RD2MISS(29)
			| WR2MISS(36) | READ_LATENCY(128),
		.timing2 = RD2WR(8) | WR2RD(15) | RRD(5) | FAW(21),
		.timing3 = BURST_PENALTY(2) | WR2MWR(6),
		.dev2dev = BUSRD2RD(2) | BUSRD2WR(2)
			| BUSWR2RD(1) | BUSWR2WR(2),
		.ddrmode = FAW_BANK | BURST_SIZE(1) | MWR_SIZE(2),
	},
	.regs = {
		.pctl = {
#include <pctl.gen.c>
		},
		.pi = {
#include <pi.gen.c>
		},
		.phy = {
			.dslice = {
#include <dslice.gen.c>
			},
			.aslice = {{
#include <aslice0.gen.c>
			}, {
#include <aslice1.gen.c>
			}, {
#include <aslice2.gen.c>
			}},
			.global = {
#include <adrctl.gen.c>
			},
		},
	}
};

const struct phy_update phy_400mhz = {
#include "phy_cfg2.inc.c"
};

const struct phy_update phy_800mhz = {
#include "phy_cfg3.inc.c"
};
