/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <dwmmc.h>
#include <dwmmc_regs.h>

#include <inttypes.h>

#include <die.h>
#include <log.h>
#include <aarch64.h>

#define DEFINE_DWMMC_ST\
	X(OK) X(CMD_TIMEOUT) X(TIMEOUT) X(ERROR)

enum dwmmc_status {
#define X(name) DWMMC_ST_##name,
	DEFINE_DWMMC_ST
#undef X
	NUM_DWMMC_ST
};

const char cmd_status_names[NUM_DWMMC_ST][16];

_Bool dwmmc_wait_cmd_inner(volatile struct dwmmc_regs *dwmmc, u32 cmd);
enum dwmmc_status dwmmc_wait_cmd_done_postissue(volatile struct dwmmc_regs *dwmmc, timestamp_t raw_timeout);
timestamp_t dwmmc_wait_not_busy(volatile struct dwmmc_regs *dwmmc, timestamp_t raw_timeout);
void dwmmc_print_status(volatile struct dwmmc_regs *dwmmc, const char *context);


HEADER_FUNC _Bool dwmmc_wait_cmd(volatile struct dwmmc_regs *dwmmc, u32 cmd) {
	return dwmmc_wait_cmd_inner(dwmmc, cmd | DWMMC_CMD_START | DWMMC_CMD_USE_HOLD_REG);
}

HEADER_FUNC enum dwmmc_status dwmmc_wait_cmd_done(volatile struct dwmmc_regs *dwmmc, u32 cmd, u32 arg, timestamp_t timeout) {
	dwmmc->cmdarg = arg;
	dsb_st();
	timestamp_t raw_timeout = timeout * TICKS_PER_MICROSECOND;
	if (cmd & DWMMC_CMD_SYNC_DATA) {
		raw_timeout -= dwmmc_wait_not_busy(dwmmc, raw_timeout);
	}
	if (!dwmmc_wait_cmd_inner(dwmmc, cmd | DWMMC_CMD_START | DWMMC_CMD_USE_HOLD_REG)) {return DWMMC_ST_TIMEOUT;}
	return dwmmc_wait_cmd_done_postissue(dwmmc, raw_timeout);
}

HEADER_FUNC void dwmmc_check_ok_status(volatile struct dwmmc_regs *dwmmc, enum dwmmc_status st, const char *context) {
	assert_msg(st == DWMMC_ST_OK, "error during %s: status=0x%08"PRIx32" rintsts=0x%08"PRIx32"\n", context, dwmmc->status, dwmmc->rintsts);
}

HEADER_FUNC _Bool set_clock_enable(volatile struct dwmmc_regs *dwmmc, _Bool enable) {
	dwmmc->clkena = enable;
	if (!dwmmc_wait_cmd(dwmmc, DWMMC_CMD_UPDATE_CLOCKS | DWMMC_CMD_SYNC_DATA)) {
		info("failed to program CLKENA=%u\n", (unsigned)enable);
		return 0;
	}
	return 1;
}
