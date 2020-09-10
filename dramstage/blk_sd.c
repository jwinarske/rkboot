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
#include <dwmmc_helpers.h>
#include <runqueue.h>
#include <rk3399.h>
#include <async.h>
#include <iost.h>
#include <sd.h>
#include <dump_mem.h>

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
static struct dwmmc_signal_services svc = {
	.set_clock = set_clock,
	.set_signal_voltage = 0,
	.frequencies_supported = 1 << DWMMC_CLOCK_400K | 1 << DWMMC_CLOCK_25M | 1 << DWMMC_CLOCK_50M,
	.voltages_supported = 1 << DWMMC_SIGNAL_3V3,
};

struct dwmmc_state sdmmc_state = {
	.regs = sdmmc,
	.svc = &svc,
	.int_st = DWMMC_INT_DATA_TRANSFER_OVER,
	.cmd_template = DWMMC_CMD_START | DWMMC_CMD_USE_HOLD_REG,
};
#if 0

static const u32 sdmmc_intid = 97;
struct sd_blockdev {
	struct async_blockdev blk;
	u32 address_shift;
	u8 *readptr, *invalidate_ptr;
	struct sd_cardinfo card;
};

static struct async_buf pump(struct async_transfer *async, size_t consume, size_t min_size) {
	struct sd_blockdev *dev = (struct sd_blockdev *)async;
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

static _Bool parse_cardinfo(struct sd_blockdev *dev) {
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
#endif

static _Alignas(4096) struct dwmmc_idmac_desc desc_buf[4096 / sizeof(struct dwmmc_idmac_desc)];

struct sd_blockdev {
	struct async_transfer async;
	struct dwmmc_xfer xfer;
	u8 *consume_ptr, *end_ptr, *next_end_ptr, *stop_ptr;
	u32 next_lba;
};

enum {REQUEST_SIZE = 1 << 18};

static u8 iost_u8[NUM_IOST];

static struct async_buf pump(struct async_transfer *async, size_t consume, size_t min_size) {
	struct sd_blockdev *dev = (struct sd_blockdev *)async;
	assert((size_t)(dev->end_ptr - dev->consume_ptr) >= consume);
	dev->consume_ptr += consume;
	while ((size_t)(dev->end_ptr - dev->consume_ptr) < min_size) {
		if (dev->end_ptr != dev->next_end_ptr) {
			infos("waiting for xfer\n");
			dwmmc_wait_xfer(&sdmmc_state, &dev->xfer);
			invalidate_range(dev->end_ptr, dev->next_end_ptr - dev->end_ptr);
			dev->end_ptr = dev->next_end_ptr;
		}
		if (dev->stop_ptr == dev->end_ptr) {
			return (struct async_buf) {dev->consume_ptr, dev->end_ptr};
		}
		u8 *end = dev->stop_ptr - dev->end_ptr > REQUEST_SIZE ? dev->end_ptr + REQUEST_SIZE: dev->stop_ptr;
		info("starting new xfer LBA 0x%08"PRIx32" buf 0x%"PRIx64"–0x%"PRIx64"\n", dev->next_lba, (u64)dev->end_ptr, (u64)end);
		enum iost res = dwmmc_start_request(&dev->xfer, dev->next_lba);
		if (res != IOST_OK) {return (struct async_buf) {iost_u8 + res, iost_u8};}
		_Bool success = dwmmc_add_phys_buffer(&dev->xfer, plat_virt_to_phys(dev->end_ptr), plat_virt_to_phys(end));
		assert(success);
		res = dwmmc_start_xfer(&sdmmc_state, &dev->xfer);
		info("res%u\n", (unsigned)res);
		if (res != IOST_OK) {return (struct async_buf) {iost_u8 + res, iost_u8};}
		dev->next_lba += REQUEST_SIZE / 512;
		dev->next_end_ptr = end;
	}
	return (struct async_buf) {dev->consume_ptr, dev->end_ptr};
}

void boot_sd() {
	infos("trying SD\n");
	mmu_unmap_range((u64)desc_buf, (u64)desc_buf + 0xfff);
	mmu_map_mmio_identity(0xfe320000, 0xfe320fff);
	mmu_map_range((u64)desc_buf, (u64)desc_buf + 0xfff, (u64)desc_buf, MEM_TYPE_UNCACHED);
	dsb_ishst();
	if (cru[CRU_CLKGATE_CON+12] & 1 << 13) {
		puts("sramstage left SD disabled\n");
		boot_medium_exit(BOOT_MEDIUM_SD);
		return;
	}
	struct sd_cardinfo card;
	if (!dwmmc_init_late(&sdmmc_state, &card)) {
		boot_medium_exit(BOOT_MEDIUM_SD);
		return;
	}
	//if (!parse_cardinfo(&blk)) {goto out;}

	if (!wait_for_boot_cue(BOOT_MEDIUM_SD)) {
		boot_medium_exit(BOOT_MEDIUM_SD);
		return;
	}
	struct sd_blockdev blk = {
		.async = {pump},
		.xfer = {
			.desc = desc_buf,
			.desc_addr = plat_virt_to_phys(desc_buf),
			.desc_cap = ARRAY_SIZE(desc_buf),
		},
		.next_lba = 4 << 20 >> 9,
		.consume_ptr = blob_buffer.start,
		.end_ptr = blob_buffer.start,
		.next_end_ptr = blob_buffer.start,
		.stop_ptr = blob_buffer.end,
	};

	enum iost res = decompress_payload(&blk.async);
	if (res == IOST_OK) {
		boot_medium_loaded(BOOT_MEDIUM_SD);
	} else {goto out;}

	dwmmc_wait_xfer(&sdmmc_state, &blk.xfer);
	printf("had read %zu bytes\n", (size_t)(blk.end_ptr - blob_buffer.start));
out:
	boot_medium_exit(BOOT_MEDIUM_SD);
}
