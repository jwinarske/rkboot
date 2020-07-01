/* SPDX-License-Identifier: CC0-1.0 */
#include <main.h>
#include <rkspi.h>
#include <rkspi_regs.h>
#include <rk3399.h>
#include <inttypes.h>

void rkspi_recv_fast(volatile struct rkspi_regs *spi, u8 *buf, u32 buf_size) {
	assert((u64)buf % 2 == 0 && buf_size % 2 == 0 && buf_size <= 0xffff);
	u16 *ptr = (u16 *)buf, *end = (u16*)(buf + buf_size);
	spi->ctrl1 = buf_size - 1;
	spi->ctrl0 = rkspi_mode_base | RKSPI_XFM_RX | RKSPI_BHT_APB_16BIT;
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

void rkspi_tx_cmd4_dummy1(volatile struct rkspi_regs *spi, u32 cmd) {
	spi->ctrl0 = rkspi_mode_base | RKSPI_XFM_TX | RKSPI_BHT_APB_8BIT;
	spi->enable = 1; mmio_barrier();
	spi->tx = cmd >> 24;
	spi->tx = cmd >> 16 & 0xff;
	spi->tx = cmd >> 8 & 0xff;
	spi->tx = cmd & 0xff;
	spi->tx = 0xff; /* dummy byte */
	while (spi->status & 1) {__asm__ volatile("yield");}
	spi->enable = 0;
}

void rkspi_tx_fast_read_cmd(volatile struct rkspi_regs *spi, u32 addr) {
	assert(!(addr >> 24));
	rkspi_tx_cmd4_dummy1(spi, (u32)0x0b << 24 | addr);
}

static const u8 irq_threshold = 24;
static const u16 max_transfer = 0xffff / (2 * irq_threshold) * irq_threshold * 2;

void rkspi_start_rx_xfer(struct rkspi_xfer_state *state, struct async_transfer *async, volatile struct rkspi_regs *spi) {
	size_t bytes = async->total_bytes - async->pos;
	assert(bytes % 2 == 0);
	if (bytes/2 < irq_threshold) {
		spi->rx_fifo_threshold = bytes / 2 - 1;
	} else {
		if (bytes > max_transfer) {
			bytes = max_transfer;
		} else {
			bytes = bytes / (irq_threshold * 2) * irq_threshold * 2;
		}
		spi->rx_fifo_threshold = irq_threshold - 1;
	}
	debug("starting %"PRIu32"-byte RX transfer\n", bytes);
	state->this_xfer_items = bytes / 2;
	spi->ctrl1 = bytes - 1;
	spi->enable = 1;
}

void rkspi_handle_interrupt(struct rkspi_xfer_state *state, struct async_transfer *async, volatile struct rkspi_regs *spi) {
	if (!(spi->intr_status & RKSPI_RX_FULL_INTR)) {
		die("unexpected SPI interrupt status %"PRIx32"\n", spi->intr_status);
	}
	assert(async->buf);
	u8 read_items = state->this_xfer_items >= irq_threshold ? irq_threshold : state->this_xfer_items;
	size_t pos = async->pos;
	assert(read_items * 2 <= async->total_bytes - pos);
	for_range(i, 0, read_items) {
		*(u16*)(async->buf + pos + 2 * i) = spi->rx;
	}
	pos += read_items * 2;
	async->pos = pos;
	state->this_xfer_items -= read_items;
	spew("pos=0x%zx, this_xfer=%"PRIu16"\n", pos, state->this_xfer_items);
	if (!state->this_xfer_items) {
		assert(spi->rx_fifo_level == 0);
		if (pos >= async->total_bytes) {return;}
		spi->enable = 0;
		debugs("starting next transfer\n");
		rkspi_start_rx_xfer(state, async, spi);
	}
}

void rkspi_read_flash_poll(volatile struct rkspi_regs *spi, u8 *buf, size_t buf_size, u32 addr) {
	spi->slave_enable = 1;
	rkspi_tx_fast_read_cmd(spi, addr);
       u8 *buf_end = buf + buf_size;
       u8 *pos = buf;
       while (pos < buf_end) {
               u32 read_size = (const char *)buf_end - (const char *)pos;
               if (read_size > RKSPI_MAX_RECV) {read_size = RKSPI_MAX_RECV;}
               assert(read_size % 2 == 0);
               rkspi_recv_fast(spi, pos, read_size);
               pos += read_size;
       }
       spi->slave_enable = 0;
}
