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
/*static inline volatile u32 *msch_base_for(u32 channel) {
	return (volatile u32 *)(0xffa84000 + MC_CHANNEL_STRIDE * (uintptr_t)channel);
}*/

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
	for_dslice(i) {apply32v(&phy->dslice[i][PHY_SW_MASTER_MODE], dop);}
	for_aslice(i) {apply32v(&phy->aslice[i][PHY_ADR_SW_MASTER_MODE], aop);}
	puts(" done.\n");
}

static void set_memory_map(volatile u32 *pctl, volatile u32 *pi, const struct channel_config *ch_cfg, enum dramtype type) {
	u32 csmask = ch_cfg->csmask;
	static const u8 row_bits_table[] = {16, 16, 15, 14, 16, 14};
	u32 row_diff = 16 - bounds_checked(row_bits_table, ch_cfg->ddrconfig);
	_Bool reduc = ch_cfg->bw != 2;
	u32 bk_diff = 3 - ch_cfg->bk, col_diff = 12 - ch_cfg->col;

	printf("set_memory_map: pctl@%zx pi@%zx reduc%u col%u row%u bk%u cs%u\n", (u64)pctl, (u64)pi, (u32)reduc, col_diff, row_diff, (u32)bk_diff, csmask);
	u64 col_op = SET_BITS32(4, col_diff);
	u64 bk_row_op = SET_BITS32(2, bk_diff) << 16
		| SET_BITS32(3, row_diff) << 24;

	apply32v(pctl + 191, col_op);
	apply32v(pctl + 190, bk_row_op);
	apply32v(pctl + 196,
		SET_BITS32(2, csmask)
		| SET_BITS32(1, reduc) << 16
	);

	apply32v(pi + 199, col_op);
	apply32v(pi + 155, bk_row_op);

	if (type == LPDDR4) {csmask |= csmask << 2;}

	apply32v(pi + 41, SET_BITS32(4, csmask) << 24);

	if (type == DDR3) {pi[34] = 0x2ec7ffff;}
}

void dump_cfg(u32 *pctl, u32 *pi, struct phy_cfg *phy) {
	printf(".regs = {\n  .pctl = {\n");
	for_range(i, 0, NUM_PCTL_REGS) {printf("    0x%08x, // PCTL%03u\n", pctl[i], i);}
	printf("  },\n  .pi = {\n");
	for_range(i, 0, NUM_PI_REGS) {printf("    0x%08x, // PI%03u\n", pi[i], i);}
	printf("  },\n  .phy = {\n    .dslice = {\n");
	for_dslice(i) {
		printf("      {\n");
		for_range(j, 0, NUM_PHY_DSLICE_REGS) {printf("        0x%08x, // DSLC%u_%u\n", phy->dslice[i][j], i, j);}
		printf("      },\n");
	}
	printf("    },\n    .aslice = {\n");
	for_aslice(i) {
		printf("      {\n");
		for_range(j, 0, NUM_PHY_ASLICE_REGS) {printf("        0x%08x, // ASLC%u_%u\n", phy->aslice[i][j], i, j);}
		printf("      },\n");
	}
	printf("    },\n    .global = {\n");
	for_range(i, 0, NUM_PHY_GLOBAL_REGS) {printf("      0x%08x, // PHY%u\n", phy->global[i], i + 896);}
	printf("    }\n  }\n}\n");
}

inline u32 mr_read_command(u8 mr, u8 cs) {
	return (u32)mr | ((u32)cs << 8) | (1 << 16);
}

enum mrrresult {MRR_OK = 0, MRR_ERROR = 1, MRR_TIMEOUT};

