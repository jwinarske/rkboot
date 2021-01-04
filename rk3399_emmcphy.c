/* SPDX-License-Identifier: CC0-1.0 */
#include <rk3399/emmcphy.h>
#include <timer.h>
#include <wait_register.h>

#define STATUS 8

_Bool rk3399_emmcphy_setup(struct sdhci_phy *phy, enum sdhci_phy_setup_action  action) {
	volatile u32 *const syscon = ((struct rk3399_emmcphy *)phy)->syscon;
	if (action & 2) {
		syscon[6] = SET_BITS16(2, 0);	/* disable DLL, power down */
		udelay(3);
	}
	syscon[6] = SET_BITS16(3, 0) << 4;	/* drive impedance: 50 Ω */
	syscon[0] = SET_BITS16(1, 1) << 11	/* enable output tap delay */
		| SET_BITS16(4, 4) << 7;	/* output tap 4 */
	if (~action & 1) {return 1;}
	syscon[6] = SET_BITS16(1, 1);	/* power up */
	return wait_u32(syscon + STATUS, 0x40, 0x40, USECS(50), "EMMCPHY calibration");
}

_Bool rk3399_emmcphy_lock_freq(struct sdhci_phy UNUSED *phy, u32 khz) {
	if (!khz || khz > 200000) {return 0;}
	static const u8 frqsel_lut[4] = {1, 2, 3, 0};
	u32 frqsel = frqsel_lut[(khz - 1) / 50000];
	printf("freqsel%"PRIu32"\n", frqsel);

	volatile u32 *const syscon = ((struct rk3399_emmcphy *)phy)->syscon;
	syscon[0] = SET_BITS16(2, frqsel) << 12;
	syscon[6] = SET_BITS16(1, 1) << 1;
	if (khz < 10000) {return 1;}	/* don't fail on very slow clocks */
	if (!wait_u32(syscon + STATUS, 0x20, 0x20, USECS(1000), "EMMCPHY DLL lock")) {return 0;}
	infos("EMMCPHY locked\n");
	return 1;
}
