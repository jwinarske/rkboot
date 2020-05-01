#include <main.h>
#include <rk-spi.h>
#include <rk3399.h>
#include <inttypes.h>
#include <gic_regs.h>

static const u32 spi_mode_base = SPI_MASTER | SPI_CSM_KEEP_LOW | SPI_SSD_FULL_CYCLE | SPI_LITTLE_ENDIAN | SPI_MSB_FIRST | SPI_POLARITY(1) | SPI_PHASE(1) | SPI_DFS_8BIT;

extern void (*volatile fiq_handler_spx)();
extern void (*volatile irq_handler_spx)();

static void UNUSED spi_recv_fast(u8 *buf, u32 buf_size) {
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

static const u16 spi1_intr = 85;
static const u8 irq_threshold = 24;
static const u16 max_transfer = 0xffff / (2 * irq_threshold) * irq_threshold * 2;
struct spi_xfer_state {
	u8 *buf;
	_Atomic(size_t) pos;
	size_t total_bytes;
	u16 this_xfer_items;
} spi1_state;

enum {
	SPI_RX_FULL_INTR = 16,
	SPI_RX_OVERFLOW_INTR = 8,
	SPI_RX_UNDERFLOW_INTR = 4,
	SPI_TX_OVERFLOW_INTR = 2,
	SPI_TX_EMPTY_INTR = 1,
};

static void start_rx_xfer(struct spi_xfer_state *state, volatile struct spi *spi) {
	size_t bytes = state->total_bytes - state->pos;
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

static void handle_spi_interrupt(struct spi_xfer_state *state, volatile struct spi *spi) {
	if (!(spi->intr_status & SPI_RX_FULL_INTR)) {
		die("unexpected SPI interrupt status %"PRIx32"\n", spi->intr_status);
	}
	assert(state->buf);
	u8 read_items = state->this_xfer_items >= irq_threshold ? irq_threshold : state->this_xfer_items;
	size_t pos = state->pos;
	assert(read_items * 2 <= state->total_bytes - pos);
	for_range(i, 0, read_items) {
		*(u16*)(state->buf + pos + 2 * i) = spi->rx;
	}
	pos += read_items * 2;
	state->pos = pos;
	state->this_xfer_items -= read_items;
	spew("pos=0x%zx, this_xfer=%"PRIu16"\n", pos, state->this_xfer_items);
	if (!state->this_xfer_items) {
		assert(spi->rx_fifo_level == 0);
		if (pos >= state->total_bytes) {return;}
		spi->enable = 0;
		debugs("starting next transfer\n");
		start_rx_xfer(state, spi);
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
			handle_spi_interrupt(&spi1_state, spi);
		} else if (grp0_intid == 1023) {
			puts("spurious interrupt\n");
			u64 iar1;__asm__("mrs %0, "ICC_IAR1_EL1 : "=r"(iar1));
			die("ICC_IAR1_EL1=%"PRIx64"\n", iar1);
		} else {
			die("unexpected group 0 interrupt");
		}
	}
	__asm__ volatile("msr "ICC_EOIR0_EL1", %0" : : "r"(grp0_intid));
}

enum {
	IGROUP_0 = 0,
	IGROUP_NS1 = 1,
	IGROUP_S1 = 2,
	INTR_LEVEL = 0,
	INTR_EDGE = 2 << 2,
};

void gicv2_setup_spi(volatile struct gic_distributor *dist, u16 intr, u8 priority, u8 targets, u32 flags) {
	assert(intr >= 32 && intr < 1020);
	u32 bit = 1 << (intr % 32);
	dist->disable[intr / 32] = bit;
	dist->clear_pending[intr / 32] = bit;
	dist->deactivate[intr / 32] = bit;
	while (dist->control & GICD_CTLR_RWP) {__asm__("yield");}
	dist->targets[intr] = targets;
	dist->priority[intr] = priority;
	if (flags & 1) {
		dist->group[intr / 32] |= bit;
	} else {
		dist->group[intr / 32] &= ~bit;
	}
	if (flags & 2) {
		dist->group_modifier[intr / 32] |= bit;
	} else {
		dist->group_modifier[intr / 32] &= ~bit;
	}
	u32 tmp = dist->configuration[intr / 16];
	u16 pos =intr % 16 * 2;
	dist->configuration[intr / 16] = (tmp & ~((u32)32 << pos)) | (flags >> 2 & 3) << pos;
	while (dist->control & GICD_CTLR_RWP) {__asm__("yield");}
	dist->enable[intr / 32] = bit;
}

void gicv3_per_cpu_setup(volatile struct gic_redistributor *redist);
void gicv3_per_cpu_teardown(volatile struct gic_redistributor *redist);

void spi_read_flash(u8 *buf, u32 buf_size) {
	assert(buf_size % 2 == 0);
	u32 ctlr = gic500d->control;
	assert((ctlr & (GICD_CTLR_ARE_NS | GICD_CTLR_ARE_S)) == 0);
	gic500d->control = GICD_CTLR_EnableGrp0 | GICD_CTLR_EnableGrp1S;
	assert(!fiq_handler_spx && !irq_handler_spx);
	fiq_handler_spx = irq_handler_spx = irq_handler;
	gicv3_per_cpu_setup(gic500r);
	gicv2_setup_spi(gic500d, spi1_intr, 0x80, 1, IGROUP_0 | INTR_LEVEL);
	__asm__("isb");
	spi1_state.buf = buf;
	spi1_state.total_bytes = buf_size;
	spi1_state.pos = 0;
	spi->intr_mask = SPI_RX_FULL_INTR;
	spi->slave_enable = 1; mmio_barrier();
	tx_fast_read_cmd(0);
	spi->ctrl0 = spi_mode_base | SPI_XFM_RX | SPI_BHT_APB_16BIT;
	printf("start rxlvl=%"PRIu32", rxthreshold=%"PRIu32" intr_status=0x%"PRIx32"\n", spi->rx_fifo_level, spi->rx_fifo_threshold, spi->intr_raw_status);
	start_rx_xfer(&spi1_state, spi);
	while (spi1_state.pos < spi1_state.total_bytes) {
		debug("idle pos=0x%zx rxlvl=%"PRIu32", rxthreshold=%"PRIu32"\n", spi1_state.pos, spi->rx_fifo_level, spi->rx_fifo_threshold);
		__asm__("wfi");
	}
	printf("end rxlvl=%"PRIu32", rxthreshold=%"PRIu32" intr_status=0x%"PRIx32"\n", spi->rx_fifo_level, spi->rx_fifo_threshold, spi->intr_raw_status);
	spi->slave_enable = 0;
	spi->intr_mask = 0;
	gic500d->disable[spi1_intr / 32] = 1 << (spi1_intr % 32);
	gicv3_per_cpu_teardown(gic500r);
	fiq_handler_spx = irq_handler_spx = 0;
}
