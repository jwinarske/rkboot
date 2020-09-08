/* SPDX-License-Identifier: CC0-1.0 */
#include <dwmmc.h>
#include <dwmmc_helpers.h>

#include <inttypes.h>

#include <timer.h>
#include <runqueue.h>
#include <sd.h>

_Bool dwmmc_init_early(struct dwmmc_state *state) {
	volatile struct dwmmc_regs *dwmmc = state->regs;
	dwmmc->pwren = 1;
	usleep(1000);
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
	dwmmc->rintsts = ~(u32)0;
	//dwmmc->card_threshold = DWMMC_CARDTHRCTL_BUSY_CLEAR_INT;
	dwmmc->intmask = DWMMC_INTMASK_ALL;
	dwmmc->fifoth = 0x307f0080;
	dwmmc->tmout = 0xffffffff;
	dwmmc->ctrl = DWMMC_CTRL_INT_ENABLE;
	dwmmc->rst_n = 1;
	usleep(5);
	dwmmc->rst_n = 0;
	dwmmc->ctype = 0;
	dwmmc->clkdiv[0] = 0;
	dsb_st();
	if (!set_clock_enable(state, 1)) {return 0;}
	debugs("clock enabled\n");
	enum iost st = dwmmc_wait_cmd_done(state, 0 | DWMMC_CMD_SEND_INITIALIZATION | DWMMC_CMD_SYNC_DATA, 0, USECS(1000));
	if (st != IOST_OK) {
		info("error on CMD0: status=%u\n", (unsigned)st);
		return 0;
	}
	dwmmc_print_status(dwmmc, "CMD0 ");
	st = dwmmc_wait_cmd_done(state, 8 | DWMMC_R1, 0x1aa, MSECS(10));
	dwmmc_print_status(dwmmc, "CMD8 ");
	if (st == IOST_LOCAL) {return 0;}
	_Bool sd_2_0 = st != IOST_LOCAL;
	if (!sd_2_0) {
		st = dwmmc_wait_cmd_done(state, 0 | DWMMC_CMD_SYNC_DATA, 0, USECS(1000));
		dwmmc_print_status(dwmmc, "CMD0 ");
		if (st != IOST_OK) {return 0;}
	} else {
		if ((dwmmc->resp[0] & 0xff) != 0xaa) {
			 infos("CMD8 check pattern returned incorrectly\n");
			 return 0;
		}
	}
	st = dwmmc_wait_cmd_done(state, 55 | DWMMC_R1, 0, USECS(1000));
	if (st != IOST_OK) {
		dwmmc_print_status(dwmmc, "ACMD41 ");
		return 0;
	}
	dwmmc_wait_data_idle(state, get_timestamp(), USECS(1000));
	dwmmc->cmdarg = 0x00ff8000 | (sd_2_0 ? SD_OCR_HIGH_CAPACITY | SD_OCR_XPC | SD_OCR_S18R : 0);
	dwmmc->ctrl = 0;	/* disable interrupts */
	atomic_thread_fence(memory_order_release);
	/* don't wait for command done, dwmmc_init_late will deal with it */
	return IOST_OK == dwmmc_wait_cmd(state, 41 | DWMMC_R3 | state->cmd_template, get_timestamp());
}
