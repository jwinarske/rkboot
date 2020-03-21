#include <main.h>
#include <rk3399.h>
#include "rk3399-dmc.h"

const struct phy_layout cfg_layout = {
	.dslice = 0,
	.aslice = NUM_PHY_ASLICE_REGS,
	.global_diff = 128 * 4 - NUM_PHY_DSLICE_REGS + (128 - NUM_PHY_ASLICE_REGS) * 3,
	.ca_offs = NUM_PHY_DSLICE_REGS
}, reg_layout = {
	.dslice = 128,
	.aslice = 128,
	.global_diff = 0,
	.ca_offs = 512
};

static struct dram_cfg init_cfg = {
#include "initcfg.inc.c"
};

static const struct phy_update phy_400mhz = {
#include "phy_cfg2.inc.c"
};

static const struct phy_update phy_800mhz = {
#include "phy_cfg3.inc.c"
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
		debug("reg %u (delta %u) mask %x val %x shift %u\n", (u32)regs[i].reg, delta, mask, val, regs[i].shift);
		clrset32(base + (regs[i].reg - delta), mask << regs[i].shift, val << regs[i].shift);
	}
}

static void set_memory_map_direct(volatile u32 *pctl, volatile u32 *pi, u32 csmask, u32 width_bits, u32 col, u32 bk, u32 row) {
	u32 bk_diff = 3 - bk, col_diff = 12 - col;
	u32 row_diff = 16 - row;
	_Bool reduc = width_bits != 2;
	debug("set_memory_map: pctl@%zx pi@%zx reduc%u col%u row%u bk%u cs%u\n", (u64)pctl, (u64)pi, (u32)reduc, col_diff, row_diff, bk_diff, csmask);
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

	csmask |= csmask << 2;
	apply32v(pi + 41, SET_BITS32(4, csmask) << 24);
}

enum mrrresult {MRR_OK = 0, MRR_ERROR = 1, MRR_TIMEOUT};

enum mrrresult read_mr(volatile u32 *pctl, u8 mr, u8 cs, u32 *out) {
	u32 cmd = (u32)mr | ((u32)cs << 8) | (1 << 16);
	pctl[PCTL_READ_MODEREG] = cmd << 8;
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
#ifdef DEBUG_MSG
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
#endif
}

u32 detect_csmask() {
	u32 mask = 0;
	for_channel(ch) {
		for_range(cs, 0, 2) {
			u32 mr_value;
			if (read_mr(pctl_base_for(ch), 5, cs, &mr_value) != MRR_OK) {continue;}
			if (mr_value) {mask |= 1 << (ch*2) << cs;}
		}
	}
	return mask;
}

void update_phy_bank(volatile struct phy_regs *phy, u32 bank, const struct phy_update *upd, u32 speed) {
	phy->PHY_GLOBAL(896) = (u32)upd->grp_shift01 << 16 | bank << 8;
	phy->PHY_GLOBAL(911) = upd->pll_ctrl;
	apply32v(&phy->PHY_GLOBAL(913), SET_BITS32(1, upd->negedge_pll_switch));
	copy_reg_range(upd->grp_slave_delay, &phy->PHY_GLOBAL(916), 3);
	for_aslice(i) {
		phy->aslice[i][0] = upd->wraddr_shift0123;
		apply32v(&phy->aslice[i][1], SET_BITS32(16, upd->wraddr_shift45));
		copy_reg_range(upd->slave_master_delays, &phy->aslice[i][32], 6);
	}
	for_dslice(i) {
		apply32v(&phy->dslice[i][7], SET_BITS32(2, upd->two_cycle_preamble) << 24);
		copy_reg_range(upd->dslice_update, &phy->dslice[i][59], 9);
		clrset32(&phy->dslice[i][68], 0xfffffc00, upd->dslice_update[9] & 0xfffffc00);
		copy_reg_range(&upd->dslice_update[10], &phy->dslice[i][69], 13);
		/* one word unused (reserved) */
		phy->dslice[i][83] = upd->dslice_update[24] + 0x00100000;
		phy->dslice[i][84] = upd->dslice_update[25] + 0x1000;
		copy_reg_range(&upd->dslice_update[26], &phy->dslice[i][85], 6);
	}

	apply32_multiple(speed_regs, ARRAY_SIZE(speed_regs), &phy->global[0], 896, SET_BITS32(2, speed));
}

