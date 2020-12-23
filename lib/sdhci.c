/* SPDX-License-Identifier: CC0-1.0 */
#include <sdhci.h>
#include <sdhci_regs.h>
#include <sdhci_helpers.h>
#include <stdatomic.h>
#include <assert.h>

#include <log.h>
#include <timer.h>
#include <wait_register.h>
#include <iost.h>
#include <mmc.h>


static void write_cxd(u32 *dest, u32 a, u32 b, u32 c, u32 d) {
	dest[0] = d << 8 | c >> 24;
	dest[1] = c << 8 | b >> 24;
	dest[2] = b << 8 | a >> 24;
	dest[3] = a << 8;
}

static u16 get_hostctrl2_hs_mode(u32 khz, _Bool ddr) {
	if (ddr) {
		return khz <= 52000 ? SDHCI_HOSTCTRL2_DDR50
			: SDHCI_HOSTCTRL2_HS400;
	} else {
		return khz <= 25000 ? SDHCI_HOSTCTRL2_SDR12 :
			khz <= 50000 ? SDHCI_HOSTCTRL2_SDR25 :
			khz <= 100000 ? SDHCI_HOSTCTRL2_SDR50
			: SDHCI_HOSTCTRL2_SDR104;
	}
}

static enum iost sdhci_set_clock(struct sdhci_state *st, u32 khz, _Bool ddr) {
	volatile struct sdhci_regs *sdhci = st->regs;
	struct sdhci_phy *phy = st->phy;
	phy->setup(phy, SDHCI_PHY_STOP);
	sdhci->clock_control = 0;
	usleep(10);
	u32 baseclock_mhz = st->caps >> 8 & 0xff;
	u16 clkctrl = SDHCI_CLKCTRL_INTCLK_EN;
	u32 multiplier = 0; // should be st->caps >> 48 & 0xff;
	u32 real_khz;
	/* only use clock multiplier if target frequency high enough */
	if (multiplier && khz >= (multiplier += 1) * baseclock_mhz) {
		u32 div = baseclock_mhz * multiplier * 1000 / khz;
		assert(div > 0 && div <= 0x400);
		clkctrl |= SDHCI_CLKCTRL_MULT_EN | SDHCI_CLKCTRL_DIV(div - 1);
		real_khz = baseclock_mhz * multiplier * 1000 / div;
	} else if (khz >= baseclock_mhz * 1000) {
		clkctrl |= SDHCI_CLKCTRL_DIV(0);
		real_khz = baseclock_mhz * 1000;
	} else {
		u32 div = (baseclock_mhz * 1000 + khz - 1) / khz;
		assert(div < 0x7ff);
		if (div != 1) {
			clkctrl |= SDHCI_CLKCTRL_DIV((div + 1) >> 1);
		}
		real_khz = baseclock_mhz * 1000 / ((div + 1) & 0x7fe);
	}
	debug("clkctrl: 0x%04"PRIx16" ⇒ %"PRIu32" kHz\n", clkctrl, real_khz);
	sdhci->clock_control = clkctrl;
	st->clock_khz = real_khz;
	st->ddr_active = ddr;
	if (real_khz > 20000) {
		sdhci->host_control1 |= SDHCI_HOSTCTRL1_HIGH_SPEED_MODE;
		u16 timing_mode = get_hostctrl2_hs_mode(real_khz, ddr);
		u16 host_control2 = sdhci->host_control2;
		sdhci->host_control2 = (host_control2 & ~SDHCI_HOSTCTRL2_UHS_MASK) | timing_mode;
	} else {
		sdhci->host_control1 &= ~SDHCI_HOSTCTRL1_HIGH_SPEED_MODE;
	}
	if (!wait_u16_set(&sdhci->clock_control, SDHCI_CLKCTRL_INTCLK_STABLE, USECS(100), "SDHCI internal clock")) {return IOST_GLOBAL;}
	if (!phy->setup(phy, SDHCI_PHY_START)) {return IOST_GLOBAL;}
	if (!phy->lock_freq(phy, khz)) {return IOST_GLOBAL;}
	sdhci->clock_control = clkctrl |= SDHCI_CLKCTRL_SDCLK_EN;
	return IOST_OK;
}

