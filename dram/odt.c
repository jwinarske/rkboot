/* SPDX-License-Identifier: CC0-1.0 */
#include <main.h>
#include <rk3399.h>
#include "rk3399-dmc.h"

const struct odt_preset odt_50mhz = {
	.phy = {
		.rd_vref = 41,
	}
};

const struct odt_preset odt_600mhz = {
	.phy = {
		.rd_vref = 32,
	}
};

const struct odt_preset odt_933mhz = {
	.phy = {
		.rd_vref = 20,
	}
};

void set_phy_io(volatile u32 *phy, const struct odt_settings *odt) {
}
