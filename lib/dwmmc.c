/* SPDX-License-Identifier: CC0-1.0 */
#include <dwmmc.h>
#include <dwmmc_helpers.h>
#include <dwmmc_dma.h>

#include <inttypes.h>
#include <assert.h>

#include <lib.h>
#include <die.h>
#include <plat.h>
#include <sd.h>
#include <runqueue.h>

static const char cmd_status_names[NUM_DWMMC_ST][16] = {
#define X(name) #name,
	DEFINE_DWMMC_ST
#undef X
};

_Bool dwmmc_wait_cmd_inner(volatile struct dwmmc_regs *dwmmc, u32 cmd) {
	spew("starting command %08"PRIx32"\n", cmd);
	dwmmc->cmd = cmd;
	timestamp_t start = get_timestamp();
	while (dwmmc->cmd & DWMMC_CMD_START) {
		if (get_timestamp() - start > 100 * TICKS_PER_MICROSECOND) {
			return 0;
		}
		sched_yield();
	}
	return 1;
}
enum dwmmc_status dwmmc_wait_cmd_done_inner(volatile struct dwmmc_regs *dwmmc, timestamp_t raw_timeout) {
	timestamp_t start = get_timestamp();
	u32 status;
	while (~(status = dwmmc->rintsts) & DWMMC_INT_CMD_DONE) {
		sched_yield();
		if (get_timestamp() - start > raw_timeout) {
			info("timed out waiting for command completion, rintsts=0x%"PRIx32" status=0x%"PRIx32"\n", dwmmc->rintsts, dwmmc->status);
			return DWMMC_ST_CMD_TIMEOUT;
		}
	}
	if (status & DWMMC_ERROR_INT_MASK) {return DWMMC_ST_ERROR;}
	if (status & DWMMC_INT_RESP_TIMEOUT) {return DWMMC_ST_TIMEOUT;}
	dwmmc->rintsts = DWMMC_ERROR_INT_MASK | DWMMC_INT_CMD_DONE | DWMMC_INT_RESP_TIMEOUT;
	return DWMMC_ST_OK;
}

void dwmmc_print_status(volatile struct dwmmc_regs *dwmmc, const char *context) {
	log("%sresp0=0x%08"PRIx32" status=0x%08"PRIx32" rintsts=0x%04"PRIx32"\n", context, dwmmc->resp[0], dwmmc->status, dwmmc->rintsts);
}

timestamp_t dwmmc_wait_not_busy(volatile struct dwmmc_regs *dwmmc, timestamp_t raw_timeout) {
	timestamp_t start = get_timestamp(), cur = start;
	while (dwmmc->status & 1 << 9) {
		sched_yield();
		if ((cur = get_timestamp()) - start > raw_timeout) {
			dwmmc_print_status(dwmmc, "idle timeout");
			return raw_timeout + 1;
		}
	}
	return cur - start;
}

void sd_dump_cid(u32 cid0, u32 cid1, u32 cid2, u32 cid3);

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

