/* SPDX-License-Identifier: CC0-1.0 */
#include <rk3399/dramstage.h>
#include <rk3399/payload.h>
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
#include <cache.h>

static const u16 spi1_intr = 85;
struct rkspi_xfer_state spi1_state = {};

static void start_irq_flash_read(u32 addr, u8 *buf, u8 *end) {
	volatile struct rkspi_regs *spi = spi1;
	size_t total_bytes = end - buf;
	assert(total_bytes % 2 == 0);
	spi->intr_mask = RKSPI_RX_FULL_INTR;
	spi->slave_enable = 1; dsb_st();
	rkspi_tx_fast_read_cmd(spi, addr);
	spi1_state.end = end;
	atomic_store_explicit(&spi1_state.buf, buf, memory_order_release);
	spi->ctrl0 = rkspi_mode_base | RKSPI_XFM_RX | RKSPI_BHT_APB_16BIT;
	debug("start rxlvl=%"PRIu32", rxthreshold=%"PRIu32" intr_status=0x%"PRIx32"\n", spi->rx_fifo_level, spi->rx_fifo_threshold, spi->intr_raw_status);
	atomic_thread_fence(memory_order_release);
	gicv2_setup_spi(gic500d, 85, 0x80, 1, IGROUP_0 | INTR_LEVEL);
	rkspi_start_rx_xfer(&spi1_state, spi, total_bytes);
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

static struct async_buf pump(struct async_transfer *async_, size_t consume, size_t min_size) {
	struct async_dummy *async = (struct async_dummy *)async_;
	async->buf.start += consume;
	u8 *old_end = async->buf.end;
	while (1) {
		u8 *ptr = async->buf.end =atomic_load_explicit(&spi1_state.buf, memory_order_acquire);
		if ((size_t)(ptr - async->buf.start) >= min_size || ptr == spi1_state.end) {break;}
		spew("idle pos=0x%zx rxlvl=%"PRIu32", rxthreshold=%"PRIu32"\n", spi1_state.pos, spi->rx_fifo_level, spi->rx_fifo_threshold);
		call_cc_ptr2_int1(sched_finish_u8ptr, &spi1_state.buf, &spi1_state.waiters, (ureg_t)ptr);
	}
	invalidate_range(old_end, async->buf.end - old_end);
	return async->buf;
}

void boot_spi() {
	u32 spi_load_addr = 256 << 10;

	printf("trying SPI\n");
	if (!wait_for_boot_cue(BOOT_MEDIUM_SPI)) {
		boot_medium_exit(BOOT_MEDIUM_SPI);
		return;
	}
	printf("SPI cued\n");
	u8 *start = blob_buffer.start, *end = blob_buffer.end;
	if ((size_t)(end - start)  > (16 << 20)) {
		end = start +(16 << 20);
	}

	rk3399_spi_setup();
	printf("setup\n");
#if !CONFIG_ELFLOADER_IRQ
	rkspi_read_flash_poll(spi1, start, end - start, spi_load_addr);
	struct async_dummy async = {
		.async = {async_pump_dummy},
		.buf = {start, end}
	};
#else
	struct async_dummy async = {
		.async = {pump},
		.buf = {start, start}
	};
	start_irq_flash_read(spi_load_addr, start, end);
#endif
	printf("start\n");

	if (decompress_payload(&async.async)) {
		boot_medium_loaded(BOOT_MEDIUM_SPI);
	}

#if CONFIG_ELFLOADER_IRQ
	rkspi_end_irq_flash_read();
#endif
	rk3399_spi_teardown();

	printf("had read %zu bytes\n", async.buf.end - start);
	boot_medium_exit(BOOT_MEDIUM_SPI);
}
