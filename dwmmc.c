/* SPDX-License-Identifier: CC0-1.0 */
#include <dwmmc.h>
#include <lib.h>
#include <die.h>
#include <rk3399.h>
#include <inttypes.h>

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