static _Bool set_clock_enable(volatile struct dwmmc_regs *dwmmc, _Bool enable) {
	dwmmc->clkena = enable;
	if (!dwmmc_wait_cmd(dwmmc, DWMMC_CMD_UPDATE_CLOCKS | DWMMC_CMD_SYNC_DATA)) {
		info("failed to program CLKENA=%u\n", (unsigned)enable);
		return 0;
	}
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

_Bool dwmmc_init(volatile struct dwmmc_regs *dwmmc, struct dwmmc_signal_services *svc) {
	assert((~svc->frequencies_supported & (1 << DWMMC_CLOCK_400K | 1 << DWMMC_CLOCK_25M)) == 0);
	assert(svc->voltages_supported & 1 << DWMMC_SIGNAL_3V3);
	svc->set_clock(svc, DWMMC_CLOCK_400K);
	dwmmc->pwren = 1;
	usleep(1000);
	dwmmc->rintsts = ~(u32)0;
	dwmmc->intmask = 0;
	dwmmc->fifoth = 0x307f0080;
	dwmmc->tmout = 0xffffffff;
	dwmmc->ctrl = DWMMC_CTRL_CONTROLLER_RESET | DWMMC_CTRL_FIFO_RESET;
	{
		timestamp_t start = get_timestamp();
		while (dwmmc->ctrl & 3) {
			sched_yield();
			if (get_timestamp() - start > 1000 * TICKS_PER_MICROSECOND) {
				info("reset timeout, ctrl=%08"PRIx32"\n", dwmmc->ctrl);
				return 0;
			}
		}
	}
	info("SDMMC reset successful. HCON=%08"PRIx32"\n", dwmmc->hcon);
	dwmmc->rst_n = 1;
	usleep(5);
	dwmmc->rst_n = 0;
	dwmmc->ctype = 0;
	dwmmc->clkdiv[0] = 0;
	dsb_st();
	if (!set_clock_enable(dwmmc, 1)) {return 0;}
	debugs("clock enabled\n");
	enum dwmmc_status st = dwmmc_wait_cmd_done(dwmmc, 0 | DWMMC_CMD_SEND_INITIALIZATION | DWMMC_CMD_CHECK_RESPONSE_CRC, 0, 1000);
	if (st != DWMMC_ST_OK) {
		info("error on CMD0: status=%s\n", cmd_status_names[st]);
		return 0;
	}
	dwmmc_print_status(dwmmc, "CMD0 ");
	st = dwmmc_wait_cmd_done(dwmmc, 8 | DWMMC_R1, 0x1aa, 10000);
	dwmmc_print_status(dwmmc, "CMD8 ");
	if (st == DWMMC_ST_ERROR) {return 0;}
	_Bool sd_2_0 = st != DWMMC_ST_TIMEOUT;
	if (!sd_2_0) {
		st = dwmmc_wait_cmd_done(dwmmc, 0 | DWMMC_CMD_CHECK_RESPONSE_CRC, 0, 1000);
		dwmmc_print_status(dwmmc, "CMD0 ");
		if (st != DWMMC_ST_OK) {return 0;}
	} else {
		if ((dwmmc->resp[0] & 0xff) != 0xaa) {
			 infos("CMD8 check pattern returned incorrectly\n");
			 return 0;
		}
	}
	u32 resp;
	{
		timestamp_t start = get_timestamp();
		while (1) {
			st = dwmmc_wait_cmd_done(dwmmc, 55 | DWMMC_R1, 0, 1000);
			if (st != DWMMC_ST_OK) {
				dwmmc_print_status(dwmmc, "ACMD41 ");
				return 0;
			}
			u32 arg = 0x00ff8000 | (sd_2_0 ? SD_OCR_HIGH_CAPACITY | SD_OCR_XPC | SD_OCR_S18R : 0);
			st = dwmmc_wait_cmd_done(dwmmc, 41 | DWMMC_R3, arg, 1000);
			if (st != DWMMC_ST_OK) {
				dwmmc_print_status(dwmmc, "ACMD41 ");
				return 0;
			}
			resp = dwmmc->resp[0];
			if (resp & SD_RESP_BUSY) {break;}
#ifdef DEBUG_MSG
			dwmmc_print_status(dwmmc, "ACMD41 ");
#endif
			if (get_timestamp() - start > 1000000 * TICKS_PER_MICROSECOND) {
				dwmmc_print_status(dwmmc, "init timeout ");
				return 0;
			}
			usleep(1000);
		}
	}
	dwmmc_print_status(dwmmc, "ACMD41 ");
	_Bool high_capacity = !!(resp & SD_OCR_HIGH_CAPACITY);
	assert_msg(sd_2_0 || !high_capacity, "conflicting info about card capacity");
	st = dwmmc_wait_cmd_done(dwmmc, 2 | DWMMC_R2, 0, 1000);
	if (st != DWMMC_ST_OK) {
		dwmmc_print_status(dwmmc, "CMD2 (ALL_SEND_CID) ");
		return 0;
	}
	sd_dump_cid(dwmmc->resp[0], dwmmc->resp[1], dwmmc->resp[2], dwmmc->resp[3]);
	st = dwmmc_wait_cmd_done(dwmmc, 3 | DWMMC_R6, 0, 1000);
	dwmmc_print_status(dwmmc, "CMD3 (send RCA) ");
	if (st != DWMMC_ST_OK) {return 0;}
	u32 rca = dwmmc->resp[0];
	info("RCA: %08"PRIx32"\n", rca);
	rca &= 0xffff0000;
	st = dwmmc_wait_cmd_done(dwmmc, 7 | DWMMC_R1, rca, 1000);
	if (st != DWMMC_ST_OK) {
		dwmmc_print_status(dwmmc, "CMD7 (select card) ");
		return 0;
	}

	timestamp_t timeout = 1000 * TICKS_PER_MICROSECOND;
	if (dwmmc_wait_not_busy(dwmmc, timeout) > timeout
		|| !set_clock_enable(dwmmc, 0)
		|| !svc->set_clock(svc, DWMMC_CLOCK_25M)
		|| !set_clock_enable(dwmmc, 1)
	) {return 0;}

	st = dwmmc_wait_cmd_done(dwmmc, 16 | DWMMC_R1, 512, 1000);
	dwmmc_print_status(dwmmc, "CMD16 (set blocklen) ");
	if (st != DWMMC_ST_OK) {return 0;}

	st = dwmmc_wait_cmd_done(dwmmc, 55 | DWMMC_R1, rca, 1000);
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

	st = dwmmc_wait_cmd_done(dwmmc, 55 | DWMMC_R1, rca, 1000);
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
			spew("%3"PRIu32": 0x%08"PRIx32"\n", pos, val);
			*(u32 *)(buf + pos) = val;
			pos += 4;
		}
		u32 ack = 0;
		if (intstatus & DWMMC_INT_CMD_DONE) {
#ifdef SPEW_MSG
			dwmmc_print_status(dwmmc);
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
	size_t bytes_transferred = state->bytes_transferred;
	size_t bytes_left = state->bytes_left;
	debugs("?");
	spew("idmac %"PRIu32"/%"PRIu32" %zx/%zx status=0x%"PRIx32" cur_desc=0x%08"PRIx32" cur_buf=0x%08"PRIx32"\n", desc_completed, desc_written, bytes_transferred, bytes_left, status, dwmmc->cur_desc_addr, dwmmc->cur_buf_addr);
	if (status & DWMMC_IDMAC_INT_RECEIVE) {
		dwmmc->idmac_status = DWMMC_IDMAC_INT_NORMAL | DWMMC_IDMAC_INT_RECEIVE;
	}
	while (desc_completed < desc_written) {
		__asm__ volatile("dc ivac, %0;dmb sy": : "r"(state->desc + desc_completed % ARRAY_SIZE(state->desc)) : "memory");
		u32 idx = desc_completed % ARRAY_SIZE(state->desc);
		struct dwmmc_idmac_desc *desc = &state->desc[idx].desc;
		if (desc->control & DWMMC_DES_OWN) {break;}
		u32 sizes = desc->sizes;
		bytes_transferred += (sizes & 0x1fff) + (sizes >> 13 & 0x1fff);
		desc_completed += 1;
		debugs(".");
		for (u64 addr = desc->ptr1, end = addr + (sizes & 0x1fff); addr < end; addr += 64) {
			__asm__ volatile("dc ivac, %0" : : "r"(addr));
		}
	}
	state->desc_completed = desc_completed;
	state->bytes_transferred = bytes_transferred;
	void *buf = state->buf;
	while (bytes_left && desc_completed + ARRAY_SIZE(state->desc) > desc_written) {
		u32 dma_size = bytes_left < 4096 ? bytes_left : 4096;
		bytes_left -= dma_size;
		u32 idx = desc_written % ARRAY_SIZE(state->desc);
		struct dwmmc_idmac_desc *desc = &state->desc[idx].desc;
		desc->sizes = dma_size;
		desc->ptr1 = (u32)buf;
		desc->ptr2 = 0;
		u32 first_flag = !desc_written ? DWMMC_DES_FIRST : 0;
		u32 last_flag = !bytes_left ? DWMMC_DES_LAST : 0;
		u32 ring_end_flag = idx == ARRAY_SIZE(state->desc) - 1 ? DWMMC_DES_END_OF_RING : 0;
		u32 control = first_flag | last_flag | ring_end_flag | DWMMC_DES_OWN;
		debugs("=");
		spew("writing descriptor@%"PRIx64": %08"PRIx32" %08"PRIx32"\n", (u64)desc, control, (u32)buf);
		dsb_st();
		desc->control = control;
		first_flag = 0;
		buf += dma_size;
		desc_written += 1;
		__asm__ volatile("dmb sy;dc civac, %0": : "r"(desc) : "memory");
	}
	state->desc_written = desc_written;
	state->bytes_left = bytes_left;
	state->buf = buf;
	if (status & DWMMC_IDMAC_INT_DESC_UNAVAILABLE) {
		dwmmc->idmac_status = DWMMC_IDMAC_INT_ABNORMAL | DWMMC_IDMAC_INT_DESC_UNAVAILABLE;
		if (bytes_left) {dwmmc->poll_demand = 1;}
	} else {
		assert_eq((status & DWMMC_IDMAC_INTMASK_ABNORMAL), 0, u32, "0x%"PRIx32);
		__asm__("yield");
	}
}

void dwmmc_read_poll_dma(volatile struct dwmmc_regs *dwmmc, u32 sector, void *buf, size_t total_bytes) {
	assert(total_bytes >= 512);
	struct dwmmc_dma_state state;
	dwmmc_init_dma_state(&state);
	state.buf = buf;
	state.bytes_left = total_bytes;
	state.bytes_transferred = 0;
	puts("DMA read\n");
	dwmmc_setup_dma(dwmmc);
	printf("bmod %"PRIx32"\n", dwmmc->bmod);
	dwmmc->desc_list_base = (u32)&state.desc;
	read_command(dwmmc, sector, total_bytes);
	while (state.bytes_transferred < total_bytes) {
		dwmmc_handle_dma_interrupt(dwmmc, &state);
	}
}
