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
#include <gic.h>
#include <iost.h>

_Bool sdhci_init_early(struct sdhci_state *st) {
	volatile struct sdhci_regs *sdhci = st->regs;
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
	struct sdhci_phy *phy = st->phy;
	if (!phy->setup(phy, SDHCI_PHY_START)) {return 0;}
	if (!phy->lock_freq(phy, 400)) {return 0;}
	sdhci->clock_control = clkctrl |= SDHCI_CLKCTRL_SDCLK_EN;
	sdhci->timeout = 14;
	sdhci->int_st_enable = sdhci->int_signal_enable = 0xfffff0ff;
	sdhci->arg = 0;
	puts("submitting CMD0\n");
	sdhci->cmd = 0;
	/* dramstage will poll the card for init complete, just kick off init here */
	if (IOST_OK != sdhci_submit_cmd(st, SDHCI_CMD(1) | SDHCI_R3, 0x40000080)) {return 0;}
	return 1;
}

extern struct sdhci_phy emmc_phy;
struct sdhci_state emmc_state = {
	.regs = regmap_emmc,
	.phy = &emmc_phy,
};

void emmc_init(struct sdhci_state *st) {
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

	if (!sdhci_init_early(st)) {
		gicv2_disable_spi(gic500d, 43);
		gicv2_wait_disabled(gic500d);
		/* shut down phy */
		grf[GRF_EMMCPHY_CON+6] = SET_BITS16(2, 0);
		dsb_st();	/* GRF_EMMCPHY_CONx seem to be in the clk_emmc domain. ensure completion to avoid hangs */
		/* gate eMMC clocks */
		cru[CRU_CLKGATE_CON+6] = SET_BITS16(3, 7) << 12;
		cru[CRU_CLKGATE_CON+32] = SET_BITS16(1, 1) << 8 | SET_BITS16(1, 1) << 10;
	}
	rk3399_set_init_flags(RK3399_INIT_EMMC_INIT);
}