void fast_freq_switch(u8 freqset, u32 freq) {
	grf[GRF_SOC_CON0] = SET_BITS16(3, 7);
	pmu[PMU_NOC_AUTO_ENA] |= 0x180;
	pmu[PMU_BUS_IDLE_REQ] |= 3 << 18;
	while ((pmu[PMU_BUS_IDLE_ST] & (3 << 18)) != (3 << 18)) {debugs("waiting for bus idle\n");}
	cic[0] = (SET_BITS16(2, freqset) << 4) | (SET_BITS16(1, 1) << 2) | SET_BITS16(1, 1);
	while (!(cic[CIC_STATUS] & 4)) {debugs("waiting for CIC ready\n");}
	if (!setup_pll(cru + CRU_DPLL_CON, freq)) {halt_and_catch_fire();}
	cic[0] = SET_BITS16(1, 1) << 1;
	debugs("waiting for CIC finish … ");
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
	debugs("done\n");
	pmu[PMU_BUS_IDLE_REQ] &= ~((u32)3 << 18);
	while ((pmu[PMU_BUS_IDLE_ST] & (3 << 18)) != 0) {debugs("waiting for bus un-idle\n");}
	pmu[PMU_NOC_AUTO_ENA] &= ~(u32)0x180;
}

void freq_step(u32 mhz, u32 ctl_freqset, u32 phy_bank, u32 csmask, const struct odt_preset *preset, const struct phy_update *phy_upd) {
	log("switching to %u MHz … ", mhz);
	for_channel(ch) {
		volatile struct phy_regs *phy = phy_for(ch);
		volatile u32 *pctl = pctl_base_for(ch), *pi = pi_base_for(ch);
		update_phy_bank(phy, phy_bank, phy_upd, 1);
		struct odt_settings odt;
		lpddr4_get_odt_settings(&odt, preset);
		set_drive_strength(pctl, &phy->dslice[0][0], &reg_layout, &odt);
		set_phy_io(&phy->dslice[0][0], reg_layout.global_diff, &odt);
		lpddr4_set_odt(pctl, pi, ctl_freqset, preset);
		if (!(phy_upd->dslice_update[86 - 59] & 0x0400)) {
			for_dslice(i) {phy->dslice[i][10] &= ~(1 << 16);}
		}
		if (phy_upd->dslice_update[84 - 59] & (1 << 16)) {
			u32 val = pctl[217];
			if (((val >> 16) & 0x1f) < 8) {
				pctl[217] = (val & 0xff70ffff) | (8 << 16);
			}
		}
	}
	puts("ready … ");
	fast_freq_switch(ctl_freqset, mhz);
	puts("switched … ");
	_Bool training_fail = 0;
	for_channel(ch) {
		volatile struct phy_regs *phy = phy_for(ch);
		volatile u32 *pctl = pctl_base_for(ch), *pi = pi_base_for(ch);
		training_fail |= !train_channel(ch, (csmask >> (ch*2)) & 3, pctl, pi, phy);
	}
	if (!training_fail) {puts("trained.\n");}
}

void configure_phy(volatile struct phy_regs *phy, const struct phy_cfg *cfg) {
	copy_reg_range(&cfg->global[0], &phy->global[0], NUM_PHY_GLOBAL_REGS);
	for_dslice(i) {
		copy_reg_range(&cfg->dslice[0], &phy->dslice[i][0], PHY_CALVL_VREF_DRIVING_SLICE);
		phy->dslice[i][PHY_CALVL_VREF_DRIVING_SLICE] = (i % 2 == 0) << PHY_SHIFT_CALVL_VREF_DRIVING_SLICE | cfg->dslice[PHY_CALVL_VREF_DRIVING_SLICE];
		copy_reg_range(&cfg->dslice[PHY_CALVL_VREF_DRIVING_SLICE + 1], &phy->dslice[i][PHY_CALVL_VREF_DRIVING_SLICE + 1], NUM_PHY_DSLICE_REGS - PHY_CALVL_VREF_DRIVING_SLICE - 1);
	}
	for_aslice(i) {copy_reg_range(&cfg->aslice[i][0], &phy->aslice[i][0], NUM_PHY_ASLICE_REGS);}

	for_dslice(i) {
		phy->dslice[i][83] = cfg->dslice[83] + 0x00100000;
		phy->dslice[i][84] = cfg->dslice[84] + 0x1000;
	}
}

#define PWRUP_SREF_EXIT (1 << 16)
#define START 1

