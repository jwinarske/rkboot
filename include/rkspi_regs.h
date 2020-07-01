/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

struct rkspi_regs {
	u32 ctrl0, ctrl1;
	u32 enable;
	u32 slave_enable;
	u32 baud;
	u32 tx_fifo_threshold, rx_fifo_threshold;
	u32 tx_fifo_level, rx_fifo_level;
	u32 status;
	u32 intr_polarity, intr_mask, intr_status, intr_raw_status, intr_clear;
	u32 dma_ctrl, dma_tx_level, dma_rx_level;
	u32 pad0[0x3b8/4];
	u32 tx;
	u32 pad1[0x3fc/4];
	u32 rx;
};
CHECK_OFFSET(rkspi_regs, status, 0x24);
CHECK_OFFSET(rkspi_regs, tx, 0x400);
CHECK_OFFSET(rkspi_regs, rx, 0x800);

enum {
	RKSPI_DFS_8BIT = 1,
	RKSPI_CSM_KEEP_LOW = 0,
	RKSPI_CSM_HALF_CYCLE = 1 << 8,
	RKSPI_CSM_FULL_CYCLE = 2 << 8,
	RKSPI_SSD_HALF_CYCLE = 0,
	RKSPI_SSD_FULL_CYCLE = 1 << 10,
	RKSPI_LITTLE_ENDIAN = 0,
	RKSPI_BIG_ENDIAN = 1 << 11,
	RKSPI_MSB_FIRST = 0,
	RKSPI_LSB_FIRST = 1 << 12,
	RKSPI_BHT_APB_16BIT = 0,
	RKSPI_BHT_APB_8BIT = 1 << 13,
	RKSPI_FRF_SPI = 0,
	RKSPI_XFM_TR = 0,
	RKSPI_XFM_TX = 1 << 18,
	RKSPI_XFM_RX = 2 << 18,
	RKSPI_MASTER = 0,
	RKSPI_SLAVE = 1 << 20,
};

enum {
	RKSPI_RX_FULL_INTR = 16,
	RKSPI_RX_OVERFLOW_INTR = 8,
	RKSPI_RX_UNDERFLOW_INTR = 4,
	RKSPI_TX_OVERFLOW_INTR = 2,
	RKSPI_TX_EMPTY_INTR = 1,
};

enum {RKSPI_MAX_RECV = 0xfffe};

#define RKSPI_PHASE(n) ((n) << 6)
#define RKSPI_POLARITY(n) ((n) << 7)
#define RKSPI_SAMPLE_DELAY(n) ((n) << 14)

static const u32 rkspi_mode_base = RKSPI_MASTER | RKSPI_CSM_KEEP_LOW | RKSPI_SSD_FULL_CYCLE | RKSPI_LITTLE_ENDIAN | RKSPI_MSB_FIRST | RKSPI_POLARITY(1) | RKSPI_PHASE(1) | RKSPI_DFS_8BIT;
