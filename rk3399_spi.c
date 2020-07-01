/* SPDX-License-Identifier: CC0-1.0 */
#include <inttypes.h>
#include <aarch64.h>
#include <rk3399.h>
#include <mmu.h>
#include <rkspi.h>
#include <rkspi_regs.h>
#include <gic.h>
#include <gic_regs.h>
#include <exc_handler.h>
#include <die.h>
#include <log.h>
#include <async.h>
#include <assert.h>

static const u16 spi1_intr = 85;
struct async_transfer spi1_async = {};
struct rkspi_xfer_state spi1_state = {};

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
		spew("IRQ %"PRIu32", GICD_ISPENDR2=0x%"PRIx32"\n", grp0_intid, gic500d->set_pending[2]);
		__asm__("msr DAIFClr, #0xf");
		if (grp0_intid == spi1_intr) {
			spew("SPI interrupt, buf=0x%zx\n", (size_t)spi1_state.buf);
			rkspi_handle_interrupt(&spi1_state, &spi1_async, spi1);
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

void rkspi_start_irq_flash_read(u32 addr) {
	volatile struct rkspi_regs *spi = spi1;
	assert(spi1_async.total_bytes % 2 == 0 && (u64)spi1_async.buf % 2 == 0);
	assert(!fiq_handler_spx && !irq_handler_spx);
	fiq_handler_spx = irq_handler_spx = irq_handler;
	gicv2_setup_spi(gic500d, spi1_intr, 0x80, 1, IGROUP_0 | INTR_LEVEL);
	spi->intr_mask = RKSPI_RX_FULL_INTR;
	spi->slave_enable = 1; dsb_st();
	rkspi_tx_fast_read_cmd(spi, addr);
	spi->ctrl0 = rkspi_mode_base | RKSPI_XFM_RX | RKSPI_BHT_APB_16BIT;
	debug("start rxlvl=%"PRIu32", rxthreshold=%"PRIu32" intr_status=0x%"PRIx32"\n", spi->rx_fifo_level, spi->rx_fifo_threshold, spi->intr_raw_status);
	rkspi_start_rx_xfer(&spi1_state, &spi1_async, spi);
}

void rkspi_end_irq_flash_read() {
	volatile struct rkspi_regs *spi = spi1;
	printf("end rxlvl=%"PRIu32", rxthreshold=%"PRIu32" intr_status=0x%"PRIx32"\n", spi->rx_fifo_level, spi->rx_fifo_threshold, spi->intr_raw_status);
	spi->enable = 0;
	spi->slave_enable = 0;
	spi->intr_raw_status = RKSPI_RX_FULL_INTR;
	dsb_st();
	spi->intr_mask = 0;
	gicv2_disable_spi(gic500d, spi1_intr);
	fiq_handler_spx = irq_handler_spx = 0;
}
