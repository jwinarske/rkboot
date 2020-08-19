/* SPDX-License-Identifier: CC0-1.0 */
#include <rk3399/sramstage.h>
#include <inttypes.h>
#include <assert.h>
#include <stdatomic.h>

#include <sdhci.h>
#include <sdhci_helpers.h>
#include <sdhci_regs.h>
#include <rk3399.h>
#include <aarch64.h>
#include <mmu.h>
#include <timer.h>
#include <log.h>
#include <runqueue.h>
#include <wait_register.h>

_Bool sdhci_init(volatile struct sdhci_regs *sdhci, struct sdhci_state *st, struct sdhci_phy *phy) {
	puts("SDHCI init\n");
	st->version = sdhci->sdhci_version;
	st->caps = (u64)sdhci->capabilities[1] << 32 | sdhci->capabilities[0];
	assert(st->version >= 2);
	sdhci->swreset = SDHCI_SWRST_ALL;
	wait_u8_unset(&sdhci->swreset, SDHCI_SWRST_ALL, USECS(100), "SDHCI reset");
	sdhci->power_control = SDHCI_PWRCTRL_1V8 | SDHCI_PWRCTRL_ON;
	usleep(1000);
	sdhci->clock_control = 0;
	udelay(10);
	u16 baseclock_mhz = st->caps >> 8 & 0xff;
	u16 div400k = baseclock_mhz * 10 / 4;
	assert(div400k < 0x400);
	u16 clkctrl = sdhci->clock_control = SDHCI_CLKCTRL_DIV(div400k) | SDHCI_CLKCTRL_INTCLK_EN;
	wait_u16_set(&sdhci->clock_control, SDHCI_CLKCTRL_INTCLK_STABLE, USECS(100), "SDHCI internal clock");
	if (!phy->setup(phy, SDHCI_PHY_START)) {return 0;}
	if (!phy->lock_freq(phy, 400)) {return 0;}
	sdhci->clock_control = clkctrl |= SDHCI_CLKCTRL_SDCLK_EN;
	sdhci->timeout = 14;
	sdhci->int_st_enable = sdhci->int_signal_enable = 0xffff0003;
	sdhci->arg = 0;
	puts("submitting CMD0\n");
	sdhci->cmd = 0;
	u32 ocr;
	while (1) {
		if (!sdhci_submit_cmd(sdhci, st, SDHCI_CMD(1) | SDHCI_R3, 0x40ff8000)) {return 0;}
		if (!sdhci_wait_state_clear(sdhci, st, SDHCI_PRESTS_CMD_INHIBIT, USECS(100), "CMD1")) {return 0;}
		ocr = sdhci->resp[0];
		printf("OCR: %08"PRIx32"\n", ocr);
		if (ocr & 1 << 31) {break;}
		usleep(1000);
	}
	return 1;
}

static _Bool emmc_phy_setup(struct sdhci_phy UNUSED *phy, enum sdhci_phy_setup_action  action) {
	if (action & 2) {
		grf[GRF_EMMCPHY_CON+6] = SET_BITS16(2, 0);
		udelay(3);
	}
	if (~action & 1) {return 1;}
	grf[GRF_EMMCPHY_CON+6] = SET_BITS16(1, 1);
	return wait_u32(grf + GRF_EMMCPHY_STATUS, 0x40, 0x40, USECS(50), "EMMCPHY calibration");
}

static _Bool emmc_phy_lock_freq(struct sdhci_phy UNUSED *phy, u32 khz) {
	if (!khz || khz > 200000) {return 0;}
	static const u8 frqsel_lut[4] = {1, 2, 3, 0};
	u32 frqsel = frqsel_lut[(khz - 1) / 50000];
	printf("freqsel%"PRIu32"\n", frqsel);
	grf[GRF_EMMCPHY_CON+0] = SET_BITS16(2, frqsel) << 12;
	grf[GRF_EMMCPHY_CON+6] = SET_BITS16(1, 1) << 1;
	if (khz < 10000) {return 1;}	/* don't fail on very slow clocks */
	return wait_u32(grf + GRF_EMMCPHY_STATUS, 0x20, 0x20, USECS(1000), "EMMCPHY DLL lock");
}

static struct sdhci_phy phy = {
	.setup = emmc_phy_setup,
	.lock_freq = emmc_phy_lock_freq,
};

void emmc_init(struct sdhci_state *st) {
	mmu_map_mmio_identity(0xfe330000, 0xfe33ffff);
	/* aclk_emmc = CPLL/4 = 200 MHz */
	cru[CRU_CLKSEL_CON+21] = SET_BITS16(1, 0) << 7 | SET_BITS16(5, 3);
	/* clk_emmc = CPLL/4 = 200 MHz, as specified by capability register */
	cru[CRU_CLKSEL_CON+22] = SET_BITS16(3, 0) << 8 | SET_BITS16(7, 3);
	/* mux out emmc_pwren */
	pmugrf[PMUGRF_GPIO0A_IOMUX] = SET_BITS16(2, 1) << 10;
	dsb_st();
	/* ungate eMMC clocks */
	cru[CRU_CLKGATE_CON+6] = SET_BITS16(3, 0) << 12;
	cru[CRU_CLKGATE_CON+32] = SET_BITS16(3, 0) << 8;
	dsb_st();
	cru[CRU_SOFTRST_CON+6] = SET_BITS16(3, 7) << 12;
	usleep(10);
	cru[CRU_SOFTRST_CON+6] = SET_BITS16(3, 0) << 12;
	usleep(100);

	if (!sdhci_init(emmc, st, &phy)) {
			/* gate eMMC clocks */
			cru[CRU_CLKGATE_CON+6] = SET_BITS16(3, 7) << 12;
			/* shut down phy */
			grf[GRF_EMMCPHY_CON+6] = SET_BITS16(2, 0);
	}
	rk3399_set_init_flags(RK3399_INIT_EMMC_INIT);
}
