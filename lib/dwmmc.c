/* SPDX-License-Identifier: CC0-1.0 */
#include <dwmmc.h>
#include <lib.h>
#include <die.h>
#include <rk3399.h>
#include <inttypes.h>
#include <sd.h>

void dwmmc_wait_cmd_inner(volatile struct dwmmc_regs *dwmmc, u32 cmd) {
	spew("starting command %08"PRIx32"\n", cmd);
	dwmmc->cmd = cmd;
	timestamp_t start = get_timestamp();
	while (dwmmc->cmd & DWMMC_CMD_START) {
		__asm__("yield");
		if (get_timestamp() - start > 100 * CYCLES_PER_MICROSECOND) {
			die("timed out waiting for CIU to accept command\n");
		}
	}
}
enum dwmmc_status dwmmc_wait_cmd_done_inner(volatile struct dwmmc_regs *dwmmc, timestamp_t raw_timeout) {
	timestamp_t start = get_timestamp();
	u32 status;
	while (~(status = dwmmc->rintsts) & DWMMC_INT_CMD_DONE) {
		__asm__("yield");
		if (get_timestamp() - start > raw_timeout) {
			die("timed out waiting for command completion, rintsts=0x%"PRIx32" status=0x%"PRIx32"\n", dwmmc->rintsts, dwmmc->status);
		}
	}
	if (status & DWMMC_ERROR_INT_MASK) {return DWMMC_ST_ERROR;}
	if (status & DWMMC_INT_RESP_TIMEOUT) {return DWMMC_ST_TIMEOUT;}
	dwmmc->rintsts = DWMMC_ERROR_INT_MASK | DWMMC_INT_CMD_DONE | DWMMC_INT_RESP_TIMEOUT;
	return DWMMC_ST_OK;
}

timestamp_t dwmmc_wait_not_busy(volatile struct dwmmc_regs *dwmmc, timestamp_t raw_timeout) {
	timestamp_t start = get_timestamp(), cur = start;
	while (dwmmc->status & 1 << 9) {
		__asm__("yield");
		if ((cur = get_timestamp()) - start > raw_timeout) {
			die("timed out waiting for card to not be busy\n");
		}
	}
	return cur - start;
}

void dwmmc_print_status(volatile struct dwmmc_regs *dwmmc) {
	info("resp0=0x%08"PRIx32" status=0x%08"PRIx32" rintsts=0x%04"PRIx32"\n", dwmmc->resp[0], dwmmc->status, dwmmc->rintsts);
}

void udelay(u32 usecs);
void sd_dump_cid(u32 cid0, u32 cid1, u32 cid2, u32 cid3);

