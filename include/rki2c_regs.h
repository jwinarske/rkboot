/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

struct rki2c_regs {
	u32 control;
	u32 clkdiv;
	u32 slave_addr;
	u32 reg_addr;
	u32 tx_count;
	u32 rx_count;
	u32 int_enable;
	u32 int_pending;
	u32 finished_count;
	u32 slave_scl_debounce;
	u32 padding0[(0x100 - 0x28) / 4];
	u32 tx_data[8];
	u32 padding1[56];
	u32 rx_data[8];
	u32 status;
};

struct rki2c_config {
	u32 control;
	u32 clkdiv;
};

enum {
	RKI2C_CON_ENABLE = 1,
	RKI2C_CON_MODE_TX = 0,
	RKI2C_CON_MODE_REGISTER_READ = 1 << 1,
	RKI2C_CON_MODE_RX = 2 << 1,
	RKI2C_CON_MODE_BIZARRO_REGISTER_READ = 3 << 1,
	RKI2C_CON_START = 8,
	RKI2C_CON_STOP = 16,
	RKI2C_CON_ACK = 32,
	RKI2C_CON_NAK_HALT = 64,
};

enum {
	RKI2C_INT_BYTE_TX = 0,
	RKI2C_INT_BYTE_RX,
	RKI2C_INT_ALL_TX,
	RKI2C_INT_ALL_RX,
	RKI2C_INT_START,
	RKI2C_INT_STOP,
	RKI2C_INT_NAK,
	RKI2C_INT_SLAVE_HOLD_SCL,
	RKI2C_INTMASK_XACT_END = 1 << RKI2C_INT_ALL_RX | 1 << RKI2C_INT_ALL_TX | 1 << RKI2C_INT_NAK,
};