static enum iost sdhci_read_ext_csd(volatile struct sdhci_regs *sdhci, struct sdhci_state *st, struct mmc_cardinfo *card) {
	sdhci->block_count = 1;
	sdhci->transfer_mode = SDHCI_TRANSMOD_READ;
	if (IOST_OK != sdhci_submit_cmd(st, SDHCI_CMD(8) | SDHCI_R1 | SDHCI_CMD_DATA, 0)) {return IOST_LOCAL;}
	if (IOST_OK != sdhci_wait_state(st, SDHCI_PRESTS_READ_READY, SDHCI_PRESTS_READ_READY, USECS(10000), "CMD8")) {return IOST_LOCAL;}
	sdhci->int_st = SDHCI_INT_BUFFER_READ_READY;
	sdhci->int_signal_enable = 0xfffff0ff;
	for_range(i, 0, 32) {
		debug("%3"PRIu32":", i * 16);
		for_range(j, 0, 4) {
			u32 val = sdhci->fifo;
			debug(" %08"PRIx32, val);
			u8 *p = card->ext_csd + 16 * i + 4 * j;
			p[0] = val & 0xff;
			p[1] = val >> 8 & 0xff;
			p[2] = val >> 16 & 0xff;
			p[3] = val >> 24;
		}
		debugs("\n");
	}
	if (IOST_OK != sdhci_wait_state(st, SDHCI_PRESTS_CMD_INHIBIT | SDHCI_PRESTS_DAT_INHIBIT, 0, USECS(10000), "CMD8")) {return IOST_LOCAL;}
	return IOST_OK;
}

static _Bool execute_training(struct sdhci_state *st) {
	volatile struct sdhci_regs *sdhci = st->regs;
	_Bool trained = 0;
	sdhci->int_signal_enable = 0;
	sdhci->block_size = 128;
	sdhci->transfer_mode = SDHCI_TRANSMOD_READ;
	u16 hs_mode = get_hostctrl2_hs_mode(st->clock_khz, st->ddr_active);
	for_range(retries, 0, 10) {
		sdhci->host_control2 = SDHCI_HOSTCTRL2_EXECUTE_TUNING | hs_mode;
		u16 hostctrl2 = sdhci->host_control2;
		info("hostctrl2: %"PRIx16"\n", hostctrl2);
		do {
			while (sdhci->present_state & SDHCI_PRESTS_CMD_INHIBIT) {sched_yield();}
			sdhci->cmd = SDHCI_CMD(21) | SDHCI_R1 | SDHCI_CMD_DATA;
			u32 int_st;
			timestamp_t start = get_timestamp();
			while (~(int_st = sdhci->int_st) & SDHCI_INT_BUFFER_READ_READY) {
				if (~(hostctrl2 = sdhci->host_control2) & SDHCI_HOSTCTRL2_EXECUTE_TUNING) {break;}
				if (get_timestamp() - start > MSECS(150)) {break;}
				sched_yield();
			}
			sdhci->int_st = SDHCI_INT_BUFFER_READ_READY;
		} while ((hostctrl2 = sdhci->host_control2) & SDHCI_HOSTCTRL2_EXECUTE_TUNING);
		info("hostctrl2: %"PRIx8"\n", hostctrl2);
		if (hostctrl2 & SDHCI_HOSTCTRL2_CLOCK_TUNED) {
			trained = 1;
			break;
		}
	}
	sdhci->block_size = 512;
	sdhci->int_signal_enable = 0xfffff0ff;
	info("int_st %04"PRIx32"\n", sdhci->int_st);
	return trained;
}

static void print_r1(const char *prefix, u32 r1, const char *suffix) {
	static const char state_names[NUM_MMC_STATE][8] = {
#define X(name) #name,
		DEFINE_MMC_STATE
#undef X
	};
	u32 state = r1 >> 9 & 15;
	printf("%s%08"PRIx32" %s  ", prefix, r1, state < NUM_MMC_STATE ? state_names[state] : "unknown state");
	static const struct {
		u8 bit;
		char name[15];
	} bit_names[NUM_MMC_R1_POS] = {
#define X(name, bit) {bit, #name},
		DEFINE_MMC_R1
#undef X
	};
	for_array(i, bit_names) {
		if (r1 & 1 << bit_names[i].bit) {
			printf("%s ", bit_names[i].name);
		}
	}
	infos(suffix);
}

static enum iost switch_timing(struct sdhci_state *st, u32 switch_arg, u32 khz, _Bool ddr, timestamp_t cmd6_timeout) {
	volatile struct sdhci_regs *sdhci = st->regs;
	enum iost res = sdhci_submit_cmd(st, SDHCI_CMD(6) | SDHCI_R1b, switch_arg);
	if (res != IOST_OK) {return IOST_LOCAL;}
	res = sdhci_wait_state(st,
		SDHCI_PRESTS_CMD_INHIBIT | SDHCI_PRESTS_DAT_INHIBIT, 0,
		cmd6_timeout, "CMD6"
	);
	if (res != IOST_OK) {return IOST_LOCAL;}
	uint32_t r1 = sdhci->resp[0];
	print_r1("CMD6 response: ", r1, "\n");

