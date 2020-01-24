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

struct phy_cfg phy_400mhz = {
#include "phy_cfg2.inc.c"
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

const struct regshift speed_regs[8] = {
	{924, 21}, {926, 9}, {927, 9}, {928, 17},
	{929, 17}, {935, 17}, {937, 17}, {939, 17},
};
void apply32_multiple(const struct regshift *regs, u8 count, volatile u32 *base, u32 delta, u64 op) {
	u32 mask = op >> 32, val = (u32)op;
	for_range(i, 0, count) {
		printf("reg %u (delta %u) mask %x val %x shift %u\n", (u32)regs[i].reg, delta, mask, val, regs[i].shift);
		clrset32(base + (regs[i].reg - delta), mask << regs[i].shift, val << regs[i].shift);
	}
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

/*void set_per_cs_training_index(volatile struct phy_regs *phy, u32 rank) {
	if (phy[84] & (1 << 16)) {
		printf("set per-cs training\n");
		for_dslice(i) {apply32v(&phy->dslice[i][8], SET_BITS32(1, rank));}
	}
}

_Bool read_gate_training(u32 idx, volatile u32 *pi, volatile u32 *phy) {
	clrset32(pi + 80, SET_BITS32(2, 2) << 24);
	clrset32(pi + 74, (SET_BITS32(1, 1) << 16) | (SET_BITS32(2, idx) << 24));
	while (1) {
		u32 status = pi[174];
		if ((phy[43] | phy[171] | phy[299] | phy[427]) & (3 << 22)) {puts("obs error\n");return 0;}
		if (status & (1 << 11)) {puts("flag error\n");return 0;}
		if ((status & (1 << 17)) && (status & (1 << 21))) {break;}
	}
	pi[175] = 0x3f7c;
	/*clrset32(pi + 80, SET_BITS32(2, 0) << 24);*
	return 1;
}*/

void update_phy_bank(volatile struct phy_regs *phy, u32 bank, const struct phy_cfg *cfg, u32 speed) {
	phy->PHY_GLOBAL(896) = (cfg->PHY_GLOBAL(896) & 0xffff00ff) | bank << 8;
	phy->PHY_GLOBAL(911) = cfg->PHY_GLOBAL(911);
	apply32v(&phy->PHY_GLOBAL(913), SET_BITS32(1, cfg->PHY_GLOBAL(913)));
	copy_reg_range(&cfg->PHY_GLOBAL(916), &phy->PHY_GLOBAL(916), 3);
	for_aslice(i) {
		phy->aslice[i][0] = cfg->aslice[i][0];
		apply32v(&phy->aslice[i][1], SET_BITS32(16, cfg->aslice[i][1]));
	}
	for_aslice(i) {copy_reg_range(&cfg->aslice[i][32], &phy->aslice[i][32], 4);}
	for_aslice(i) {phy->aslice[i][36] = cfg->aslice[i][36];}
	for_aslice(i) {phy->aslice[i][37] = cfg->aslice[i][37];}
	for_dslice(i) {copy_reg_range(&cfg->dslice[i][59], &phy->dslice[i][59], 5);}
	for_dslice(i) {
		phy->dslice[i][83] = cfg->dslice[i][83] + 0x00100000;
		phy->dslice[i][84] = cfg->dslice[i][84] + 0x1000;
		phy->dslice[i][85] = cfg->dslice[i][85];
	}
	for_dslice(i) {copy_reg_range(&cfg->dslice[i][87], &phy->dslice[i][87], 4);}
	for_dslice(i) {copy_reg_range(&cfg->dslice[i][80], &phy->dslice[i][80], 2);}
	for_dslice(i) {phy->dslice[i][86] = cfg->dslice[i][86];}
	for_dslice(i) {
		copy_reg_range(&cfg->dslice[i][64], &phy->dslice[i][64], 4);
		clrset32(&phy->dslice[i][68], 0xfffffc00, cfg->dslice[i][68] & 0xfffffc00);
		copy_reg_range(&cfg->dslice[i][69], &phy->dslice[i][69], 11);
	}
	for_dslice(i) {clrset32(&phy->dslice[i][7], 0x03000000, cfg->dslice[i][7] & 0x03000000);}

	apply32_multiple(speed_regs, ARRAY_SIZE(speed_regs), &phy->global[0], 896, SET_BITS32(2, speed));
}

void fast_freq_switch(u8 freqset, u32 freq) {
	grf[GRF_SOC_CON0] = SET_BITS16(3, 7);
	pmu[PMU_NOC_AUTO_ENA] |= 0x180;
	pmu[PMU_BUS_IDLE_REQ] |= 3 << 18;
	while ((pmu[PMU_BUS_IDLE_ST] & (3 << 18)) != (3 << 18)) {puts("waiting for bus idle\n");}
	cic[0] = (SET_BITS16(2, freqset) << 4) | (SET_BITS16(1, 1) << 2) | SET_BITS16(1, 1);
	while (!(cic[CIC_STATUS] & 4)) {puts("waiting for CIC ready\n");}
	if (!setup_pll(cru + CRU_DPLL_CON, freq)) {halt_and_catch_fire();}
	cic[0] = SET_BITS16(1, 1) << 1;
	puts("waiting for CIC finish … ");
	u32 status;
	for (size_t i = 0; i < 100000; i += 1) {
		status = cic[CIC_STATUS];
		if (status & 2) {
			puts("fail");
			halt_and_catch_fire();
		} else if (status & 1) {
			break;
		}
		udelay(1);
	}
	if (!(status & 1)) {
		puts("timeout\n");
		halt_and_catch_fire();
	}
	puts("done\n");
	pmu[PMU_BUS_IDLE_REQ] &= ~((u32)3 << 18);
	while ((pmu[PMU_BUS_IDLE_ST] & (3 << 18)) != 0) {puts("waiting for bus un-idle\n");}
	pmu[PMU_NOC_AUTO_ENA] &= ~(u32)0x180;
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
		clrset32(pctl + 68, PWRUP_SREF_EXIT, cfg->channels[ch].pwrup_sref_exit * PWRUP_SREF_EXIT);
		log("channel %u initialized\n", ch);
	}
	dump_mrs();
	for_channel(ch) {
		const struct channel_config *ch_cfg = &cfg->channels[ch];
		u32 ddrconfig = ch_cfg->ddrconfig, ddrsize = 0;
		u8 address_bits_both = ch_cfg->bw + ch_cfg->col + ch_cfg->bk;
		for_range(cs, 0, 2) {
			if (!(ch_cfg->csmask & (1 << cs))) {continue;}
			u8 address_bits = address_bits_both + ch_cfg->row[cs];
			ddrsize |= 1 << (address_bits - 25) << (16 * cs);
		}
		printf("ch%u ddrconfig %08x ddrsize %08x\n", ch, ddrconfig, ddrsize);
		volatile u32 *msch = msch_base_for(ch);
		msch[MSCH_DDRCONF] = ddrconfig;
		msch[MSCH_DDRSIZE] = ddrsize;
		msch[MSCH_TIMING1] = ch_cfg->timing1;
		msch[MSCH_TIMING2] = ch_cfg->timing2;
		msch[MSCH_TIMING3] = ch_cfg->timing3;
		msch[MSCH_DEV2DEV] = ch_cfg->dev2dev;
		msch[MSCH_DDRMODE] = ch_cfg->ddrmode;
		/*msch[MSCH_AGING] = ch_cfg->aging;*/
		assert(cfg->type == LPDDR4); /* see dram_all_config */
	}
	/* channel stride: 0xc – 128B, 0xd – 256B, 0xe – 512B, 0xf – 4KiB (other values for different capacities */
	apply32v((volatile u32*)0xff33e010, SET_BITS32(5, 0xd));
	for_channel(ch) {
		volatile struct phy_regs *phy = phy_for(ch);
		volatile u32 *pctl = pctl_base_for(ch);
		update_phy_bank(phy, 1, &phy_400mhz, 1);
		lpddr4_set_odt(pctl, pi_base_for(ch), 0, &odt_600mhz);
		if (!(phy_400mhz.dslice[0][86] & 0x0400)) {
			for_dslice(i) {phy->dslice[0][10] &= ~(1 << 16);}
		}
		if (phy_400mhz.dslice[0][84] & (1 << 16)) {
			u32 val = pctl[217];
			if (((val >> 16) & 0x1f) < 8) {
				pctl[217] = (val & 0xff70ffff) | (8 << 16);
			}
		}
		fast_freq_switch(0, 400);
	}
	return 1;
}

static uint64_t splittable64(uint64_t x)
{
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9;
    x ^= x >> 27;
    x *= 0x94d049bb133111eb;
    x ^= x >> 31;
    return x;
}

static _Bool memtest(u64 salt) {
	_Bool res = 1;
	for_range(block, 0, 31) {
		u32 block_start = block * 0x08000000;
		printf("testing %x–%x … ", block_start, block_start + 0x07ffffff);
		volatile u64 *block_ptr = (volatile u64*)block_start;
		for_range(word, !block, 0x01000000) {
			block_ptr[word] = splittable64(salt ^ (word | block << 24));
		}
		for_range(word, !block, 0x01000000) {
			u64 got = block_ptr[word], expected = splittable64(salt ^ (word | block << 24));
			if (unlikely(got != expected)) {
				printf("@%x: expected %zx, got %zx\n", block_start | word << 3, expected, got);
				res = 0;
			}
		}
	}
	return res;
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
	u64 round = 0;
	while (1) {
		memtest(round++ << 29);
	}
}
