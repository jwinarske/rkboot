/* SPDX-License-Identifier: CC0-1.0 */
#include <stdatomic.h>
#include <assert.h>

#include <log.h>
#include <mmu.h>
#include <timer.h>
#include <aarch64.h>
#include <dwmmc.h>
#include <rk3399.h>
#include <runqueue.h>

void rk3399_init_sdmmc() {
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
	grf[GRF_GPIO4B_IOMUX] = SET_BITS16(2, 1) | SET_BITS16(2, 1) << 2 | SET_BITS16(2, 1) << 4 | SET_BITS16(2, 1) << 6 | SET_BITS16(2, 1) << 8 | SET_BITS16(2, 1) << 10;
	/* mux out card detect */
	pmugrf[PMUGRF_GPIO0A_IOMUX] = SET_BITS16(2, 1) << 14;
	/* reset SDMMC */
	cru[CRU_SOFTRST_CON + 7] = SET_BITS16(1, 1) << 10;
	usleep(100);
	cru[CRU_SOFTRST_CON + 7] = SET_BITS16(1, 0) << 10;
	usleep(2000);

	mmu_map_mmio_identity(0xfe320000, 0xfe320fff);
	dsb_ishst();
	info("starting SDMMC\n");
	if (!dwmmc_init_early(sdmmc)) {
		puts("SD init failed\n");
		atomic_thread_fence(memory_order_release);
		/* gate hclk_sd, this is checked by dramstage to see if SDMMC was initialized */
		cru[CRU_CLKGATE_CON+12] = SET_BITS16(1, 1) << 13;
	}
	rk3399_set_init_flags(RK3399_INIT_SD_INIT);
}