	res = sdhci_set_clock(st, khz, ddr);
	if (res != IOST_OK) {return IOST_LOCAL;}

	res = sdhci_submit_cmd(st, SDHCI_CMD(13) | SDHCI_R1, 2 << 16);
	if (res != IOST_OK) {return IOST_LOCAL;}
	res = sdhci_wait_state(st,
		SDHCI_PRESTS_CMD_INHIBIT | SDHCI_PRESTS_DAT_INHIBIT, 0,
		cmd6_timeout, "CMD13"
	);
	if (res != IOST_OK) {return IOST_LOCAL;}
	print_r1("card status: ", sdhci->resp[0], "\n");
	if (r1 & MMC_R1_SWITCH_ERR) {return IOST_INVALID;}
	return IOST_OK;
}

static u32 sw_arg_timing(enum extcsd_hs_timing timing) {
	return MMC_SWITCH_SET_BYTE(EXTCSD_HS_TIMING, timing);
}

static u32 sw_arg_buswidth(enum extcsd_bus_width buswidth) {
	return MMC_SWITCH_SET_BYTE(EXTCSD_BUS_WIDTH, buswidth);
}

static enum iost sdhci_try_higher_speeds(struct sdhci_state *st, struct mmc_cardinfo *card) {
	volatile struct sdhci_regs *sdhci = st->regs;
	enum iost res;
	timestamp_t cmd6_timeout = USECS(100000);
	/* don't try switching to other speed modes if we don't understand the (ext) CSD */
	if (!mmc_cardinfo_understood(card)) {return IOST_OK;}
	if (card->ext_csd[EXTCSD_REV] >= 6) {
		cmd6_timeout = USECS(10000 * card->ext_csd[EXTCSD_GENERIC_CMD6_TIME]);
	}
	u8 card_type = card->ext_csd[EXTCSD_CARD_TYPE];
	info("card type: 0x%02"PRIx8"\n", card_type);
	if (card_type & MMC_CARD_TYPE_HS200_1V8) {
		infos("card supports HS200, trying to enable\n");
		res = switch_timing(st, sw_arg_timing(MMC_TIMING_HS200), 200000, 0, cmd6_timeout);
		if (res != IOST_OK) {return res;}
		if (!execute_training(st)) {
			info("tuning failed, switching back to normal\n");
			res = switch_timing(st, sw_arg_timing(MMC_TIMING_BC), 20000, 0, cmd6_timeout);
			if (res != IOST_OK) {return res;}
		}
	}
	if (st->clock_khz <= 20000 && (card_type & (MMC_CARD_TYPE_HS26 | MMC_CARD_TYPE_HS52))) {
		u32 hs_khz = card_type & MMC_CARD_TYPE_HS52 ? 52000 : 26000;
		infos("card supports HS, trying to enable\n");
		res = switch_timing(st, sw_arg_timing(MMC_TIMING_HS), hs_khz, 0, cmd6_timeout);
		if (res != IOST_OK) {return res;}
		const u8 ddr52_timings = MMC_CARD_TYPE_HS52 | MMC_CARD_TYPE_DDR52_1V8;
		if ((card_type & ddr52_timings) == ddr52_timings) {
			res = switch_timing(st, sw_arg_buswidth(MMC_BUS_WIDTH_DDR8), 52000, 1, cmd6_timeout);
			if (res != IOST_OK) {return res;}
		}
	}
	return sdhci_read_ext_csd(sdhci, st, card);
}

