#include <main.h>
#include <rk3399.h>
#include "rk3399-dmc.h"

const struct phy_layout cfg_layout = {
	.dslice = NUM_PHY_DSLICE_REGS,
	.aslice = NUM_PHY_ASLICE_REGS,
	.global_diff = (128 - NUM_PHY_DSLICE_REGS) * 4 + (128 - NUM_PHY_ASLICE_REGS) * 3,
	.ca_offs = NUM_PHY_DSLICE_REGS*4
}, reg_layout = {
	.dslice = 128,
	.aslice = 128,
	.global_diff = 0,
	.ca_offs = 512
};

struct dram_cfg init_cfg = {
#include "initcfg.inc.c"
};

enum {MC_NUM_CHANNELS = 2, MC_CHANNEL_STRIDE = 0x8000, MC_NUM_FREQUENCIES = 3};
static inline volatile struct phy_regs *phy_for(u32 channel) {
	return (volatile struct phy_regs *)(0xffa82000 + MC_CHANNEL_STRIDE * (uintptr_t)channel);
}
static inline volatile u32 *pctl_base_for(u32 channel) {
	return (volatile u32 *)(0xffa80000 + MC_CHANNEL_STRIDE * (uintptr_t)channel);
}
static inline volatile u32 *pi_base_for(u32 channel) {
	return (volatile u32 *)(0xffa80800 + MC_CHANNEL_STRIDE * (uintptr_t)channel);
}
static inline volatile u32 *msch_base_for(u32 channel) {
	return (volatile u32 *)(0xffa84000 + MC_CHANNEL_STRIDE * (uintptr_t)channel);
}

static void set_ddr_reset_request(_Bool controller, _Bool phy) {
	cru[CRU_SOFTRST_CON + 4] = 0x33000000 | (controller << 12) | (controller << 8) | (phy << 13) | (phy << 9);
}

static void softreset_memory_controller() {
	set_ddr_reset_request(1, 1);
	udelay(10);
	set_ddr_reset_request(1, 0);
	udelay(10);
	set_ddr_reset_request(0, 0);
	udelay(10);
}

static void copy_reg_range(const volatile u32 *a, volatile u32 *b, u32 n) {
	while (n--) {*b++ = *a++;}
}

enum {
	REG_PHY_SW_MASTER_MODE = 86,
};
enum {
	REG_PHY_ADR_SW_MASTER_MODE = 35,
};
enum {
	REG_PCTL_DRAM_CLASS = 0,
};

static void set_phy_dll_bypass(volatile struct phy_regs *phy, _Bool enable) {
	u64 aop, dop;
	if (enable) {
		puts("enabling DLL bypass …");
		aop = SET_BITS32(2, 3) << 10;
		dop = SET_BITS32(2, 3) << 18;
	} else {
		puts("disabling DLL bypass …");
		aop = SET_BITS32(2, 0) << 10;
		dop = SET_BITS32(2, 0) << 18;
	}
	for_dslice(i) {apply32v(&phy->dslice[i][REG_PHY_SW_MASTER_MODE], dop);}
	for_aslice(i) {apply32v(&phy->aslice[i][REG_PHY_ADR_SW_MASTER_MODE], aop);}
	puts(" done.\n");
}

static void set_memory_map(volatile u32 *pctl, volatile u32 *pi, const struct channel_config *ch_cfg, enum dramtype type) {
	u8 csmask = ch_cfg->csmask;
	static const u8 row_bits_table[] = {16, 16, 15, 14, 16, 14};
	u8 row_bits = 16 - bounds_checked(row_bits_table, ch_cfg->ddrconfig);
	_Bool reduc = ch_cfg->bw == 2;
	u64 col_op = SET_BITS32(4, 12 - ch_cfg->col);
	u64 bk_row_op = SET_BITS32(2, 3 - ch_cfg->bk) << 16
		| SET_BITS32(3, row_bits) << 24;
	apply32v(pctl + 191, col_op);
	apply32v(pctl + 190, bk_row_op);
	apply32v(pctl + 196,
		SET_BITS32(2, csmask)
		| SET_BITS32(1, reduc) << 16
	);

	apply32v(pi + 199, col_op);
	apply32v(pi + 155, bk_row_op);
	if (type == LPDDR4) {
		csmask |= csmask << 2;
	}

	apply32v(pi + 41, SET_BITS32(4, csmask) << 24);

	if (type == DDR3) {
		pi[34] = 0x2ec7ffff;
	}
}

static void dump_cfg(u32 *pctl, u32 *pi, struct phy_cfg *phy) {
	printf(".regs = {\n  .pctl = {\n");
	for_range(i, 0, NUM_PCTL_REGS) {printf("     %08x,\n", pctl[i]);}
	printf("  },\n  .pi = {\n");
	for_range(i, 0, NUM_PI_REGS) {printf("     %08x,\n", pi[i]);}
	printf("  },\n  .phy = {\n    .dslice = {\n");
	for_dslice(i) {
		printf("      {\n");
		for_range(j, 0, NUM_PHY_DSLICE_REGS) {printf("     %08x,\n", phy->dslice[i][j]);}
		printf("      },");
	}
	printf("    },\n    .aslice = {\n");
	for_aslice(i) {
		printf("      {\n");
		for_range(j, 0, NUM_PHY_ASLICE_REGS) {printf("     %08x,\n", phy->aslice[i][j]);}
		printf("      },");
	}
	printf("    },\n    .global = {\n");
	for_range(i, 0, NUM_PHY_GLOBAL_REGS) {printf("     %08x,\n", phy->global[i]);}
	printf("    }\n  }\n}");
}

