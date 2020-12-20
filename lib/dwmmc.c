/* SPDX-License-Identifier: CC0-1.0 */
#include <dwmmc.h>
#include <dwmmc_helpers.h>
#include <inttypes.h>
#include <assert.h>
#include <stdatomic.h>

#include <die.h>
#include <timer.h>
#include <sd.h>
#include <runqueue.h>
#include <async.h>
#include <irq.h>

/* WARNING: does not read from the FIFO, so only use for short results that are read later */
static _Bool wait_data_finished(struct dwmmc_state *state, timestamp_t timeout) {
	u32 status;
	timestamp_t start = get_timestamp();
	while (~(status = atomic_load_explicit(&state->int_st, memory_order_acquire)) & DWMMC_INT_DATA_TRANSFER_OVER) {
		if (status & DWMMC_ERROR_INT_MASK) {
			return 0;
		}
		if (get_timestamp() - start > timeout) {
			return 0;
		}
	}
	dwmmc_print_status(state->regs, "xfer done ");
	return 1;
}

static _Bool try_high_speed(struct dwmmc_state *state, struct dwmmc_signal_services *svc) {
	volatile struct dwmmc_regs *dwmmc = state->regs;
	dwmmc->blksiz = 64;
	dwmmc->bytcnt = 64;
	enum iost st = dwmmc_wait_cmd_done(state,
		6 | DWMMC_R1 | DWMMC_CMD_DATA_EXPECTED, 0x00ffffff,
		USECS(1000)
	);
	dwmmc_print_status(dwmmc, "CMD6 check ");
	if (st != IOST_OK) {return 0;}
	if (!wait_data_finished(state, MSECS(10))) {
		dwmmc_print_status(dwmmc, "CMD6 read failure");
		return 0;
	}
	u32 UNUSED switch0 = dwmmc->fifo;
	u32 UNUSED switch1 = dwmmc->fifo;
	u32 UNUSED switch2 = dwmmc->fifo;
	u32 switch3 = dwmmc->fifo;
	info("CMD6  0: 0x%08"PRIx32" %08"PRIx32" %08"PRIx32" %08"PRIx32"\n", switch0, switch1, switch2, switch3);
	for (u32 i = 4; i < 16; i += 4) {
		u32 a = dwmmc->fifo, b = dwmmc->fifo, c = dwmmc->fifo, d = dwmmc->fifo;
		info("CMD6 %2"PRIu32": 0x%08"PRIx32" %08"PRIx32" %08"PRIx32" %08"PRIx32"\n", i, a, b, c, d);
	}
	if (switch3 & 0x200) {
		infos("switching to high speed\n");
		st = dwmmc_wait_cmd_done(state,
			6 | DWMMC_R1 | DWMMC_CMD_DATA_EXPECTED, 0x80fffff1,
			MSECS(100)
		);
		dwmmc_print_status(dwmmc, "CMD6 commit ");
		if (st != IOST_OK) {return 0;}
		if (!wait_data_finished(state, MSECS(100))) {
			dwmmc_print_status(dwmmc, "CMD6 read failure");
			return 0;
		}
		for (u32 i = 0; i < 16; i += 4) {
			u32 a = dwmmc->fifo, b = dwmmc->fifo, c = dwmmc->fifo, d = dwmmc->fifo;
			info("CMD6 %2"PRIu32": 0x%08"PRIx32" %08"PRIx32" %08"PRIx32" %08"PRIx32"\n", i, a, b, c, d);
		}
		if (!dwmmc_wait_data_idle(state, get_timestamp(), MSECS(3000))) {return 0;}
		if (!set_clock_enable(state, 0)) {return 0;}
		if (!svc->set_clock(svc, DWMMC_CLOCK_50M)) {
			info("failed to set the clock for high-speed\n");
			return 0;
		}
		return set_clock_enable(state, 1);
	}
	return 1;	/* non-support is not a failure */
}

