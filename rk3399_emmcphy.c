/* SPDX-License-Identifier: CC0-1.0 */
#include <timer.h>
#include <wait_register.h>
#include <rk3399.h>
#include <sdhci.h>

static _Bool emmc_phy_setup(struct sdhci_phy UNUSED *phy, enum sdhci_phy_setup_action  action) {
	if (action & 2) {
		grf[GRF_EMMCPHY_CON+6] = SET_BITS16(2, 0);	/* disable DLL, power down */
		udelay(3);
	}
	grf[GRF_EMMCPHY_CON+6] = SET_BITS16(3, 0) << 4;	/* drive impedance: 50 Ω */
	grf[GRF_EMMCPHY_CON+0] = SET_BITS16(1, 1) << 11	/* enable output tap delay */
		| SET_BITS16(4, 4) << 7;	/* output tap 4 */
	if (~action & 1) {return 1;}
	grf[GRF_EMMCPHY_CON+6] = SET_BITS16(1, 1);	/* power up */
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
	if (!wait_u32(grf + GRF_EMMCPHY_STATUS, 0x20, 0x20, USECS(1000), "EMMCPHY DLL lock")) {return 0;}
	infos("EMMCPHY locked\n");
	return 1;
}

struct sdhci_phy emmc_phy = {
	.setup = emmc_phy_setup,
	.lock_freq = emmc_phy_lock_freq,
};
