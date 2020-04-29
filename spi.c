#include <main.h>
#include <rk-spi.h>
#include <rk3399.h>

static const u32 spi_mode_base = SPI_MASTER | SPI_CSM_KEEP_LOW | SPI_SSD_FULL_CYCLE | SPI_LITTLE_ENDIAN | SPI_MSB_FIRST | SPI_POLARITY(1) | SPI_PHASE(1) | SPI_DFS_8BIT;

static void spi_recv_fast(u8 *buf, u32 buf_size) {
	assert((spi_mode_base | SPI_BHT_APB_8BIT) == 0x24c1);
	assert((u64)buf % 2 == 0 && buf_size % 2 == 0 && buf_size <= 0xffff);
	u16 *ptr = (u16 *)buf, *end = (u16*)(buf + buf_size);
	spi->ctrl1 = buf_size - 1;
	spi->ctrl0 = spi_mode_base | SPI_XFM_RX | SPI_BHT_APB_16BIT;
	spi->enable = 1;
	u32 ticks_waited = 0;
	while (1) {
		u32 rx_lvl;
		u64 wait_ts = get_timestamp(), end_wait_ts = wait_ts;
		while (!(rx_lvl = spi->rx_fifo_level)) {
			__asm__ volatile("yield");
			end_wait_ts = get_timestamp();
			if (end_wait_ts - wait_ts > 1000 * CYCLES_PER_MICROSECOND) {
				die("SPI timed out\n");
			}
		}
		ticks_waited += end_wait_ts - wait_ts;
		while (rx_lvl--) {
			u16 rx = spi->rx;
			*ptr++ = rx;
			if (ptr >= end) {goto xfer_finished;}
		}
	} xfer_finished:;
	debug("waited %u ticks, %u left\n", ticks_waited, spi->rx_fifo_level);
	spi->enable = 0;
}

static void tx_fast_read_cmd(u32 addr) {
	spi->ctrl0 = spi_mode_base | SPI_XFM_TX | SPI_BHT_APB_8BIT;
	spi->enable = 1; mmio_barrier();
	spi->tx = 0x0b;
	spi->tx = addr >> 16 & 0xff;
	spi->tx = addr >> 8 & 0xff;
	spi->tx = addr & 0xff;
	spi->tx = 0xff; /* dummy byte */
	while (spi->status & 1) {__asm__ volatile("yield");}
	spi->enable = 0;
	mmio_barrier();
}

void spi_read_flash(u8 *buf, u32 buf_size) {
	spi->slave_enable = 1; mmio_barrier();
	tx_fast_read_cmd(0);
	u8 *buf_end = buf + buf_size;
	u8 *pos = buf;
	while (pos < buf_end) {
		u32 read_size = (const char *)buf_end - (const char *)pos;
		if (read_size > SPI_MAX_RECV) {read_size = SPI_MAX_RECV;}
		assert(read_size % 2 == 0);
		spi_recv_fast(pos, read_size);
		pos += read_size;
	}
	spi->slave_enable = 0;
}
