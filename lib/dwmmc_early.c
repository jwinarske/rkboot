/* SPDX-License-Identifier: CC0-1.0 */
#include <dwmmc.h>
#include <dwmmc_helpers.h>
#include <dwmmc_dma.h>

#include <inttypes.h>

#include <timer.h>
#include <runqueue.h>
#include <sd.h>

_Bool dwmmc_init_early(volatile struct dwmmc_regs *dwmmc) {
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
	st = dwmmc_wait_cmd_done(dwmmc, 55 | DWMMC_R1, 0, 1000);
	if (st != DWMMC_ST_OK) {
		dwmmc_print_status(dwmmc, "ACMD41 ");
		return 0;
	}
	dwmmc_wait_not_busy(dwmmc, 1000);
	dwmmc->cmdarg = 0x00ff8000 | (sd_2_0 ? SD_OCR_HIGH_CAPACITY | SD_OCR_XPC | SD_OCR_S18R : 0);
	/* don't wait for command done, dwmmc_init_late will deal with it */
	return dwmmc_wait_cmd(dwmmc, 41 | DWMMC_R3);
}