enum iost sdhci_init_late(struct sdhci_state *st, struct mmc_cardinfo *card) {
	volatile struct sdhci_regs *sdhci = st->regs;
	timestamp_t start = get_timestamp();
	info("[%"PRIuTS"] SDHCI init\n", start);
	st->version = sdhci->sdhci_version;
	st->caps = (u64)sdhci->capabilities[1] << 32 | sdhci->capabilities[0];
	assert(st->version >= 2);
	u32 ocr;
	enum iost res;
	while (1) {
		if (IOST_OK != (res = sdhci_wait_state(st, SDHCI_PRESTS_CMD_INHIBIT, 0, USECS(1000), "CMD1"))) {return res;}
		ocr = sdhci->resp[0];
		printf("OCR: %08"PRIx32"\n", ocr);
		if (ocr & 1 << 31) {break;}
		if (get_timestamp() - start > USECS(1000000)) {
			infos("eMMC init timeout\n");
			return 0;
		}
		usleep(1000);
		if (IOST_OK != (res = sdhci_submit_cmd(st, SDHCI_CMD(1) | SDHCI_R3, 0x40000080))) {return res;}
	}
	card->rocr = ocr;
	if (~ocr & 1 << 30) {
		infos("eMMC does not support sector addressing\n");
		return IOST_INVALID;
	}
	if (IOST_OK != sdhci_submit_cmd(st, SDHCI_CMD(2) | SDHCI_R2, 0)) {return IOST_LOCAL;}
	if (IOST_OK != sdhci_wait_state(st, SDHCI_PRESTS_CMD_INHIBIT, 0, USECS(10000), "CMD2")) {return IOST_LOCAL;}
	write_cxd(card->cxd + 4, sdhci->resp[0], sdhci->resp[1], sdhci->resp[2], sdhci->resp[3]);
	info("CID %08"PRIx32" %08"PRIx32" %08"PRIx32" %08"PRIx32"\n",
		card->cxd[4], card->cxd[5], card->cxd[6], card->cxd[7]
	);
	if (IOST_OK != sdhci_submit_cmd(st, SDHCI_CMD(3) | SDHCI_R1, 2 << 16)) {return IOST_LOCAL;}
	if (IOST_OK != sdhci_wait_state(st, SDHCI_PRESTS_CMD_INHIBIT, 0, USECS(1000), "CMD3")) {return IOST_LOCAL;}
	if (IOST_OK != (res = sdhci_set_clock(st, 20000, 0))) {return res;}
	if (IOST_OK != sdhci_submit_cmd(st, SDHCI_CMD(9) | SDHCI_R2, 2 << 16)) {return IOST_LOCAL;}
	if (IOST_OK != sdhci_wait_state(st, SDHCI_PRESTS_CMD_INHIBIT, 0, USECS(10000), "CMD9")) {return IOST_LOCAL;}
	write_cxd(card->cxd, sdhci->resp[0], sdhci->resp[1], sdhci->resp[2], sdhci->resp[3]);
	info("CSD %08"PRIx32" %08"PRIx32" %08"PRIx32" %08"PRIx32"\n",
		card->cxd[0], card->cxd[1], card->cxd[2], card->cxd[3]
	);
	if (IOST_OK != sdhci_submit_cmd(st, SDHCI_CMD(7) | SDHCI_R1, 2 << 16)) {return IOST_LOCAL;}
	if (IOST_OK != sdhci_submit_cmd(st, SDHCI_CMD(16) | SDHCI_R1, 512)) {return IOST_LOCAL;}
	if (IOST_OK != sdhci_submit_cmd(st, SDHCI_CMD(6) | SDHCI_R1b,
		MMC_SWITCH_SET_BYTE(EXTCSD_BUS_WIDTH, MMC_BUS_WIDTH_8)
	)) {
		return IOST_LOCAL;
	}
	timestamp_t cmd6_timeout = USECS(100000);
	if (IOST_OK != sdhci_wait_state(st, SDHCI_PRESTS_CMD_INHIBIT | SDHCI_PRESTS_DAT_INHIBIT, 0, cmd6_timeout, "CMD6")) {return IOST_LOCAL;}
	sdhci->host_control1 = (
		sdhci->host_control1
		& ~(SDHCI_HOSTCTRL1_BUS_WIDTH_4 | SDHCI_HOSTCTRL1_DMA_MASK)
	) | SDHCI_HOSTCTRL1_BUS_WIDTH_8 | SDHCI_HOSTCTRL1_ADMA2_32;
	sdhci->block_size = 512;
	if (IOST_OK != (res = sdhci_read_ext_csd(sdhci, st, card))) {return res;}
	if (IOST_LOCAL >= (res = sdhci_try_higher_speeds(st, card))) {return res;}
	return IOST_OK;
}

_Bool sdhci_try_abort(struct sdhci_state *st) {
	volatile struct sdhci_regs *sdhci = st->regs;
	if (IOST_OK != sdhci_wait_state(st, SDHCI_PRESTS_CMD_INHIBIT, 0, USECS(10000), "abort transfer")) {return 0;}
	if (~sdhci->present_state & SDHCI_PRESTS_DAT_INHIBIT) {return 1;}
	sdhci->cmd = SDHCI_CMD(12) | SDHCI_CMD_ABORT | SDHCI_R1b;
	if (IOST_OK != sdhci_wait_state(st, SDHCI_PRESTS_CMD_INHIBIT, 0, USECS(10000), "CMD12")) {return 0;}
	sdhci->swreset = SDHCI_SWRST_CMD | SDHCI_SWRST_DAT;
	timestamp_t start = get_timestamp();
	while (sdhci->swreset & (SDHCI_SWRST_CMD | SDHCI_SWRST_DAT)) {
		if (get_timestamp() - start > USECS(10000)) {return 0;}
		sched_yield();
	}
	return 1;
}

