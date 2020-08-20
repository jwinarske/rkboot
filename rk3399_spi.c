/* SPDX-License-Identifier: CC0-1.0 */
#include <rk3399/dramstage.h>
#include <inttypes.h>
#include <stdatomic.h>

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
#include "rk3399_spi.h"
#include <dump_mem.h>

static const u16 spi1_intr = 85;
struct async_transfer spi1_async = {};
struct rkspi_xfer_state spi1_state = {};

void rkspi_start_irq_flash_read(u32 addr) {
	volatile struct rkspi_regs *spi = spi1;
	assert(spi1_async.total_bytes % 2 == 0 && (u64)spi1_async.buf % 2 == 0);
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
	gicv2_wait_disabled(gic500d);
}

void boot_spi() {
	u32 spi_load_addr = 256 << 10;

	printf("trying SPI\n");
	if (!wait_for_boot_cue(BOOT_MEDIUM_SPI)) {
		boot_medium_exit(BOOT_MEDIUM_SPI);
		return;
	}
	printf("SPI cued\n");
	struct async_transfer *async = &spi1_async;
	init_blob_buffer(async);
	if (async->total_bytes > (16 << 20)) {
		async->total_bytes = 16 << 20;
	}
	atomic_thread_fence(memory_order_release);
	gicv2_setup_spi(gic500d, 85, 0x80, 1, IGROUP_0 | INTR_LEVEL);

	rk3399_spi_setup();
	printf("setup\n");
#if !CONFIG_ELFLOADER_IRQ
	rkspi_read_flash_poll(spi1, async->buf, async->total_bytes, spi_load_addr);
	async->pos = async->total_bytes;
#else
	rkspi_start_irq_flash_read(spi_load_addr);
#endif
	printf("start\n");

	if (decompress_payload(async)) {boot_medium_loaded(BOOT_MEDIUM_SPI);}

#if CONFIG_ELFLOADER_IRQ
	rkspi_end_irq_flash_read();
#endif
	rk3399_spi_teardown();

	printf("had read %zu bytes\n", async->pos);
	boot_medium_exit(BOOT_MEDIUM_SPI);
}
