/* SPDX-License-Identifier: CC0-1.0 */
#include "ddrinit.h"

#include <stdatomic.h>
#include <inttypes.h>

#include <main.h>
#include <rk3399.h>
#include <mmu.h>
#include <rkpll.h>
#include <runqueue.h>
#include <sched_aarch64.h>
#include <rk3399/sramstage.h>
#include "rk3399-dmc.h"
#include <gic.h>
#include <irq.h>

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
		spew("reg %u (delta %u) mask %x val %x shift %u\n", (u32)regs[i].reg, delta, mask, val, regs[i].shift);
		clrset32(base + (regs[i].reg - delta), mask << regs[i].shift, val << regs[i].shift);
	}
}

static u32 mrr_cmd(u8 mr, u8 cs) {
	return ((u32)mr | ((u32)cs << 8) | (1 << 16)) << 8;
}

enum mrrresult {MRR_OK = 0, MRR_ERROR = 1, MRR_TIMEOUT};

static enum mrrresult UNUSED read_mr(volatile u32 *pctl, u8 mr, u8 cs, u32 *out) {
	pctl[PCTL_READ_MODEREG] = mrr_cmd(mr, cs);
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

static void dump_mrs(volatile u32 UNUSED *pctl) {
#ifdef DEBUG_MSG
	for_range(cs, 0, 2) {
		printf("CS=%u\n", cs);
		for_range(mr, 0, 26) {
			if (!((1 << mr) & 0x30c51f1)) {continue;}
			u32 mr_value; enum mrrresult res;
			if ((res = read_mr(pctl, mr, cs, &mr_value))) {
				if (res == MRR_TIMEOUT) {printf("MRR timeout for mr%u\n", mr);}
				else {printf("MRR error %x for MR%u\n", mr_value, mr);}
			} else {
				printf("MR%u = %x\n", mr, mr_value);
			}
		}
	}
#endif
}

static void update_phy_bank(volatile struct phy_regs *phy, u32 bank, const struct phy_update *upd, u32 speed) {
	phy->PHY_GLOBAL(896) = (u32)upd->grp_shift01 << 16 | bank << 8;
	phy->PHY_GLOBAL(911) = upd->pll_ctrl;
	apply32v(&phy->PHY_GLOBAL(913), SET_BITS32(1, upd->negedge_pll_switch));
	copy_reg_range(upd->grp_slave_delay, &phy->PHY_GLOBAL(916), 3);
	copy_reg_range(upd->adrctl28_44, &phy->PHY_GLOBAL(924), 17);
	for_aslice(i) {
		phy->aslice[i][0] = upd->wraddr_shift0123;
		apply32v(&phy->aslice[i][1], SET_BITS32(16, upd->wraddr_shift45));
		copy_reg_range(upd->slave_master_delays, &phy->aslice[i][32], 6);
	}
	for_dslice(i) {
		copy_reg_range(upd->dslice5_7, &phy->dslice[i][5], 3);
		copy_reg_range(upd->dslice59_90, &phy->dslice[i][59], 9);
		clrset32(&phy->dslice[i][68], 0xfffffc00, upd->dslice59_90[9] & 0xfffffc00);
		copy_reg_range(&upd->dslice59_90[10], &phy->dslice[i][69], 13);
		/* one word unused (reserved) */
		phy->dslice[i][83] = upd->dslice59_90[24] + 0x00100000;
		phy->dslice[i][84] = upd->dslice59_90[25] + 0x1000;
		copy_reg_range(&upd->dslice59_90[26], &phy->dslice[i][85], 6);
	}

	apply32_multiple(speed_regs, ARRAY_SIZE(speed_regs), &phy->global[0], 896, SET_BITS32(2, speed));
}

/* this function seems very prone to system hangs if we try to yield inbetween, probably because of the bus idle. it usually finishes in around 25 μs, so that sholudn't be a problem */
static void fast_freq_switch(u8 freqset, u32 freq) {
	irq_save_t irq = irq_save_mask();	/* just for safety */
	grf[GRF_SOC_CON0] = SET_BITS16(3, 7);
	pmu[PMU_NOC_AUTO_ENA] |= 0x180;
	pmu[PMU_BUS_IDLE_REQ] |= 3 << 18;
	while ((pmu[PMU_BUS_IDLE_ST] & (3 << 18)) != (3 << 18)) {
		debugs("waiting for bus idle\n");
	}
	volatile u32 *cic = regmap_cic;
	cic[0] = (SET_BITS16(2, freqset) << 4) | (SET_BITS16(1, 1) << 2) | SET_BITS16(1, 1);
	while (!(cic[CIC_STATUS] & 4)) {
		debugs("waiting for CIC ready\n");
	}
	rkpll_configure(cru + CRU_DPLL_CON, freq);
	timestamp_t start = get_timestamp();
	while (!rkpll_switch(cru + CRU_DPLL_CON)) {
		if (get_timestamp() - start > USECS(100)) {
			die("failed to lock-on DPLL\n");
		}
	}
	cic[0] = SET_BITS16(1, 1) << 1;
	debugs("waiting for CIC finish … ");
	while (1) {
		u32 status = cic[CIC_STATUS];
		if (status & 2) {
			die("fail");
		} else if (status & 1) {
			break;
		}
	}
	debugs("done\n");
	pmu[PMU_BUS_IDLE_REQ] &= ~((u32)3 << 18);
	while ((pmu[PMU_BUS_IDLE_ST] & (3 << 18)) != 0) {
		debugs("waiting for bus un-idle\n");
	}
	pmu[PMU_NOC_AUTO_ENA] &= ~(u32)0x180;
	irq_restore(irq);
}

static void freq_step(u32 mhz, u32 ctl_freqset, u32 phy_bank, const struct odt_preset *preset, const struct phy_update *phy_upd) {
	log("switching to %u MHz … ", mhz);
	for_channel(ch) {
		volatile struct phy_regs *phy = phy_for(ch);
		volatile u32 *pctl = pctl_base_for(ch);
		update_phy_bank(phy, phy_bank, phy_upd, 1);
		struct odt_settings odt;
		lpddr4_get_odt_settings(&odt, preset);
		u32 wr_en = phy_upd->dslice5_7[0] >> 17 & 1;
		for_aslice(i) {
			clrset32(phy->aslice[i] + 6, 0x0100, wr_en << 8);
		}
		set_phy_io(&phy->dslice[0][0], &odt);
		if (mhz <= 125) {	/* DLL bypass mode, disable slice power reduction */
			for_dslice(i) {phy->dslice[i][10] |= 1 << 16;}
		}
		if (phy_upd->dslice59_90[84 - 59] & (1 << 16)) {
			u32 val = pctl[217]; /* FIXME: should depend on freq set */
			if (((val >> 16) & 0x1f) < 8) {
				pctl[217] = (val & 0xff70ffff) | (8 << 16);
			}
		}
	}
	puts("ready … ");
	timestamp_t start = get_timestamp();
	fast_freq_switch(ctl_freqset, mhz);
	if (mhz > 125) {	/* not DLL bypass mode, enable slice power reduction */
		for_channel(ch) {
			volatile struct phy_regs *phy = phy_for(ch);
			for_dslice(i) {phy->dslice[i][10] &= ~(1 << 16);}
		}
	}
	printf("switched (%"PRIuTS" ticks) … ", get_timestamp() - start);
}

static void configure_phy(volatile struct phy_regs *phy, const struct phy_cfg *cfg) {
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

const char ddrinit_chan_state_names[NUM_CHAN_ST][12] = {
#define X(name) #name,
	DEFINE_CHANNEL_STATES
#undef X
};

#define PWRUP_SREF_EXIT (1 << 16)
#define START 1

static void configure(struct ddrinit_state *st, struct dram_cfg *cfg, u32 mhz) {
	u32 sref_save[MC_NUM_CHANNELS];
	for_channel(ch) {
		if (st->chan_st[ch] != CHAN_ST_UNINIT) {continue;}
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

		if (ch == 1 && st->chan_st[ch] != CHAN_ST_INACTIVE) {
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

		st->chan_st[ch] = CHAN_ST_CONFIGURED;
	}
	for_channel(ch) {
		if (st->chan_st[ch] != CHAN_ST_CONFIGURED) {continue;}

		grf[GRF_DDRC_CON + 2*ch] = SET_BITS16(1, 0) << 8;
		apply32v(&phy_for(ch)->PHY_GLOBAL(957), SET_BITS32(2, 2) << 24);
	}
}

void ddrinit_set_channel_stride(u32 val) {
	/* channel stride: 0xc – 128B, 0xd – 256B, 0xe – 512B, 0xf – 4KiB (other values for different capacities) */
	pmusgrf[PMUSGRF_SOC_CON4] = SET_BITS16(5, val) << 10;
}

void memtest(u64);

void ddrinit_configure(struct ddrinit_state *st) {
	debugs("ddrinit() reached\n");
	struct odt_settings odt;
	lpddr4_get_odt_settings(&odt, &odt_50mhz);
	odt.flags |= ODT_SET_RST_DRIVE;

	softreset_memory_controller();
	logs("initializing DRAM\n");

	/* not doing this will make the CPU hang */
	pmusgrf[PMUSGRF_DDR_RGN_CON + 16] = SET_BITS16(2, 3) << 14;
	udelay(1000);
	/* map memory controller ranges */
	mmu_map_mmio_identity(0xffa80000, 0xffa8ffff);
	dsb_ishst();
	st->chan_st[0] = st->chan_st[1] = CHAN_ST_UNINIT;
	configure(st, &init_cfg, 50);
}

static void set_width(struct sdram_geometry *geo, u32 mr_value, u32 cs) {
	if (!mr_value) {return;}
	geo->csmask |= 1 << cs;
	if (mr_value >> 16) {geo->width = 2;}
}

static void switch_and_train(struct ddrinit_state *st, u32 mhz, u32 ctl_f, u32 phy_f, const struct odt_preset *preset, const struct phy_update *phy_upd) {
	atomic_store_explicit(&st->sync, 0, memory_order_release);
	/* disable DDRC interrupts during switch, since the handler will try to read from registers behind an idled bus */
	gicv2_disable_spi(gic500d, 35);
	gicv2_disable_spi(gic500d, 36);
	gicv2_wait_disabled(gic500d);
	atomic_signal_fence(memory_order_acquire);

	freq_step(mhz, ctl_f, phy_f, preset, phy_upd);
	u32 flags = 0;
	if (phy_upd->dslice59_90[84 - 59] & 1 << 16) {flags |= DDRINIT_PER_CS_TRAINING;}
	st->training_flags = flags;
	for_channel(c) {
		volatile u32 *pctl = pctl_base_for(c);
		pctl[PCTL_INT_ACK]  = 0x24000000;
		pctl[PCTL_INT_MASK] = 0x08002800;
		st->chan_st[c] = CHAN_ST_SWITCHED;
	}
	atomic_signal_fence(memory_order_release);
	gicv2_enable_spi(gic500d, 35);
	gicv2_enable_spi(gic500d, 36);

	ddrinit_train(st);
}

static void both_channels_ready(struct ddrinit_state *st) {
	encode_dram_size(st->geo);
	switch_and_train(st, 400, 0, 1, &odt_600mhz, &phy_400mhz);
	switch_and_train(st, 800, 1, 0, &odt_933mhz, &phy_800mhz);
	rk3399_set_init_flags(RK3399_INIT_DRAM_TRAINING);
	logs("finished.\n");
	/* 256B interleaving */
	ddrinit_set_channel_stride(0xd);
	__asm__ volatile("dsb ish");
	mmu_unmap_range(0xffa80000, 0xffa8ffff);
	for_range(bit, 10, 32) {
		if (test_mirror(MIRROR_TEST_ADDR, bit)) {die("mirroring detected\n");}
	}
	mmu_unmap_range(0, 0xf7ffffff);
	rk3399_set_init_flags(RK3399_INIT_DRAM_READY);
}

void ddrinit_irq(struct ddrinit_state *st, u32 ch) {
	enum channel_state chan_st = st->chan_st[ch];
	assert(chan_st < NUM_CHAN_ST);
	volatile u32 *pctl = pctl_base_for(ch), *pi = pi_base_for(ch);
	volatile struct phy_regs *phy = phy_for(ch);
	u32 int_status = pctl[PCTL_INT_STATUS];
	debug("DDRC%"PRIu32" status=0x%01"PRIx32"%08"PRIx32" chan_st=%s\n", ch, pctl[PCTL_INT_STATUS+1], int_status, ddrinit_chan_state_names[chan_st]);
	switch (chan_st) {
	case CHAN_ST_CONFIGURED:
		if (~int_status & PCTL_INT0_INIT_DONE) {break;}
		pctl[PCTL_INT_ACK] = PCTL_INT0_INIT_DONE;
		grf[GRF_DDRC_CON + 2*ch] = SET_BITS16(1, 1) << 8;
		for_dslice(i) {
			for_range(reg, 53, 58) {phy->dslice[i][reg] = 0x08200820;}
			clrset32(&phy->dslice[i][58], 0xffff, 0x0820);
		}
		if (ch == 1) { /* restore reset drive strength */
			clrset32(&phy->PHY_GLOBAL(937), 0xff, init_cfg.regs.phy.PHY_GLOBAL(937) & 0xff);
		}
		dump_mrs(pctl);
		struct sdram_geometry *geo = st->geo + ch;
		geo->csmask = 0;
		geo->width = 1;
		geo->col = geo->bank = geo->cs0_row = geo->cs1_row = 0;
		pctl[PCTL_READ_MODEREG] = mrr_cmd(5, 0);
		st->chan_st[ch] = CHAN_ST_INIT;
		rk3399_set_init_flags(RK3399_INIT_DDRC0_INIT << ch);
		log("channel %u initialized\n", ch);
		return;
	case CHAN_ST_INIT:
		if (~int_status & PCTL_INT0_MRR_DONE) {break;}
		pctl[PCTL_INT_ACK] = PCTL_INT0_MRR_DONE;
		set_width(st->geo + ch, pctl[PCTL_PERIPHERAL_MRR_DATA], 0);
		pctl[PCTL_READ_MODEREG] = mrr_cmd(5, 1);
		st->chan_st[ch] = CHAN_ST_CS0_MR5;
		return;
	case CHAN_ST_CS0_MR5:
		if (~int_status & PCTL_INT0_MRR_DONE) {break;}
		pctl[PCTL_INT_ACK] = PCTL_INT0_MRR_DONE;
		geo = st->geo + ch;
		set_width(st->geo + ch, pctl[PCTL_PERIPHERAL_MRR_DATA], 1);
		ddrinit_set_channel_stride(0x17+ch); /* map only this channel */
		for_channel(c) {if (st->chan_st[c] >= CHAN_ST_READY) {goto already_mapped;}}
		/* map DRAM region as MMIO; needed for geometry detection; unmapped after training */
		mmu_map_mmio_identity(0, 0xf7ffffff);
		already_mapped:;
		printf("channel %u: ", ch);
		channel_post_init(pctl, pi, msch_base_for(ch), &init_cfg.msch, geo);
		st->chan_st[ch] = CHAN_ST_READY;
		rk3399_set_init_flags(RK3399_INIT_DDRC0_READY << ch);

		for_channel(c) {if (st->chan_st[c] < CHAN_ST_READY) {return;}}
		struct sched_thread_start thread_start = {
			.runnable = {.next = 0, .run = sched_start_thread},
			.pc = (u64)both_channels_ready,
			.pad = 0,
			.args = {(u64)st, },
		}, *runnable = (struct sched_thread_start *)(vstack_base(SRAMSTAGE_VSTACK_DDRC0) - sizeof(struct sched_thread_start));
		*runnable = thread_start;
		sched_queue_single(CURRENT_RUNQUEUE, &runnable->runnable);
		return;
	default: break;
	}
	die("unexpected DDRC%"PRIu32" interrupt: status=0x%01"PRIx32"%08"PRIx32"  chan_st=%s\n", ch, pctl[PCTL_INT_STATUS+1], int_status, ddrinit_chan_state_names[chan_st]);
}
