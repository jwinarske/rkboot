/* SPDX-License-Identifier: CC0-1.0 */
#include <main.h>
#include <rkspi.h>
#include <rkspi_regs.h>
#include <rk3399.h>
#include <inttypes.h>
#include <gic.h>
#include <gic_regs.h>
#include <exc_handler.h>

static const u32 spi_mode_base = SPI_MASTER | SPI_CSM_KEEP_LOW | SPI_SSD_FULL_CYCLE | SPI_LITTLE_ENDIAN | SPI_MSB_FIRST | SPI_POLARITY(1) | SPI_PHASE(1) | SPI_DFS_8BIT;

static void spi_recv_fast(volatile struct rkspi *spi, u8 *buf, u32 buf_size) {
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

static void tx_fast_read_cmd(volatile struct rkspi *spi, u32 addr) {
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

static const u16 spi1_intr = 85;
static const u8 irq_threshold = 24;
static const u16 max_transfer = 0xffff / (2 * irq_threshold) * irq_threshold * 2;
struct async_transfer spi1_async;
struct spi_xfer_state {
	u16 this_xfer_items;
} spi1_state;

enum {
	SPI_RX_FULL_INTR = 16,
	SPI_RX_OVERFLOW_INTR = 8,
	SPI_RX_UNDERFLOW_INTR = 4,
	SPI_TX_OVERFLOW_INTR = 2,
	SPI_TX_EMPTY_INTR = 1,
};

static void start_rx_xfer(struct spi_xfer_state *state, struct async_transfer *async, volatile struct rkspi *spi) {
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

static void handle_spi_interrupt(struct spi_xfer_state *state, struct async_transfer *async, volatile struct rkspi *spi) {
	if (!(spi->intr_status & SPI_RX_FULL_INTR)) {
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
		start_rx_xfer(state, async, spi);
	}
}

static void irq_handler() {
	u64 grp0_intid;
	__asm__ volatile("mrs %0, "ICC_IAR0_EL1 : "=r"(grp0_intid));
	u64 sp;
	__asm__("add %0, SP, #0" : "=r"(sp));
	spew("SP=%"PRIx64"\n", sp);
	if (grp0_intid >= 1020 && grp0_intid < 1023) {
		if (grp0_intid == 1020) {
			die("intid1020");
		} else if (grp0_intid == 1021) {
			die("intid1021");
		} else if (grp0_intid == 1022) {
			die("intid1022");
		}
	} else {
		/*debug("IRQ %"PRIu32", GICD_ISPENDR2=0x%"PRIx32"\n", grp0_intid, gic500d->set_pending[2]);*/
		__asm__("msr DAIFClr, #0xf");
		if (grp0_intid == spi1_intr) {
			/*debug("SPI interrupt, buf=0x%zx\n", (size_t)spi1_state.buf);*/
			handle_spi_interrupt(&spi1_state, &spi1_async, spi1);
		} else if (grp0_intid == 1023) {
			puts("spurious interrupt\n");
			u64 iar1;__asm__("mrs %0, "ICC_IAR1_EL1 : "=r"(iar1));
			die("ICC_IAR1_EL1=%"PRIx64"\n", iar1);
		} else {
			die("unexpected group 0 interrupt");
		}
	}
	__asm__ volatile("msr DAIFSet, #0xf;msr "ICC_EOIR0_EL1", %0" : : "r"(grp0_intid));
}

void rkspi_read_flash_poll(volatile struct rkspi *spi, u8 *buf, size_t buf_size, u32 addr) {
	spi->slave_enable = 1;
	tx_fast_read_cmd(spi, addr);
       u8 *buf_end = buf + buf_size;
       u8 *pos = buf;
       while (pos < buf_end) {
               u32 read_size = (const char *)buf_end - (const char *)pos;
               if (read_size > SPI_MAX_RECV) {read_size = SPI_MAX_RECV;}
               assert(read_size % 2 == 0);
               spi_recv_fast(spi, pos, read_size);
               pos += read_size;
       }
       spi->slave_enable = 0;
}

void rkspi_start_irq_flash_read(u32 addr) {
	volatile struct rkspi *spi = spi1;
	assert(spi1_async.total_bytes % 2 == 0 && (u64)spi1_async.buf % 2 == 0);
	assert(!fiq_handler_spx && !irq_handler_spx);
	fiq_handler_spx = irq_handler_spx = irq_handler;
	gicv2_setup_spi(gic500d, spi1_intr, 0x80, 1, IGROUP_0 | INTR_LEVEL);
	spi->intr_mask = SPI_RX_FULL_INTR;
	spi->slave_enable = 1; mmio_barrier();
	tx_fast_read_cmd(spi, addr);
	spi->ctrl0 = spi_mode_base | SPI_XFM_RX | SPI_BHT_APB_16BIT;
	printf("start rxlvl=%"PRIu32", rxthreshold=%"PRIu32" intr_status=0x%"PRIx32"\n", spi->rx_fifo_level, spi->rx_fifo_threshold, spi->intr_raw_status);
	start_rx_xfer(&spi1_state, &spi1_async, spi);
}

void rkspi_end_irq_flash_read() {
	volatile struct rkspi *spi = spi1;
	printf("end rxlvl=%"PRIu32", rxthreshold=%"PRIu32" intr_status=0x%"PRIx32"\n", spi->rx_fifo_level, spi->rx_fifo_threshold, spi->intr_raw_status);
	spi->enable = 0;
	spi->slave_enable = 0;
	spi->intr_raw_status = SPI_RX_FULL_INTR;
	mmio_barrier();
	spi->intr_mask = 0;
	gicv2_disable_spi(gic500d, spi1_intr);
	fiq_handler_spx = irq_handler_spx = 0;
}
