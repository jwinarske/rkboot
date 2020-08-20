/* SPDX-License-Identifier: CC0-1.0 */
#include <dwmmc.h>
#include <dwmmc_helpers.h>
#include <dwmmc_dma.h>

#include <inttypes.h>
#include <assert.h>

#include <die.h>
#include <log.h>
#include <timer.h>
#include <runqueue.h>


const char cmd_status_names[NUM_DWMMC_ST][16] = {
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
enum dwmmc_status dwmmc_wait_cmd_done_postissue(volatile struct dwmmc_regs *dwmmc, timestamp_t raw_timeout) {
	timestamp_t start = get_timestamp();
	u32 status;
	while (~(status = dwmmc->rintsts) & DWMMC_INT_CMD_DONE) {
		if (get_timestamp() - start > raw_timeout) {
			info("timed out waiting for command completion, rintsts=0x%"PRIx32" status=0x%"PRIx32"\n", dwmmc->rintsts, dwmmc->status);
			return DWMMC_ST_CMD_TIMEOUT;
		}
		sched_yield();
	}
	if (status & DWMMC_ERROR_INT_MASK) {return DWMMC_ST_ERROR;}
	if (status & DWMMC_INT_RESP_TIMEOUT) {return DWMMC_ST_TIMEOUT;}
	dwmmc->rintsts = DWMMC_ERROR_INT_MASK | DWMMC_INT_CMD_DONE | DWMMC_INT_RESP_TIMEOUT;
	return DWMMC_ST_OK;
}

void dwmmc_print_status(volatile struct dwmmc_regs *dwmmc, const char *context) {
	info("[%"PRIuTS"] %sresp0=0x%08"PRIx32" status=0x%08"PRIx32" rintsts=0x%04"PRIx32"\n", get_timestamp(), context, dwmmc->resp[0], dwmmc->status, dwmmc->rintsts);
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