#define PWRUP_SREF_EXIT (1 << 16)
#define START 1

_Bool try_init(u32 chmask, struct dram_cfg *cfg) {
	softreset_memory_controller();
	for_channel(ch) {
		if (!((1 << ch) & chmask)) {continue;}
		volatile u32 *pctl = pctl_base_for(ch);
		volatile u32 *pi = pi_base_for(ch);
		volatile struct phy_regs *phy = phy_for(ch);
		set_phy_dll_bypass(phy, cfg->mhz < 125);
		switch (cfg->type) {
		case LPDDR4:
			lpddr4_modify_config(cfg);
			break;
		default:
			die("unsupported DDR type");
		}
		dump_cfg(&cfg->regs.pctl[0], &cfg->regs.pi[0], &cfg->regs.phy);
		copy_reg_range(
			&cfg->regs.pctl[REG_PCTL_DRAM_CLASS + 1],
			pctl + REG_PCTL_DRAM_CLASS + 1,
			NUM_PCTL_REGS - REG_PCTL_DRAM_CLASS - 1
		);
		/* must happen after setting NO_PHY_IND_TRAIN_INT in the transfer above */
		pctl[REG_PCTL_DRAM_CLASS] = cfg->regs.pctl[REG_PCTL_DRAM_CLASS];
		
		if (cfg->type == LPDDR4 && chmask == 3 && ch == 1) {
			/* delay ZQ calibration */
			pctl[14] += cfg->mhz * 1000;
		}

		copy_reg_range(&cfg->regs.pi[0], pi, NUM_PI_REGS);
		set_memory_map(pctl, pi, &cfg->channels[ch], cfg->type);
		
		const struct phy_cfg *phy_cfg = &cfg->regs.phy;
		for_range(i, 0, 3) {phy->PHY_GLOBAL(910 + i) = phy_cfg->PHY_GLOBAL(910 + i);}

		if (cfg->type == LPDDR4) {
			phy->PHY_GLOBAL(898) = phy_cfg->PHY_GLOBAL(898);
			phy->PHY_GLOBAL(919) = phy_cfg->PHY_GLOBAL(919);
		}

		u32 val = pctl[68];
		cfg->channels[ch].pwrup_sref_exit = val & PWRUP_SREF_EXIT;
		pctl[68] = val & ~PWRUP_SREF_EXIT;
		
		apply32v(&phy->PHY_GLOBAL(957), SET_BITS32(2, 1) << 24);

		pi[0] = START;
		pctl[0] = START;

		assert(cfg->type == LPDDR4);

		copy_reg_range(&phy_cfg->global[0], &phy->global[0], NUM_PHY_GLOBAL_REGS);
		for_dslice(i) {copy_reg_range(&phy_cfg->dslice[i][0], &phy->dslice[i][0], NUM_PHY_DSLICE_REGS);}
		for_aslice(i) {copy_reg_range(&phy_cfg->aslice[i][0], &phy->dslice[i][0], NUM_PHY_ASLICE_REGS);}

		/* TODO: do next register set manipulation */

		for_dslice(i) {
			phy->dslice[i][83] = phy_cfg->dslice[i][83] + 0x00100000;
			phy->dslice[i][84] = phy_cfg->dslice[i][84] + 0x1000;
		}
		
		if (cfg->type == LPDDR4) {
			/* improve dqs and dq phase */
			for_dslice(i) {apply32v(&phy->dslice[i][1], SET_BITS32(12, 0x680) << 8);}
		}
		if (ch == 1) {
			/* workaround 366 ball reset */
			clrset32(&phy->PHY_GLOBAL(937), 0xff, ODT_DS_240 | ODT_DS_240 << 4);
		}
	}
	for_channel(ch) {
		if (!(chmask & (1 << ch))) {continue;}
		volatile u32 *pctl = pctl_base_for(ch);
		volatile struct phy_regs *phy = phy_for(ch);
		grf[GRF_DDRC_CON + 2*ch] = SET_BITS16(1, 0) << 8;
		apply32v(&phy->PHY_GLOBAL(957), SET_BITS32(2, 2) << 24);
		
		_Bool locked = 0;
		for (u32 i = 0; i < 1000; i += 1) {
			locked = (pctl[PCTL_INT_STATUS] >> 3) & 1;
			if (locked) {break;}
			udelay(1);
		}
		if (!locked) {
			printf("channel %u init fail\n", ch);
			return 0;
		}
		grf[GRF_DDRC_CON + 2*ch] = SET_BITS16(1, 1) << 8;
		for_dslice(i) {
			for_range(reg, 53, 58) {phy->dslice[i][reg] = 0x08200820;}
			clrset32(&phy->dslice[i][58], 0xffff, 0x0820);
		}
		log("channel %u initialized\n", ch);
	}
	return 1;
}

void ddrinit() {
	log("initializing DRAM\n");
	if (!setup_pll(cru + CRU_DPLL_CON, 50)) {die("PLL setup failed\n");}
	/* not doing this will make the CPU hang during the DLL bypass step */
	*(volatile u32*)0xff330040 = 0xc000c000;
	try_init(3, &init_cfg);
}