static _Bool try_init(u32 chmask, struct dram_cfg *cfg, u32 mhz) {
	u32 sref_save[MC_NUM_CHANNELS];
	softreset_memory_controller();
	for_channel(ch) {
		if (!((1 << ch) & chmask)) {continue;}
		volatile u32 *pctl = pctl_base_for(ch);
		volatile u32 *pi = pi_base_for(ch);
		volatile struct phy_regs *phy = phy_for(ch);

		copy_reg_range(
			&cfg->regs.pctl[PCTL_DRAM_CLASS + 1],
			pctl + PCTL_DRAM_CLASS + 1,
			NUM_PCTL_REGS - PCTL_DRAM_CLASS - 1
		);
		/* must happen after setting NO_PHY_IND_TRAIN_INT in the transfer above */
		pctl[PCTL_DRAM_CLASS] = cfg->regs.pctl[PCTL_DRAM_CLASS];

		if (chmask == 3 && ch == 1) {
			/* delay ZQ calibration */
			pctl[14] += mhz * 1000;
		}

		copy_reg_range(&cfg->regs.pi[0], pi, NUM_PI_REGS);
		
		const struct phy_cfg *phy_cfg = &cfg->regs.phy;
		for_range(i, 0, 3) {phy->PHY_GLOBAL(910 + i) = phy_cfg->PHY_GLOBAL(910 + i);}

		phy->PHY_GLOBAL(898) = phy_cfg->PHY_GLOBAL(898);
		phy->PHY_GLOBAL(919) = phy_cfg->PHY_GLOBAL(919);

		sref_save[ch] = pctl[68];
		pctl[68] = sref_save[ch] & ~PWRUP_SREF_EXIT;

		apply32v(&phy->PHY_GLOBAL(957), SET_BITS32(2, 1) << 24);

		pi[0] |= START;
		pctl[0] |= START;

		configure_phy(phy, phy_cfg);
		/* improve dqs and dq phase */
		for_dslice(i) {apply32v(&phy->dslice[i][1], SET_BITS32(11, 0x680) << 8);}
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
		clrset32(pctl + 68, PWRUP_SREF_EXIT, sref_save[ch] & PWRUP_SREF_EXIT);
		log("channel %u initialized\n", ch);
	}
	return 1;
}

static _Bool test_mirror(u32 addr, u32 bit) {
	volatile u32 *base = (volatile u32*)(uintptr_t)addr, *mirror = (volatile u32*)(uintptr_t)(addr ^ (1 << bit));
	static const u32 pattern0 = 0, pattern1 = 0xf00f55aa;
	debug("test_mirror(0x%08x,%u)", addr, bit);
	*base = pattern0;
	*mirror = pattern1;
	u32 a = *base;
	u32 b = *mirror;
	if (b != pattern1) {die("mirror test (0x%08x,%u) corrupted\n", addr, bit);}
	if (a == pattern0) {
		return 0;
	} else if (a == pattern1) {
		return 1;
	} else {
		die("mirror test (0x%08x,%u) corrupted\n", addr, bit);
	}
}

static void set_channel_stride(u32 val) {
	/* channel stride: 0xc – 128B, 0xd – 256B, 0xe – 512B, 0xf – 4KiB (other values for different capacities) */
	*(volatile u32*)0xff33e010 = SET_BITS16(5, val) << 10;
}

#define MIRROR_TEST_ADDR 0x100