enum mrrresult read_mr(volatile u32 *pctl, u8 mr, u8 cs, u32 *out) {
	pctl[PCTL_READ_MODEREG] = mr_read_command(mr, cs) << 8;
	u32 status;
	u64 start_time = get_timestamp();
	while (!((status = pctl[PCTL_INT_STATUS]) & 0x00201000)) {
		if (get_timestamp() - start_time > 100 * CYCLES_PER_MICROSECOND) {
			*out = 0;
			return MRR_TIMEOUT;
		}
	}
	pctl[PCTL_INT_ACK] = 0x00201000;
	if ((status >> 12) & 1) {
		*out = pctl[PCTL_MRR_ERROR_STATUS];
		return MRR_ERROR;
	}
	*out = pctl[PCTL_PERIPHERAL_MRR_DATA];
	return MRR_OK;
}

void dump_mrs() {
	for_channel(ch) {
		printf("channel %u\n", ch);
		for_range(cs, 0, 2) {
			printf("CS=%u\n", cs);
			for_range(mr, 0, 26) {
				if (!((1 << mr) & 0x30c51f1)) {continue;}
				u32 mr_value; enum mrrresult res;
				if ((res = read_mr(pctl_base_for(ch), mr, cs, &mr_value))) {
					if (res == MRR_TIMEOUT) {printf("MRR timeout for mr%u\n", mr);}
					else {printf("MRR error %x for MR%u\n", mr_value, mr);}
				} else {
					printf("MR%u = %x\n", mr, mr_value);
				}
			}
		}
	}
}

#define PWRUP_SREF_EXIT (1 << 16)
#define START 1

_Bool try_init(u32 chmask, struct dram_cfg *cfg, const struct odt_settings *odt) {
	softreset_memory_controller();
	for_channel(ch) {
		if (!((1 << ch) & chmask)) {continue;}
		volatile u32 *pctl = pctl_base_for(ch);
		volatile u32 *pi = pi_base_for(ch);
		volatile struct phy_regs *phy = phy_for(ch);
		set_phy_dll_bypass(phy, cfg->mhz < 125);

		set_phy_io((volatile u32 *)phy, &reg_layout, odt);

		copy_reg_range(
			&cfg->regs.pctl[PCTL_DRAM_CLASS + 1],
			pctl + PCTL_DRAM_CLASS + 1,
			NUM_PCTL_REGS - PCTL_DRAM_CLASS - 1
		);
		/* must happen after setting NO_PHY_IND_TRAIN_INT in the transfer above */
		pctl[PCTL_DRAM_CLASS] = cfg->regs.pctl[PCTL_DRAM_CLASS];

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

		pi[0] |= START;
		pctl[0] |= START;

		assert(cfg->type == LPDDR4);

		copy_reg_range(&phy_cfg->global[0], &phy->global[0], NUM_PHY_GLOBAL_REGS);
		for_dslice(i) {copy_reg_range(&phy_cfg->dslice[i][0], &phy->dslice[i][0], NUM_PHY_DSLICE_REGS);}
		for_aslice(i) {copy_reg_range(&phy_cfg->aslice[i][0], &phy->aslice[i][0], NUM_PHY_ASLICE_REGS);}

		/* TODO: do next register set manipulation */

		phy->global[0] = phy_cfg->global[0] & ~(u32)0x0300;

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
	dump_mrs();
	return 1;
}

void ddrinit() {
	log("initializing DRAM%s\n", "");
	if (!setup_pll(cru + CRU_DPLL_CON, 50)) {die("PLL setup failed\n");}
	/* not doing this will make the CPU hang during the DLL bypass step */
	*(volatile u32*)0xff330040 = 0xc000c000;
	/*dump_cfg(&init_cfg.regs.pctl[0], &init_cfg.regs.pi[0], &init_cfg.regs.phy);*/
	struct odt_settings odt;
	lpddr4_get_odt_settings(&odt, &odt_50mhz);
	odt.flags |= ODT_SET_RST_DRIVE;
	switch (init_cfg.type) {
	case LPDDR4:
		lpddr4_modify_config(&init_cfg, &odt);
		break;
	default:
		die("unsupported DDR type %u\n", (u32)init_cfg.type);
	}
	/*dump_cfg(&init_cfg.regs.pctl[0], &init_cfg.regs.pi[0], &init_cfg.regs.phy);*/
	try_init(3, &init_cfg, &odt);
}
