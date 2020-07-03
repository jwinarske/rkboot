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
	u32 poll_demand;
	u32 desc_list_base;
	u32 idmac_status;
	u32 idmac_int_enable;
	u32 cur_desc_addr;
	u32 cur_buf_addr;
};
CHECK_OFFSET(dwmmc_regs, cmdarg, 0x28);
CHECK_OFFSET(dwmmc_regs, status, 0x48);
CHECK_OFFSET(dwmmc_regs, hcon, 0x70);
CHECK_OFFSET(dwmmc_regs, bmod, 0x80);

enum {
	DWMMC_CTRL_USE_IDMAC = 1 << 25,
	/* … */
	DWMMC_CTRL_INT_ENABLE = 16,
	/* bit 3 reserved */
	DWMMC_CTRL_DMA_RESET = 4,
	DWMMC_CTRL_FIFO_RESET = 2,
	DWMMC_CTRL_CONTROLLER_RESET = 1,
};

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
	DWMMC_INT_RX_FIFO_DATA_REQ = 0x20,
	DWMMC_INT_TX_FIFO_DATA_REQ = 0x10,
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

enum {
	/* … */
	DWMMC_BMOD_IDMAC_ENABLE = 1 << 7,
	/* … */
	DWMMC_BMOD_FIXED_BURST = 2,
	DWMMC_BMOD_SOFT_RESET = 1,
};

enum {
	DWMMC_IDMAC_INT_ABNORMAL = 1 << 9,
	DWMMC_IDMAC_INT_NORMAL = 1 << 8,
	DWMMC_IDMAC_INT_CARD_ERROR = 32,
	DWMMC_IDMAC_INT_DESC_UNAVAILABLE = 16,
	DWMMC_IDMAC_INT_FATAL_BUS_ERROR = 4,
	DWMMC_IDMAC_INT_RECEIVE = 2,
	DWMMC_IDMAC_INT_TRANSMIT = 1,
	DWMMC_IDMAC_INTMASK_ABNORMAL = 0x214,
	DWMMC_IDMAC_INTMASK_NORMAL = 0x103,
	DWMMC_IDMAC_INTMASK_ALL = 0x337,
};

struct dwmmc_idmac_desc {
	_Alignas(8)
	_Atomic(u32) control;
	u32 sizes;
	u32 ptr1;
	u32 ptr2;
};
_Static_assert(sizeof(struct dwmmc_idmac_desc) == 16, "iDMAC descriptor size is wrong");

enum {
	DWMMC_DES_OWN = 1 << 31,
	DWMMC_DES_CES = 1 << 30,
	DWMMC_DES_END_OF_RING = 32,
	DWMMC_DES_CHAIN_PTR = 16,
	DWMMC_DES_FIRST = 8,
	DWMMC_DES_LAST = 4,
	DWMMC_DES_DISABLE_INTERRUPT = 2,
};
