/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

enum {
	RKPCIE_CLIENT_CONF = 0,
	RKPCIE_CLIENT_POWER_CTRL,
	RKPCIE_CLIENT_POWER_STATUS,
	RKPCIE_CLIENT_INT_CTRL,
	RKPCIE_CLIENT_ERR_CTRL,
	RKPCIE_CLIENT_ERR_CNT,
	RKPCIE_CLIENT_HOT_RESET_CTRL,
	RKPCIE_CLIENT_SIDE_BAND_CTRL,
	RKPCIE_CLIENT_SIDE_BAND_ST,
	RKPCIE_CLIENT_FLR_DONE,
	RKPCIE_CLIENT_FLR_STATUS,
	RKPCIE_CLIENT_VF_STATUS,
	RKPCIE_CLIENT_VF_PWR_STATUS,
	RKPCIE_CLIENT_VF_TPH_STATUS,
	RKPCIE_CLIENT_TPH_STATUS,
	RKPCIE_CLIENT_DEBUG_OUT,
	RKPCIE_CLIENT_DEBUG_OUT_1,
	RKPCIE_CLIENT_BASIC_STATUS,
	RKPCIE_CLIENT_BASIC_STATUS1,
};

enum {
	RKPCIE_CLI_GEN2 = 1 << 7,
	RKPCIE_CLI_ROOT_PORT = 64,
#define RKPCIE_CLI_LANE_COUNT_SHIFT(shift) ((shift) << 4 & 0x30)
	RKPCIE_CLI_ARI_EN = 8,
	RKPCIE_CLI_SR_IOV_EN = 4,
	RKPCIE_CLI_LINK_TRAIN_EN = 2,
	RKPCIE_CLI_CONF_EN = 1,
};

enum {
	RKPCIE_MGMT_PLC0 = 0,
	RKPCIE_MGMT_PLC1,

	RKPCIE_MGMT_LWC = 0x50 >> 2,

	RKPCIE_MGMT_RCBAR = 0x300 >> 2,
};

enum {RKPCIE_MGMT_PLC0_LINK_TRAINED = 1};

enum {
	RKPCIE_RCCONF_PCIECAP = 0xc0 >> 2,
};