_Bool sdhci_reset_xfer(struct sdhci_xfer *xfer) {
	u8 status = atomic_load_explicit(&xfer->status, memory_order_acquire);
	assert(status < NUM_IOST);
	_Bool success = atomic_compare_exchange_strong_explicit(&xfer->status, &status, SDHCI_CREATING, memory_order_release, memory_order_relaxed);
	assert(success);
	if (!success) {return 0;}
	xfer->desc_size = 0;
	xfer->xfer_bytes = 0;
	return 1;
}

_Bool sdhci_add_phys_buffer(struct sdhci_xfer *xfer, phys_addr_t buf, phys_addr_t buf_end) {
	assert(atomic_load_explicit(&xfer->status, memory_order_relaxed) == SDHCI_CREATING);
	assert(buf <= buf_end);
	assert(buf_end <= 0xffffffff);
	if (buf & 3 || buf_end & 3) {return 0;}

	size_t size = xfer->desc_size;
	for (;buf < buf_end; ++size) {
		if (size >= xfer->desc_cap) {return 0;}
		struct sdhci_adma2_desc8 *desc = xfer->desc8 + size;
		desc->addr = (u32)buf;
		u16 seg_size;
		if (buf_end - buf >= 1 << 16) {
			seg_size = 0;
			buf += 1 << 16;
			xfer->xfer_bytes += 1 << 16;
		} else {
			seg_size = buf_end - buf;
			buf = buf_end;
			xfer->xfer_bytes += seg_size;
		}
		u32 cmd = (u32)seg_size << 16 | SDHCI_DESC_TRAN;
		if (size) {cmd |= SDHCI_DESC_VALID;}
		spew("desc %08"PRIx32" %08"PRIx32"\n", cmd, desc->addr);
		atomic_store_explicit(&desc->cmd, cmd, memory_order_relaxed);
	}
	xfer->desc_size = size;
	return 1;
}

enum iost sdhci_start_xfer(struct sdhci_state *st, struct sdhci_xfer *xfer, u32 addr) {
	assert(atomic_load_explicit(&xfer->status, memory_order_relaxed) == SDHCI_CREATING);
	if (!xfer->desc_size) {return IOST_INVALID;}
	struct sdhci_adma2_desc8 *desc = xfer->desc8 + xfer->desc_size - 1;
	u32 cmd = atomic_load_explicit(&desc->cmd, memory_order_relaxed);
	atomic_store_explicit(&desc->cmd, cmd | SDHCI_DESC_END, memory_order_relaxed);
	cmd = atomic_load_explicit(&xfer->desc8->cmd, memory_order_relaxed);
	atomic_store_explicit(&xfer->desc8->cmd, cmd | SDHCI_DESC_VALID, memory_order_release);
	atomic_store_explicit(&xfer->status, SDHCI_SUBMITTED, memory_order_release);

	struct sdhci_xfer *dummy = 0;
	assert(xfer->xfer_bytes % 512 == 0);
	_Bool success = atomic_compare_exchange_strong_explicit(&st->active_xfer, &dummy, xfer, memory_order_acq_rel, memory_order_relaxed);
	if (!success) {return IOST_TRANSIENT;}
	volatile struct sdhci_regs *sdhci = st->regs;
	sdhci->adma_addr[0] = xfer->desc_addr;
	sdhci->adma_addr[1] = (u64)xfer->desc_addr >> 32;
	sdhci->block_count = xfer->xfer_bytes / 512;
	sdhci->arg2 = xfer->xfer_bytes / 512;
	sdhci->transfer_mode = SDHCI_TRANSMOD_READ
		| SDHCI_TRANSMOD_BLOCK_COUNT
		| SDHCI_TRANSMOD_MULTIBLOCK
		| SDHCI_TRANSMOD_AUTO_CMD23
		| SDHCI_TRANSMOD_DMA;
	return sdhci_submit_cmd(st, SDHCI_CMD(18) | SDHCI_R1 | SDHCI_CMD_DATA, addr);
}

enum iost sdhci_wait_xfer(struct sdhci_state *st, struct sdhci_xfer *xfer) {
	u8 status;
	while ((status = atomic_load_explicit(&xfer->status, memory_order_acquire)) >= NUM_IOST) {
		assert(status == SDHCI_SUBMITTED);
		call_cc_ptr2_int2(sched_finish_u8, &xfer->status, &st->interrupt_waiters, 0xff, status);
		spews("sdhci_wait_xfer: woke up");
	}
	return status;
}
