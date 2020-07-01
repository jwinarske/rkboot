/* SPDX-License-Identifier: CC0-1.0 */
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
	.two_cycle_preamble = 3,
	.wraddr_shift0123 = 0,
	.wraddr_shift45 = 0,
	.negedge_pll_switch = 1,
	.grp_shift01 = 0,
	.pll_ctrl = 0x03221302,
	.dslice_update = {
#include <dslice_f2.gen.c>
	},
	.slave_master_delays = {
#include <slave_master_delays_f2.gen.c>
	},
	.grp_slave_delay = {
#include <grp_slave_delay_f2.gen.c>
	},
};

const struct phy_update phy_800mhz = {
	.two_cycle_preamble = 3,
	.wraddr_shift0123 = 0,
	.wraddr_shift45 = 0,
	.negedge_pll_switch = 0,
	.grp_shift01 = 0,
	.pll_ctrl = 0x01221102,
	.dslice_update = {
#include <dslice_f1.gen.c>
	},
	.slave_master_delays = {
#include <slave_master_delays_f1.gen.c>
	},
	.grp_slave_delay = {
#include <grp_slave_delay_f1.gen.c>
	},
};