/* assumes that the bus is at 400kHz, with ACMD41 having been issued last (infers further info from the command registers) */
_Bool dwmmc_init_late(struct dwmmc_state *state, struct sd_cardinfo *card) {
	volatile struct dwmmc_regs *dwmmc = state->regs;
	struct dwmmc_signal_services *svc = state->svc;
	assert((~svc->frequencies_supported & (1 << DWMMC_CLOCK_400K | 1 << DWMMC_CLOCK_25M)) == 0);
	assert(svc->voltages_supported & 1 << DWMMC_SIGNAL_3V3);

	u32 last_cmd = dwmmc->cmd;
	assert((last_cmd & 63) == 41);
	u32 acmd41_arg = dwmmc->cmdarg;
	_Bool sd_2_0 = !!(acmd41_arg & SD_OCR_HIGH_CAPACITY);
	dwmmc->ctrl = DWMMC_CTRL_INT_ENABLE;
	enum iost st = dwmmc_wait_cmd_done_postissue(state, get_timestamp(), USECS(1000));
	if (st != IOST_OK) {return 0;}
	{
		timestamp_t start = get_timestamp();
		while (1) {
			card->rocr = dwmmc->resp[0];
			if (card->rocr & SD_RESP_BUSY) {break;}
#ifdef DEBUG_MSG
			dwmmc_print_status(dwmmc, "ACMD41 ");
#endif
			if (get_timestamp() - start > 1000000 * TICKS_PER_MICROSECOND) {
				dwmmc_print_status(dwmmc, "init timeout ");
				return 0;
			}
			usleep(1000);
			st = dwmmc_wait_cmd_done(state, 55 | DWMMC_R1, 0, USECS(1000));
			if (st != IOST_OK) {
				dwmmc_print_status(dwmmc, "ACMD41 ");
				return 0;
			}
			st = dwmmc_wait_cmd_done(state, 41 | DWMMC_R3, acmd41_arg, USECS(1000));
			if (st != IOST_OK) {
				dwmmc_print_status(dwmmc, "ACMD41 ");
				return 0;
			}
		}
	}
	dwmmc_print_status(dwmmc, "ACMD41 ");
	_Bool high_capacity = !!(card->rocr & SD_OCR_HIGH_CAPACITY);
	assert_msg(sd_2_0 || !high_capacity, "conflicting info about card capacity");
	st = dwmmc_wait_cmd_done(state, 2 | DWMMC_R2, 0, USECS(1000));
	if (st != IOST_OK) {
		dwmmc_print_status(dwmmc, "CMD2 (ALL_SEND_CID) ");
		return 0;
	}
	for_array(i, card->cid) {card->cid[i] = dwmmc->resp[i];}
	sd_dump_cid(card);
	st = dwmmc_wait_cmd_done(state, 3 | DWMMC_R6, 0, USECS(1000));
	dwmmc_print_status(dwmmc, "CMD3 (send RCA) ");
	if (st != IOST_OK) {return 0;}
	card->rca = dwmmc->resp[0];
	card->rca &= 0xffff0000;
	info("RCA: %08"PRIx32"\n", card->rca);

	if (!dwmmc_wait_data_idle(state, get_timestamp(), USECS(1000))
		|| !set_clock_enable(state, 0)
		|| !svc->set_clock(svc, DWMMC_CLOCK_25M)
		|| !set_clock_enable(state, 1)
	) {return 0;}

	st = dwmmc_wait_cmd_done(state, 9 | DWMMC_R2, card->rca, USECS(1000));
	if (st != IOST_OK) {
		dwmmc_print_status(dwmmc, "CMD9 (SEND_CSD) ");
		return 0;
	}
	for_array(i, card->csd) {card->csd[i] = dwmmc->resp[i];}
	sd_dump_csd(card);

	st = dwmmc_wait_cmd_done(state, 7 | DWMMC_R1, card->rca, USECS(1000));
	if (st != IOST_OK) {
		dwmmc_print_status(dwmmc, "CMD7 (select card) ");
		return 0;
	}

	st = dwmmc_wait_cmd_done(state, 16 | DWMMC_R1, 512, USECS(1000));
	dwmmc_print_status(dwmmc, "CMD16 (set blocklen) ");
	if (st != IOST_OK) {return 0;}

	st = dwmmc_wait_cmd_done(state, 55 | DWMMC_R1, card->rca, USECS(1000));
	if (st != IOST_OK) {
		dwmmc_print_status(dwmmc, "ACMD6 (set bus width) ");
		return 0;
	}
	st = dwmmc_wait_cmd_done(state, 6 | DWMMC_R1, 2, USECS(1000));
	dwmmc_print_status(dwmmc, "ACMD6 (set bus width) ");
	if (st != IOST_OK) {return 0;}
	dwmmc->ctype = 1;

	if (svc->frequencies_supported & 1 << DWMMC_CLOCK_50M) {
		if (!try_high_speed(state, svc)) {return 0;}
	}

	st = dwmmc_wait_cmd_done(state, 55 | DWMMC_R1, card->rca, USECS(1000));
	if (st != IOST_OK) {
		dwmmc_print_status(dwmmc, "ACMD13 (SD_STATUS) ");
		return 0;
	}
	dwmmc->blksiz = 64;
	dwmmc->bytcnt = 64;
	st = dwmmc_wait_cmd_done(state,
		13 | DWMMC_R1 | DWMMC_CMD_DATA_EXPECTED, 0,
		MSECS(10)
	);
	dwmmc_print_status(dwmmc, "ACMD13 (SD_STATUS) ");
	if (st != IOST_OK) {return 0;}
	if (!wait_data_finished(state, USECS(1000))) {
		dwmmc_print_status(dwmmc, "SSR read failure ");
	}
	for (u32 i = 0; i < 16; i += 4) {
		u32 a = dwmmc->fifo, b = dwmmc->fifo, c = dwmmc->fifo, d = dwmmc->fifo;
		info("SSR %2"PRIu32": 0x%08"PRIx32" %08"PRIx32" %08"PRIx32" %08"PRIx32"\n", i, a, b, c, d);
		card->ssr[i] = a; card->ssr[i + 1] = b; card->ssr[i + 2] = c; card->ssr[i + 3] = d;
	}
	if (!high_capacity) {
		info("card does not support high capacity addressing\n");
		return 0;
	}
	return 1;
}