void dwmmc_init(volatile struct dwmmc_regs *dwmmc) {
	dwmmc->pwren = 1;
	udelay(1000);
	dwmmc->rintsts = ~(u32)0;
	dwmmc->intmask = 0;
	dwmmc->fifoth = 0x307f0080;
	dwmmc->tmout = 0xffffffff;
	dwmmc->ctrl = DWMMC_CTRL_CONTROLLER_RESET | DWMMC_CTRL_FIFO_RESET;
	{
		timestamp_t start = get_timestamp();
		while (dwmmc->ctrl & 3) {
			__asm__("yield");
			if (get_timestamp() - start > 100 * CYCLES_PER_MICROSECOND) {
				die("reset timeout, ctrl=%08"PRIx32"\n", dwmmc->ctrl);
			}
		}
	}
	info("SDMMC reset successful. HCON=%08"PRIx32"\n", dwmmc->hcon);
	dwmmc->rst_n = 1;
	udelay(5);
	dwmmc->rst_n = 0;
	dwmmc->ctype = 0;
	dwmmc->clkdiv[0] = 0;
	dsb_st();
	dwmmc->clkena = 1;
	dwmmc_wait_cmd(dwmmc, DWMMC_CMD_UPDATE_CLOCKS | DWMMC_CMD_SYNC_DATA);
	info("clock enabled\n");
	udelay(500);
	dwmmc_wait_cmd_done(dwmmc, 0 | DWMMC_CMD_SEND_INITIALIZATION | DWMMC_CMD_CHECK_RESPONSE_CRC, 0, 1000);
	udelay(10000);
	info("CMD0 resp=%08"PRIx32" rintsts=%"PRIx32"\n", dwmmc->resp[0], dwmmc->rintsts);
	info("status=%08"PRIx32"\n", dwmmc->status);
	enum dwmmc_status st = dwmmc_wait_cmd_done(dwmmc, 8 | DWMMC_R1, 0x1aa, 100000);
	assert_msg(st != DWMMC_ST_ERROR, "error on SET_IF_COND (CMD8)\n");
	assert_unimpl(st != DWMMC_ST_TIMEOUT, "SD <2.0 cards\n");
	info("CMD8 resp=%08"PRIx32" rintsts=%"PRIx32"\n", dwmmc->resp[0], dwmmc->rintsts);
	assert_msg((dwmmc->resp[0] & 0xff) == 0xaa, "CMD8 check pattern returned incorrectly\n");
	info("status=%08"PRIx32"\n", dwmmc->status);
	u32 resp;
	{
		timestamp_t start = get_timestamp();
		while (1) {
			st = dwmmc_wait_cmd_done(dwmmc, 55 | DWMMC_R1, 0, 1000);
			dwmmc_check_ok_status(dwmmc, st, "CMD55 (APP_CMD)");
			st = dwmmc_wait_cmd_done(dwmmc, 41 | DWMMC_R3, 0x00ff8000 | SD_OCR_HIGH_CAPACITY | SD_OCR_S18R, 1000);
			dwmmc_check_ok_status(dwmmc, st, "ACMD41 (OP_COND)");
			resp = dwmmc->resp[0];
			if (resp & SD_RESP_BUSY) {break;}
			if (get_timestamp() - start > 1000000 * CYCLES_PER_MICROSECOND) {
				die("timeout in initialization\n");
			}
			udelay(100);
		}
	}
	info("resp0=0x%08"PRIx32" status=0x%08"PRIx32"\n", resp, dwmmc->status);
	assert_msg(resp & SD_OCR_HIGH_CAPACITY, "card does not support high capacity addressing\n");
	st = dwmmc_wait_cmd_done(dwmmc, 2 | DWMMC_R2, 0, 1000);
	dwmmc_check_ok_status(dwmmc, st, "CMD2 (ALL_SEND_CID)");
	sd_dump_cid(dwmmc->resp[0], dwmmc->resp[1], dwmmc->resp[2], dwmmc->resp[3]);
	st = dwmmc_wait_cmd_done(dwmmc, 3 | DWMMC_R6, 0, 1000);
	dwmmc_check_ok_status(dwmmc, st, "CMD3 (SEND_RELATIVE_ADDRESS)");
	u32 rca = dwmmc->resp[0];
	info("RCA: %08"PRIx32"\n", rca);
	rca &= 0xffff0000;
	st = dwmmc_wait_cmd_done(dwmmc, 10 | DWMMC_R2, rca, 1000);
	dwmmc_check_ok_status(dwmmc, st, "CMD10 (SEND_CID)");
	sd_dump_cid(dwmmc->resp[0], dwmmc->resp[1], dwmmc->resp[2], dwmmc->resp[3]);
	st = dwmmc_wait_cmd_done(dwmmc, 7 | DWMMC_R1, rca, 1000);
	dwmmc_check_ok_status(dwmmc, st, "CMD7 (SELECT_CARD)");
	dwmmc_wait_not_busy(dwmmc, 1000 * CYCLES_PER_MICROSECOND);
	dwmmc->clkena = 0;
	dwmmc_wait_cmd(dwmmc, DWMMC_CMD_UPDATE_CLOCKS | DWMMC_CMD_SYNC_DATA);
	/* clk_sdmmc = 50 MHz */
	cru[CRU_CLKSEL_CON + 16] = SET_BITS16(3, 0) << 8 | SET_BITS16(7, 19);
	dwmmc->clkena = 1;
	dwmmc_wait_cmd(dwmmc, DWMMC_CMD_UPDATE_CLOCKS | DWMMC_CMD_SYNC_DATA);
	st = dwmmc_wait_cmd_done(dwmmc, 16 | DWMMC_R1, 512, 1000);
	dwmmc_check_ok_status(dwmmc, st, "CMD16 (SET_BLOCKLEN)");
	info("resp0=0x%08"PRIx32" status=0x%08"PRIx32"\n", dwmmc->resp[0], dwmmc->status);
	st = dwmmc_wait_cmd_done(dwmmc, 55 | DWMMC_R1, rca, 1000);
	dwmmc_check_ok_status(dwmmc, st, "CMD55 (APP_CMD)");
	st = dwmmc_wait_cmd_done(dwmmc, 6 | DWMMC_R1, 2, 1000);
	dwmmc_check_ok_status(dwmmc, st, "ACMD6 (SET_BUS_WIDTH)");
	dwmmc->ctype = 1;
	st = dwmmc_wait_cmd_done(dwmmc, 55 | DWMMC_R1, rca, 1000);
	dwmmc_check_ok_status(dwmmc, st, "CMD55 (APP_CMD)");
	dwmmc->blksiz = 64;
	dwmmc->bytcnt = 64;
	st = dwmmc_wait_cmd_done(dwmmc, 13 | DWMMC_R1 | DWMMC_CMD_DATA_EXPECTED, 0, 1000);
	dwmmc_check_ok_status(dwmmc, st, "ACMD13 (SD_STATUS)");
	{
		u32 status;
		timestamp_t start = get_timestamp();
		while (~(status = dwmmc->rintsts) & DWMMC_INT_DATA_TRANSFER_OVER) {
			if (status & DWMMC_ERROR_INT_MASK) {
				die("error transferring SSR\n");
			}
			if (get_timestamp() - start > 1000 * CYCLES_PER_MICROSECOND) {
				die("SSR timeout\n");
			}
		}
	}
	dwmmc_print_status(dwmmc);
	dwmmc->rintsts = DWMMC_INT_DATA_TRANSFER_OVER;
	for_range(i, 0, 16) {
		info("SSR%"PRIu32": 0x%08"PRIx32"\n", i, dwmmc->fifo);
	}
	dwmmc_print_status(dwmmc);
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
			udelay(100);
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
		if (state->desc[idx].desc.control & DWMMC_DES_OWN) {break;}
		u32 sizes = state->desc[idx].desc.sizes;
		bytes_transferred += (sizes & 0x1fff) + (sizes >> 13 & 0x1fff);
		desc_completed += 1;
		debugs(".");
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
