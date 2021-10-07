/* SPDX-License-Identifier: CC0-1.0 */
#include <rk3399/sramstage.h>
#include <stdatomic.h>
#include <assert.h>

#include <log.h>
#include <mmu.h>
#include <timer.h>
#include <aarch64.h>
#include <dwmmc.h>
#include <dwmmc_regs.h>
#include <rk3399.h>
#include <runqueue.h>

struct dwmmc_state sdmmc_state = {
	.regs = regmap_sdmmc,
	.int_st = DWMMC_INT_DATA_NO_BUSY | DWMMC_INT_CMD_DONE | DWMMC_INT_DATA_TRANSFER_OVER,
	.cmd_template = DWMMC_CMD_START | DWMMC_CMD_USE_HOLD_REG,
};

void rk3399_init_sdmmc() {
	static volatile u32 *const cru = regmap_cru;
	/* hclk_sd = 200 MHz */
	cru[CRU_CLKSEL_CON + 13] = SET_BITS16(1, 0) << 15 | SET_BITS16(5, 4) << 8;
	/* clk_sdmmc = 24 MHz / 30 = 800 kHz */
	cru[CRU_CLKSEL_CON + 16] = SET_BITS16(3, 5) << 8 | SET_BITS16(7, 29);
	dsb_st();
	cru[CRU_CLKGATE_CON+6] = SET_BITS16(1, 0) << 1;	/* ungate clk_sdmmc */
	cru[CRU_CLKGATE_CON+12] = SET_BITS16(1, 0) << 13;	/* ungate hclk_sd */
	/* drive phase 180° */
	cru[CRU_SDMMC_CON] = SET_BITS16(1, 1);
	cru[CRU_SDMMC_CON + 0] = SET_BITS16(2, 1) << 1 | SET_BITS16(8, 0) << 3 | SET_BITS16(1, 0) << 11;
	cru[CRU_SDMMC_CON + 1] = SET_BITS16(2, 0) << 1 | SET_BITS16(8, 0) << 3 | SET_BITS16(1, 0) << 11;
	cru[CRU_SDMMC_CON] = SET_BITS16(1, 0);
	/* mux out the SD lines */
	regmap_grf[GRF_GPIO4B_IOMUX] = SET_BITS16(2, 1) | SET_BITS16(2, 1) << 2 | SET_BITS16(2, 1) << 4 | SET_BITS16(2, 1) << 6 | SET_BITS16(2, 1) << 8 | SET_BITS16(2, 1) << 10;
	/* mux out card detect */
	regmap_pmugrf[PMUGRF_GPIO0A_IOMUX] = SET_BITS16(2, 1) << 14;
	/* reset SDMMC */
	cru[CRU_SOFTRST_CON + 7] = SET_BITS16(1, 1) << 10;
	usleep(100);
	cru[CRU_SOFTRST_CON + 7] = SET_BITS16(1, 0) << 10;
	usleep(2000);

	dsb_ishst();
	info("starting SDMMC");
	if (!dwmmc_init_early(&sdmmc_state)) {
		puts("SD init failed");
		atomic_thread_fence(memory_order_release);
		/* gate hclk_sd, this is checked by dramstage to see if SDMMC was initialized */
		cru[CRU_CLKGATE_CON+12] = SET_BITS16(1, 1) << 13;
	}
	rk3399_set_init_flags(RK3399_INIT_SD_INIT);
}
