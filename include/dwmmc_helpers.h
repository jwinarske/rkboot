/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <dwmmc.h>
#include <dwmmc_regs.h>
#include <stdatomic.h>

#include <die.h>
#include <log.h>
#include <aarch64.h>
#include <timer.h>
#include <iost.h>

enum iost dwmmc_wait_cmd(struct dwmmc_state *st, u32 cmd, timestamp_t start);
enum iost dwmmc_wait_cmd_done_postissue(struct dwmmc_state *st, timestamp_t start, timestamp_t raw_timeout);
_Bool dwmmc_wait_data_idle(struct dwmmc_state *st, timestamp_t start, timestamp_t raw_timeout);
void dwmmc_print_status(volatile struct dwmmc_regs *dwmmc, const char *context);

void dwmmc_handle_dma_interrupt(struct dwmmc_state *state, u32 status);

HEADER_FUNC enum iost dwmmc_wait_cmd_done(struct dwmmc_state *st, u32 cmd, u32 arg, timestamp_t timeout) {
	st->regs->cmdarg = arg;
	dsb_st();
	timestamp_t start = get_timestamp();
	enum iost res;
	if (IOST_OK != (res = dwmmc_wait_cmd(st, cmd | st->cmd_template, start))) {return res;}
	if (IOST_OK != (res = dwmmc_wait_cmd_done_postissue(st, start, timeout))) {return res;}
	if (!dwmmc_wait_data_idle(st, start, timeout)) {return IOST_GLOBAL;}
	return IOST_OK;
}

HEADER_FUNC _Bool set_clock_enable(struct dwmmc_state *st, _Bool enable) {
	st->regs->clkena = enable;
	timestamp_t start = get_timestamp();
	if (IOST_OK != dwmmc_wait_cmd(st, DWMMC_CMD_UPDATE_CLOCKS | DWMMC_CMD_SYNC_DATA | DWMMC_CMD_START, start)) {
		info("failed to program CLKENA=%u\n", (unsigned)enable);
		return 0;
	}
	atomic_fetch_or_explicit(&st->int_st, DWMMC_INT_DATA_NO_BUSY | DWMMC_INT_CMD_DONE, memory_order_release);
	return 1;
}
