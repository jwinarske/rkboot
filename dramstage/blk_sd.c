/* SPDX-License-Identifier: CC0-1.0 */
#include <rk3399/dramstage.h>
#include <rk3399/payload.h>
#include <assert.h>
#include <stdatomic.h>

#include <mmu.h>
#include <cache.h>
#include <log.h>
#include <arch.h>
#include <die.h>
#include <exc_handler.h>
#include <gic.h>
#include <gic_regs.h>
#include <dwmmc.h>
#include <dwmmc_dma.h>
#include <dwmmc_helpers.h>
#include <runqueue.h>
#include <rk3399.h>
#include <async.h>
#include <iost.h>
#include <sd.h>

static _Bool set_clock(struct dwmmc_signal_services UNUSED *svc, enum dwmmc_clock clk) {
	switch (clk) {
	case DWMMC_CLOCK_400K:
		/* clk_sdmmc = 24 MHz / 30 = 800 kHz */
		cru[CRU_CLKSEL_CON + 16] = SET_BITS16(3, 5) << 8 | SET_BITS16(7, 29);
		break;
	case DWMMC_CLOCK_25M:
		/* clk_sdmmc = CPLL/16 = 50 MHz */
		cru[CRU_CLKSEL_CON + 16] = SET_BITS16(3, 0) << 8 | SET_BITS16(7, 15);
		break;
	case DWMMC_CLOCK_50M:
		/* clk_sdmmc = CPLL/8 = 100 MHz */
		cru[CRU_CLKSEL_CON + 16] = SET_BITS16(3, 0) << 8 | SET_BITS16(7, 7);
		break;
	default: return 0;
	}
	arch_flush_writes();
	return 1;
}

static const u32 sdmmc_intid = 97;

struct dwmmc_dma_state sdmmc_dma_state = {};

struct sd_blockdev {
	struct async_blockdev blk;
	u32 address_shift;
	u8 *readptr, *invalidate_ptr;
	struct sd_cardinfo card;
};

static struct async_buf pump(struct async_transfer *async_, size_t consume, size_t min_size) {
	struct async_dummy *async = (struct async_dummy *)async_;
	async->buf.start += consume;
	u8 *old_end = async->buf.end;
	while (1) {
		u32 val =atomic_load_explicit(&sdmmc_dma_state.finished, memory_order_acquire);
		async->buf.end = (u8 *)(uintptr_t)val;
		size_t size = async->buf.end - async->buf.start;
		if (size >= min_size || val == sdmmc_dma_state.end) {break;}
#ifdef SPEW_MSG
		dwmmc_print_status(sdmmc, "idle ");
#endif
		call_cc_ptr2_int2(sched_finish_u32, &sdmmc_dma_state.finished, &sdmmc_dma_state.waiters, ~(u32)0, val);
	}
	invalidate_range(old_end, async->buf.end - old_end);
	return async->buf;
}

static void UNUSED rk3399_sdmmc_start_irq_read(u32 sector, u8 *start, u8 *end) {
	dwmmc_setup_dma(sdmmc);
	dwmmc_init_dma_state(&sdmmc_dma_state);
	sdmmc_dma_state.end = (u32)(uintptr_t)end;
	sdmmc_dma_state.buf = (u32)(uintptr_t)start;
	atomic_store_explicit(&sdmmc_dma_state.finished, (u32)(uintptr_t)start, memory_order_release);
	sdmmc->desc_list_base = (u32)(uintptr_t)&sdmmc_dma_state.desc;
	gicv2_setup_spi(gic500d, sdmmc_intid, 0x80, 1, IGROUP_0 | INTR_LEVEL);
	sdmmc->ctrl |= DWMMC_CTRL_INT_ENABLE;
	size_t total_bytes = end - start;
	printf("%"PRIx64"–%"PRIx64"\n", (u64)start, (u64)end);
	assert(total_bytes % 512 == 0);
	assert(total_bytes <= 0xffffffff);
	sdmmc->blksiz = 512;
	sdmmc->bytcnt = total_bytes;
	enum dwmmc_status st = dwmmc_wait_cmd_done(sdmmc, 18 | DWMMC_R1 | DWMMC_CMD_DATA_EXPECTED, sector, 1000);
	dwmmc_check_ok_status(sdmmc, st, "CMD18 (READ_MULTIPLE_BLOCK)");
}
static void UNUSED rk3399_sdmmc_end_irq_read() {
	gicv2_disable_spi(gic500d, sdmmc_intid);
	/* make sure the iDMAC is suspended before we hand off */
    u32 tmp;
	while (((tmp = sdmmc->idmac_status) & (DWMMC_IDMAC_INTMASK_ABNORMAL | DWMMC_IDMAC_INT_CARD_ERROR)) == 0 && (tmp >> 13 & 15) > 1) {
		__asm__("yield");
	}
}

