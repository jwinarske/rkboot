/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <dwmmc_regs.h>
#include <aarch64.h>
#include <inttypes.h>
#include <die.h>

void dwmmc_wait_cmd_inner(volatile struct dwmmc_regs *dwmmc, u32 cmd);enum dwmmc_status {
	DWMMC_ST_OK = 0,
	DWMMC_ST_TIMEOUT = 1,
	DWMMC_ST_ERROR = 2,
};
enum dwmmc_status dwmmc_wait_cmd_done_inner(volatile struct dwmmc_regs *dwmmc, timestamp_t raw_timeout);
timestamp_t dwmmc_wait_not_busy(volatile struct dwmmc_regs *dwmmc, timestamp_t raw_timeout);
void dwmmc_print_status(volatile struct dwmmc_regs *dwmmc);
void dwmmc_init(volatile struct dwmmc_regs *dwmmc);
void dwmmc_read_poll(volatile struct dwmmc_regs *dwmmc, u32 sector, void *buf, size_t total_bytes);
void dwmmc_read_poll_dma(volatile struct dwmmc_regs *dwmmc, u32 sector, void *buf, size_t total_bytes);

static inline void UNUSED dwmmc_wait_cmd(volatile struct dwmmc_regs *dwmmc, u32 cmd) {
	dwmmc_wait_cmd_inner(dwmmc, cmd | DWMMC_CMD_START | DWMMC_CMD_USE_HOLD_REG);
}

timestamp_t get_timestamp();

static inline enum dwmmc_status UNUSED dwmmc_wait_cmd_done(volatile struct dwmmc_regs *dwmmc, u32 cmd, u32 arg, timestamp_t timeout) {
	dwmmc->cmdarg = arg;
	dsb_st();
	timestamp_t raw_timeout = timeout * TICKS_PER_MICROSECOND;
	if (cmd & DWMMC_CMD_SYNC_DATA) {
		raw_timeout -= dwmmc_wait_not_busy(dwmmc, raw_timeout);
	}
	dwmmc_wait_cmd_inner(dwmmc, cmd | DWMMC_CMD_START | DWMMC_CMD_USE_HOLD_REG);
	return dwmmc_wait_cmd_done_inner(dwmmc, raw_timeout);
}

static inline void UNUSED dwmmc_check_ok_status(volatile struct dwmmc_regs *dwmmc, enum dwmmc_status st, const char *context) {
	assert_msg(st == DWMMC_ST_OK, "error during %s: status=0x%08"PRIx32" rintsts=0x%08"PRIx32"\n", context, dwmmc->status, dwmmc->rintsts);
}
