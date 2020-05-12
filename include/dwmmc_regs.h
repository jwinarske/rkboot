/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

struct dwmmc_regs {
	u32 ctrl;
	u32 pwren;
	u32 clkdiv[1];
	u32 clksrc;
	u32 clkena;
	u32 tmout;
	u32 ctype;
	u32 blksiz;
	u32 bytcnt;
	u32 intmask;
	u32 cmdarg;
	u32 cmd;
	u32 resp[4];
	u32 mintsts;
	u32 rintsts;
	u32 status;
	u32 fifoth;
	u32 cdetect;
	u32 wrtprt;
	u32 padding1;
	u32 tcbcnt;
	u32 tbbcnt;
	u32 debnce;
	u32 usrid;
	u32 verid;
	u32 hcon;
	u32 uhs_reg;
	u32 rst_n;
	u32 padding2;
	u32 bmod;
};
CHECK_OFFSET(dwmmc_regs, cmdarg, 0x28);
CHECK_OFFSET(dwmmc_regs, status, 0x48);
CHECK_OFFSET(dwmmc_regs, hcon, 0x70);
CHECK_OFFSET(dwmmc_regs, bmod, 0x80);

enum {
	DWMMC_CMD_START = 1 << 31,
	/* … */
	DWMMC_CMD_USE_HOLD_REG = 1 << 29,
	DWMMC_CMD_VOLT_SWITCH = 1 << 28,
	/* … */
	DWMMC_CMD_UPDATE_CLOCKS = 1 << 21,
	/* … */
	DWMMC_CMD_SEND_INITIALIZATION = 1 << 15,
	/* … */
	DWMMC_CMD_SYNC_DATA = 1 << 13,
	/* … */
	DWMMC_CMD_DATA_EXPECTED = 1 << 9,
	DWMMC_CMD_CHECK_RESPONSE_CRC = 1 << 8,
	DWMMC_CMD_RESPONSE_LENGTH = 1 << 7,
	DWMMC_CMD_RESPONSE_EXPECT = 1 << 6,
};
enum {
	DWMMC_R1 = DWMMC_CMD_SYNC_DATA | DWMMC_CMD_CHECK_RESPONSE_CRC | DWMMC_CMD_RESPONSE_EXPECT,
	DWMMC_R2 = DWMMC_CMD_SYNC_DATA | DWMMC_CMD_CHECK_RESPONSE_CRC | DWMMC_CMD_RESPONSE_EXPECT | DWMMC_CMD_RESPONSE_LENGTH,
	DWMMC_R3 = DWMMC_CMD_SYNC_DATA | DWMMC_CMD_RESPONSE_EXPECT, /* no CRC checking */
	DWMMC_R6 = DWMMC_CMD_SYNC_DATA | DWMMC_CMD_CHECK_RESPONSE_CRC | DWMMC_CMD_RESPONSE_EXPECT,
};
enum {
	DWMMC_INT_RESP_TIMEOUT = 0x100,
	/* … */
	DWMMC_INT_DATA_TRANSFER_OVER = 8,
	DWMMC_INT_CMD_DONE = 4,
	/* … */
	DWMMC_ERROR_INT_MASK = 0xb8c2
};
enum {
	/* … */
	DWMMC_STATUS_DATA_BUSY = 1 << 9,
	/* … */
};
