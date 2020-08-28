/* SPDX-License-Identifier: CC0-1.0 */
#include <dwmmc.h>
#include <dwmmc_helpers.h>
#include <dwmmc_dma.h>
#include <inttypes.h>
#include <assert.h>
#include <stdatomic.h>

#include <die.h>
#include <timer.h>
#include <sd.h>
#include <runqueue.h>

/* WARNING: does not read from the FIFO, so only use for short results that are read later */
static _Bool wait_data_finished(volatile struct dwmmc_regs *dwmmc, u64 usecs) {
	u32 status;
	timestamp_t start = get_timestamp();
	while (~(status = dwmmc->rintsts) & DWMMC_INT_DATA_TRANSFER_OVER) {
		if (status & DWMMC_ERROR_INT_MASK) {
			return 0;
		}
		if (get_timestamp() - start > usecs * TICKS_PER_MICROSECOND) {
			return 0;
		}
	}
	dwmmc_print_status(dwmmc, "xfer done ");
	dwmmc->rintsts = DWMMC_INT_DATA_TRANSFER_OVER;
	return 1;
}

static _Bool try_high_speed(volatile struct dwmmc_regs *dwmmc, struct dwmmc_signal_services *svc) {
	dwmmc->blksiz = 64;
	dwmmc->bytcnt = 64;
	enum dwmmc_status st = dwmmc_wait_cmd_done(
		dwmmc, 6 | DWMMC_R1 | DWMMC_CMD_DATA_EXPECTED, 0x00ffffff, 1000
	);
	dwmmc_print_status(dwmmc, "CMD6 check ");
	if (st != DWMMC_ST_OK) {return 0;}
	if (!wait_data_finished(dwmmc, 100000)) {
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
		st = dwmmc_wait_cmd_done(dwmmc, 6 | DWMMC_R1 | DWMMC_CMD_DATA_EXPECTED, 0x80fffff1, 1000);
		dwmmc_print_status(dwmmc, "CMD6 commit ");
		if (st != DWMMC_ST_OK) {return 0;}
		if (!wait_data_finished(dwmmc, 100000)) {
			dwmmc_print_status(dwmmc, "CMD6 read failure");
			return 0;
		}
		for (u32 i = 0; i < 16; i += 4) {
			u32 a = dwmmc->fifo, b = dwmmc->fifo, c = dwmmc->fifo, d = dwmmc->fifo;
			info("CMD6 %2"PRIu32": 0x%08"PRIx32" %08"PRIx32" %08"PRIx32" %08"PRIx32"\n", i, a, b, c, d);
		}
		timestamp_t timeout = 1000 * TICKS_PER_MICROSECOND;
		if (dwmmc_wait_not_busy(dwmmc, timeout) > timeout) {return 0;}
		if (!set_clock_enable(dwmmc, 0)) {return 0;}
		if (!svc->set_clock(svc, DWMMC_CLOCK_50M)) {
			info("failed to set the clock for high-speed\n");
			return 0;
		}
		return set_clock_enable(dwmmc, 1);
	}
	return 1;	/* non-support is not a failure */
}