_Bool parse_cardinfo(struct sd_blockdev *dev) {
	const u32 *csd = dev->card.csd;
	if (csd[3] >> 30 != 1) {
		infos("unrecognized CSD structure version\n");
		return 0;
	}
	dev->address_shift = 0;
	dev->blk.block_size = 512;
	dev->blk.num_blocks = (u64)(csd[1] >> 16 | (csd[2] << 16 & 0x3f0000)) * 1024 + 1024;
	info("SD has %"PRIu64" %"PRIu32"-byte sectors\n", dev->blk.num_blocks, dev->blk.block_size);
	return 1;
}

void boot_sd() {
	infos("trying SD");
	mmu_map_mmio_identity(0xfe320000, 0xfe320fff);
	dsb_ishst();
	if (cru[CRU_CLKGATE_CON+12] & 1 << 13) {
		puts("sramstage left SD disabled\n");
		boot_medium_exit(BOOT_MEDIUM_SD);
		return;
	}
	struct dwmmc_signal_services svc = {
		.set_clock = set_clock,
		.set_signal_voltage = 0,
		.frequencies_supported = 1 << DWMMC_CLOCK_400K | 1 << DWMMC_CLOCK_25M | 1 << DWMMC_CLOCK_50M,
		.voltages_supported = 1 << DWMMC_SIGNAL_3V3,
	};
	struct sd_blockdev blk = {
		.blk = {
			.async = {pump},
			.num_blocks = 0,
			.block_size = 0,
			.start = 0,
		},
		.address_shift = 0,
		.readptr = 0,
		.invalidate_ptr = 0,
		/* don't init cardinfo */
	};
	if (!dwmmc_init_late(sdmmc, &svc, &blk.card)) {
		boot_medium_exit(BOOT_MEDIUM_SD);
		return;
	}
	if (!parse_cardinfo(&blk)) {goto out;}

	if (!wait_for_boot_cue(BOOT_MEDIUM_SD)) {
		boot_medium_exit(BOOT_MEDIUM_SD);
		return;
	}
	static const u32 sd_start_sector = 4 << 11; /* offset 4 MiB */

#if !CONFIG_ELFLOADER_IRQ
	dwmmc_read_poll_dma(sdmmc, sd_start_sector, blob_buffer.start, blob_buffer.end - blob_buffer.start);
	struct async_dummy async = {
		.async = {async_pump_dummy},
		.buf = {blob_buffer.start, blob_buffer.end},
	};
#else
	struct async_dummy async = {
		.async = {pump},
		.buf = {blob_buffer.start, blob_buffer.start},
	};
	rk3399_sdmmc_start_irq_read(sd_start_sector, blob_buffer.start, blob_buffer.end);
#endif

	if (IOST_OK == decompress_payload(&async.async)) {
		boot_medium_loaded(BOOT_MEDIUM_SD);
	}

#if CONFIG_ELFLOADER_IRQ
	rk3399_sdmmc_end_irq_read();
#endif

	printf("had read %zu bytes\n", (size_t)(async.buf.end - blob_buffer.start));
out:
	boot_medium_exit(BOOT_MEDIUM_SD);
}
