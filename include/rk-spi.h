#pragma once
#include <defs.h>

struct spi {
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
CHECK_OFFSET(spi, status, 0x24);
CHECK_OFFSET(spi, tx, 0x400);
CHECK_OFFSET(spi, rx, 0x800);

enum {
	SPI_DFS_8BIT = 1,
	SPI_CSM_KEEP_LOW = 0,
	SPI_CSM_HALF_CYCLE = 1 << 8,
	SPI_CSM_FULL_CYCLE = 2 << 8,
	SPI_SSD_HALF_CYCLE = 0,
	SPI_SSD_FULL_CYCLE = 1 << 10,
	SPI_LITTLE_ENDIAN = 0,
	SPI_BIG_ENDIAN = 1 << 11,
	SPI_MSB_FIRST = 0,
	SPI_LSB_FIRST = 1 << 12,
	SPI_BHT_APB_16BIT = 0,
	SPI_BHT_APB_8BIT = 1 << 13,
	SPI_FRF_SPI = 0,
	SPI_XFM_TR = 0,
	SPI_XFM_TX = 1 << 18,
	SPI_XFM_RX = 2 << 18,
	SPI_MASTER = 0,
	SPI_SLAVE = 1 << 20,
};

#define SPI_PHASE(n) ((n) << 6)
#define SPI_POLARITY(n) ((n) << 7)
#define SPI_SAMPLE_DELAY(n) ((n) << 14)

static volatile struct spi *const spi = (volatile struct spi*)0xff1d0000;
