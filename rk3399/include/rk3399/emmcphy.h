/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <sdhci.h>

_Bool rk3399_emmcphy_setup(struct sdhci_phy *phy, enum sdhci_phy_setup_action  action);
_Bool rk3399_emmcphy_lock_freq(struct sdhci_phy *phy, uint32_t khz);

struct rk3399_emmcphy {
	struct sdhci_phy phy;
	volatile u32 *syscon;
};