/* assumes that the bus is at 400kHz, with ACMD41 having been issued last (infers further info from the command registers) */
_Bool dwmmc_init_late(volatile struct dwmmc_regs *dwmmc, struct dwmmc_signal_services *svc, struct sd_cardinfo *card) {
	assert((~svc->frequencies_supported & (1 << DWMMC_CLOCK_400K | 1 << DWMMC_CLOCK_25M)) == 0);
	assert(svc->voltages_supported & 1 << DWMMC_SIGNAL_3V3);

	u32 last_cmd = dwmmc->cmd;
	assert((last_cmd & 63) == 41);
	u32 acmd41_arg = dwmmc->cmdarg;
	_Bool sd_2_0 = !!(acmd41_arg & SD_OCR_HIGH_CAPACITY);
	dwmmc_wait_cmd_done_postissue(dwmmc, 1000);
	enum dwmmc_status st;
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
			st = dwmmc_wait_cmd_done(dwmmc, 55 | DWMMC_R1, 0, 1000);
			if (st != DWMMC_ST_OK) {
				dwmmc_print_status(dwmmc, "ACMD41 ");
				return 0;
			}
			st = dwmmc_wait_cmd_done(dwmmc, 41 | DWMMC_R3, acmd41_arg, 1000);
			if (st != DWMMC_ST_OK) {
				dwmmc_print_status(dwmmc, "ACMD41 ");
				return 0;
			}
		}
	}
	dwmmc_print_status(dwmmc, "ACMD41 ");
	_Bool high_capacity = !!(card->rocr & SD_OCR_HIGH_CAPACITY);
	assert_msg(sd_2_0 || !high_capacity, "conflicting info about card capacity");
	st = dwmmc_wait_cmd_done(dwmmc, 2 | DWMMC_R2, 0, 1000);
	if (st != DWMMC_ST_OK) {
		dwmmc_print_status(dwmmc, "CMD2 (ALL_SEND_CID) ");
		return 0;
	}
	for_array(i, card->cid) {card->cid[i] = dwmmc->resp[i];}
	sd_dump_cid(card);
	st = dwmmc_wait_cmd_done(dwmmc, 3 | DWMMC_R6, 0, 1000);
	dwmmc_print_status(dwmmc, "CMD3 (send RCA) ");
	if (st != DWMMC_ST_OK) {return 0;}
	card->rca = dwmmc->resp[0];
	card->rca &= 0xffff0000;
	info("RCA: %08"PRIx32"\n", card->rca);

	timestamp_t timeout = 1000 * TICKS_PER_MICROSECOND;
	if (dwmmc_wait_not_busy(dwmmc, timeout) > timeout
		|| !set_clock_enable(dwmmc, 0)
		|| !svc->set_clock(svc, DWMMC_CLOCK_25M)
		|| !set_clock_enable(dwmmc, 1)
	) {return 0;}

	st = dwmmc_wait_cmd_done(dwmmc, 9 | DWMMC_R2, card->rca, 1000);
	if (st != DWMMC_ST_OK) {
		dwmmc_print_status(dwmmc, "CMD9 (SEND_CSD) ");
		return 0;
	}
	for_array(i, card->csd) {card->csd[i] = dwmmc->resp[i];}
	sd_dump_csd(card);

	st = dwmmc_wait_cmd_done(dwmmc, 7 | DWMMC_R1, card->rca, 1000);
	if (st != DWMMC_ST_OK) {
		dwmmc_print_status(dwmmc, "CMD7 (select card) ");
		return 0;
	}

	st = dwmmc_wait_cmd_done(dwmmc, 16 | DWMMC_R1, 512, 1000);
	dwmmc_print_status(dwmmc, "CMD16 (set blocklen) ");
	if (st != DWMMC_ST_OK) {return 0;}

	st = dwmmc_wait_cmd_done(dwmmc, 55 | DWMMC_R1, card->rca, 1000);
	if (st != DWMMC_ST_OK) {
		dwmmc_print_status(dwmmc, "ACMD6 (set bus width) ");
		return 0;
	}
	st = dwmmc_wait_cmd_done(dwmmc, 6 | DWMMC_R1, 2, 1000);
	dwmmc_print_status(dwmmc, "ACMD6 (set bus width) ");
	if (st != DWMMC_ST_OK) {return 0;}
	dwmmc->ctype = 1;

	if (svc->frequencies_supported & 1 << DWMMC_CLOCK_50M) {
		if (!try_high_speed(dwmmc, svc)) {return 0;}
	}

	st = dwmmc_wait_cmd_done(dwmmc, 55 | DWMMC_R1, card->rca, 1000);
	if (st != DWMMC_ST_OK) {
		dwmmc_print_status(dwmmc, "ACMD13 (SD_STATUS) ");
		return 0;
	}
	dwmmc->blksiz = 64;
	dwmmc->bytcnt = 64;
	st = dwmmc_wait_cmd_done(dwmmc, 13 | DWMMC_R1 | DWMMC_CMD_DATA_EXPECTED, 0, 1000);
	dwmmc_print_status(dwmmc, "ACMD13 (SD_STATUS) ");
	if (st != DWMMC_ST_OK) {return 0;}
	if (!wait_data_finished(dwmmc, 1000)) {
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

static void read_command(volatile struct dwmmc_regs *dwmmc, u32 sector, size_t total_bytes) {
	assert(total_bytes % 512 == 0);
	dwmmc->blksiz = 512;
	dwmmc->bytcnt = total_bytes;
	enum dwmmc_status st = dwmmc_wait_cmd_done(dwmmc, 18 | DWMMC_R1 | DWMMC_CMD_DATA_EXPECTED, sector, 1000);
	dwmmc_check_ok_status(dwmmc, st, "CMD18 (READ_MULTIPLE_BLOCK)");
}

void dwmmc_read_poll(volatile struct dwmmc_regs *dwmmc, u32 sector, void *buf, size_t total_bytes) {
	read_command(dwmmc, sector, total_bytes);
	size_t pos = 0;
	while (1) {
		u32 status = dwmmc->status, intstatus = dwmmc->rintsts;
		assert((intstatus & DWMMC_ERROR_INT_MASK) == 0);
		u32 fifo_items = status >> 17 & 0x1fff;
		for_range(i, 0, fifo_items) {
			u32 val = *(volatile u32*)0xfe320200;
			spew("%3zu: 0x%08"PRIx32"\n", pos, val);
			*(u32 *)(buf + pos) = val;
			pos += 4;
		}
		u32 ack = 0;
		if (intstatus & DWMMC_INT_CMD_DONE) {
#ifdef SPEW_MSG
			dwmmc_print_status(dwmmc, "poll ");
#endif
			ack |= DWMMC_INT_CMD_DONE;
		}
		if (intstatus & DWMMC_INT_DATA_TRANSFER_OVER) {
			ack |= DWMMC_INT_DATA_TRANSFER_OVER;
		}
		dwmmc->rintsts = ack;
		if (!fifo_items) {
			if (pos >= total_bytes) {break;}
			usleep(100);
			continue;
		}
	}
}

void dwmmc_setup_dma(volatile struct dwmmc_regs *dwmmc) {
	dwmmc->bmod = DWMMC_BMOD_SOFT_RESET;
	dwmmc->ctrl |= DWMMC_CTRL_USE_IDMAC | DWMMC_CTRL_DMA_RESET;
	while (dwmmc->ctrl & DWMMC_CTRL_DMA_RESET) {__asm__("yield");}
	while (dwmmc->bmod & DWMMC_BMOD_SOFT_RESET) {__asm__("yield");}
	dwmmc->bmod = DWMMC_BMOD_IDMAC_ENABLE | 12 << 2;
	dwmmc->idmac_int_enable = DWMMC_IDMAC_INTMASK_ALL;
}

void dwmmc_init_dma_state(struct dwmmc_dma_state *state) {
	state->desc_completed = state->desc_written = 0;

	for_array(i, state->desc) {
		state->desc[i].desc.control = 0;
		__asm__ volatile("dc cvac, %0": : "r"(&state->desc[i]) : "memory");
	}
}

void dwmmc_handle_dma_interrupt(volatile struct dwmmc_regs *dwmmc, struct dwmmc_dma_state *state) {
	u32 status = dwmmc->idmac_status;
	u32 desc_written = state->desc_written, desc_completed = state->desc_completed;
	/* mutual exclusion should be provided by the interrupt controller, only need to handle synchronization */
	u32 finished = atomic_load_explicit(&state->finished, memory_order_acquire);
	u32 buf = state->buf;
	u32 end = state->end;
	debugs("?");
	spew("idmac %"PRIu32"/%"PRIu32" %"PRIx32"/%"PRIx32" status=0x%"PRIx32" cur_desc=0x%08"PRIx32" cur_buf=0x%08"PRIx32"\n", desc_completed, desc_written, finished, buf, status, dwmmc->cur_desc_addr, dwmmc->cur_buf_addr);
	if (status & DWMMC_IDMAC_INT_RECEIVE) {
		dwmmc->idmac_status = DWMMC_IDMAC_INT_NORMAL | DWMMC_IDMAC_INT_RECEIVE;
	}
	while (desc_completed < desc_written) {
		__asm__ volatile("dc ivac, %0;dmb sy": : "r"(state->desc + desc_completed % ARRAY_SIZE(state->desc)) : "memory");
		u32 idx = desc_completed % ARRAY_SIZE(state->desc);
		struct dwmmc_idmac_desc *desc = &state->desc[idx].desc;
		if (desc->control & DWMMC_DES_OWN) {break;}
		u32 sizes = desc->sizes;
		finished += (sizes & 0x1fff) + (sizes >> 13 & 0x1fff);
		desc_completed += 1;
		debugs(".");
		for (u64 addr = desc->ptr1, end = addr + (sizes & 0x1fff); addr < end; addr += 64) {
			__asm__ volatile("dc ivac, %0" : : "r"(addr));
		}
	}
	state->desc_completed = desc_completed;
	while (buf < end && desc_completed + ARRAY_SIZE(state->desc) > desc_written) {
		size_t bytes_left = end - buf;
		u32 dma_size = bytes_left < 4096 ? bytes_left : 4096;
		u32 idx = desc_written % ARRAY_SIZE(state->desc);
		struct dwmmc_idmac_desc *desc = &state->desc[idx].desc;
		desc->sizes = dma_size;
		desc->ptr1 = (u32)(uintptr_t)buf;
		desc->ptr2 = 0;
		u32 first_flag = !desc_written ? DWMMC_DES_FIRST : 0;
		u32 last_flag = buf == end ? DWMMC_DES_LAST : 0;
		u32 ring_end_flag = idx == ARRAY_SIZE(state->desc) - 1 ? DWMMC_DES_END_OF_RING : 0;
		u32 control = first_flag | last_flag | ring_end_flag | DWMMC_DES_OWN;
		debugs("=");
		spew("writing descriptor@%"PRIx64": %08"PRIx32" %08"PRIx32"\n", (u64)desc, control, buf);
		dsb_st();
		desc->control = control;
		first_flag = 0;
		buf += dma_size;
		desc_written += 1;
		__asm__ volatile("dmb sy;dc civac, %0": : "r"(desc) : "memory");
	}
	state->desc_written = desc_written;
	state->buf = buf;
	atomic_store_explicit(&state->finished, finished, memory_order_release);
	sched_queue_list(CURRENT_RUNQUEUE, &state->waiters);
	if (status & DWMMC_IDMAC_INT_DESC_UNAVAILABLE) {
		dwmmc->idmac_status = DWMMC_IDMAC_INT_ABNORMAL | DWMMC_IDMAC_INT_DESC_UNAVAILABLE;
		if (buf != end) {
			dwmmc->poll_demand = 1;
		} else {
			debugs("end of transfer reached\n");
		}
	} else {
		assert_eq((status & DWMMC_IDMAC_INTMASK_ABNORMAL), 0, u32, "0x%"PRIx32);
		__asm__("yield");
	}
}

void dwmmc_read_poll_dma(volatile struct dwmmc_regs *dwmmc, u32 sector, void *buf, size_t total_bytes) {
	assert(total_bytes >= 512);
	struct dwmmc_dma_state state;
	dwmmc_init_dma_state(&state);
	assert((uintptr_t)(buf + total_bytes) <= 0xffffffff);
	state.end = (u32)(uintptr_t)(buf + total_bytes);
	state.buf = (u32)(uintptr_t)buf;
	/* single-threaded: don't need synchronization */
	atomic_store_explicit(&state.finished, (u32)(uintptr_t)buf, memory_order_relaxed);
	puts("DMA read\n");
	dwmmc_setup_dma(dwmmc);
	printf("bmod %"PRIx32"\n", dwmmc->bmod);
	dwmmc->desc_list_base = (u32)(uintptr_t)&state.desc;
	read_command(dwmmc, sector, total_bytes);
	while (atomic_load_explicit(&state.finished, memory_order_relaxed) < state.end) {
		dwmmc_handle_dma_interrupt(dwmmc, &state);
	}
}