static void channel_post_init(volatile u32 *pctl, volatile u32 *pi, volatile u32 *msch, const struct msch_config *msch_cfg, u32 width_bits, u32 ch_csmask) {
	if (!ch_csmask) {return;}
	assert(ch_csmask < 4);
	assert(ch_csmask & 1);
	/* set bus width and CS mask */
	assert(width_bits >= 1 && width_bits <= 2);
	printf("bw=%u ", width_bits);
	set_memory_map_direct(pctl, pi, ch_csmask, width_bits, 12, 3, 12);
	msch[MSCH_DDRSIZE] = 4096/32; /* map full range to CS0 */
	msch[MSCH_DDRCONF] = width_bits == 2 ? 0x0303 : 0x0202; /* map for max row size (16K for 32-bit width, 8K for 16-bit width) */
	u32 col_bits = 12; /* max col bits */
	while (test_mirror(MIRROR_TEST_ADDR, col_bits + width_bits - 1)) {
		if (col_bits == 9) {die("rows too small (<9 bits column address)!\n");}
		col_bits -= 1;
	}
	printf("col=%u ", col_bits);

	u32 bank_shift = width_bits + col_bits;
	assert(bank_shift >= 11 && bank_shift <= 14);
	u32 ddrconf = bank_shift - 11;
	printf("ddrconf=%u ", ddrconf);

	u32 bank_bits = 3;
	if (test_mirror(MIRROR_TEST_ADDR, width_bits + 14)) {
		bank_bits = 2;
	}
	printf("bank=%u ", bank_bits);

	u32 row_shift = width_bits + col_bits + bank_bits;
	u32 max_row_bits = 16;
	if (row_shift + 16 > 32) {max_row_bits = 32 - row_shift;}
	set_memory_map_direct(pctl, pi, ch_csmask, width_bits, col_bits, bank_bits, max_row_bits);
	msch[MSCH_DDRCONF] = ddrconf | ddrconf << 8;
	msch[MSCH_DDRSIZE] = 4096/32; /* map full range to CS0 */

	u32 cs0_size;
	u32 cs0_row_bits = max_row_bits;
	udelay(1);
	while (test_mirror(MIRROR_TEST_ADDR, row_shift - 1 + cs0_row_bits)) {
		if (cs0_row_bits == 12) {die("too few CS0 rows (<12 bits row address)!\n");}
		printf(" row mirror");
		cs0_row_bits -= 1;
	}
	printf("cs0row=%u ", cs0_row_bits);
	cs0_row_bits=16;
	/* FIXME: assumes power-of-two row number */
	cs0_size = 1 << (cs0_row_bits + row_shift - 25);

	u32 cs1_size = 0;
	if (ch_csmask & 2) {
		msch[MSCH_DDRSIZE] = cs0_size | (4096/32 - cs0_size) << 8;
		u32 cs1_row_bits = cs0_row_bits;
		u32 test_addr = MIRROR_TEST_ADDR + (cs0_size << 25);
		while (test_mirror(test_addr, row_shift - 1 + cs1_row_bits)) {
			if (cs1_row_bits == 12) {die("too few CS1 rows (<12 bits row address)!\n");}
			cs1_row_bits -= 1;
		}
		printf("cs1row=%u ", cs1_row_bits);
		cs1_size = 1 << (cs1_row_bits + row_shift -  25);
	}
	u32 ddrsize = cs0_size | cs1_size << 8;
	printf("ddrsize=0x%04x\n", ddrsize);
	set_memory_map_direct(pctl, pi, ch_csmask, width_bits, col_bits, bank_bits, cs0_row_bits);
	msch[MSCH_DDRCONF] = ddrconf;
	msch[MSCH_DDRSIZE] = ddrsize;

	msch[MSCH_TIMING1] = msch_cfg->timing1;
	msch[MSCH_TIMING2] = msch_cfg->timing2;
	msch[MSCH_TIMING3] = msch_cfg->timing3;
	msch[MSCH_DEV2DEV] = msch_cfg->dev2dev;
	msch[MSCH_DDRMODE] = msch_cfg->ddrmode;
}

void ddrinit() {
	struct odt_settings odt;
	lpddr4_get_odt_settings(&odt, &odt_50mhz);
	odt.flags |= ODT_SET_RST_DRIVE;
	lpddr4_modify_config(init_cfg.regs.pctl, init_cfg.regs.pi, &init_cfg.regs.phy, &odt);

	log("initializing DRAM%s\n", "");

	if (!setup_pll(cru + CRU_DPLL_CON, 50)) {die("PLL setup failed\n");}
	/* not doing this will make the CPU hang during the DLL bypass step */
	*(volatile u32*)0xff330040 = 0xc000c000;
	if (!try_init(3, &init_cfg, 50)) {halt_and_catch_fire();}

	dump_mrs();
	u32 csmask = detect_csmask();
	debug("csmask: %x\n", csmask);
	for_channel(ch) {
		volatile u32 *msch = msch_base_for(ch), *pctl = pctl_base_for(ch), *pi = pi_base_for(ch);
		u32 ch_csmask = (csmask >> (2*ch)) & 3;
		u32 width_bits = 2;
		set_channel_stride(0x17+ch); /* map only this channel */
		printf("channel %u: ", ch);
		channel_post_init(pctl, pi, msch, &init_cfg.msch, width_bits, ch_csmask);
	}
	freq_step(400, 0, 1, csmask, &odt_600mhz, &phy_400mhz);
	freq_step(800, 1, 0, csmask, &odt_933mhz, &phy_800mhz);
	log("finished.%s\n", "");
	/* 256B interleaving */
	set_channel_stride(0xd);
	for_range(bit, 10, 32) {
		if (test_mirror(MIRROR_TEST_ADDR, bit)) {die("mirroring detected\n");}
	}
}
