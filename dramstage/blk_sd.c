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
	static volatile u32 *const cru = regmap_cru;
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
	.regs = regmap_sdmmc,
	.svc = &svc,
	.int_st = DWMMC_INT_DATA_TRANSFER_OVER,
	.cmd_template = DWMMC_CMD_START | DWMMC_CMD_USE_HOLD_REG,
};

struct sd_blockdev {
	struct async_blockdev blk;
	struct dwmmc_xfer xfer;
	u8 *consume_ptr, *end_ptr, *next_end_ptr, *stop_ptr;
	u32 next_lba;
	unsigned address_shift : 4;
	struct sd_cardinfo card;
};

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

static _Alignas(4096) struct dwmmc_idmac_desc desc_buf[4096 / sizeof(struct dwmmc_idmac_desc)];

enum {REQUEST_SIZE = 1 << 19};

static u8 iost_u8[NUM_IOST];

static enum iost wait_xfer(struct sd_blockdev *dev) {
	debugs("waiting for xfer\n");
	enum iost res = dwmmc_wait_xfer(&sdmmc_state, &dev->xfer);
	if (res != IOST_OK) {return res;}
	invalidate_range(dev->end_ptr, dev->next_end_ptr - dev->end_ptr);
	dev->end_ptr = dev->next_end_ptr;
	return IOST_OK;
}

static struct async_buf pump(struct async_transfer *async, size_t consume, size_t min_size) {
	struct sd_blockdev *dev = (struct sd_blockdev *)async;
	assert((size_t)(dev->end_ptr - dev->consume_ptr) >= consume);
	dev->consume_ptr += consume;
	while ((size_t)(dev->end_ptr - dev->consume_ptr) < min_size) {
		if (dev->end_ptr != dev->next_end_ptr) {
			enum iost res = wait_xfer(dev);
			if (res != IOST_OK) {return (struct async_buf) {iost_u8 + res, iost_u8};}
		}
		if (dev->stop_ptr == dev->end_ptr) {
			return (struct async_buf) {dev->consume_ptr, dev->end_ptr};
		}
		u8 *end = dev->stop_ptr - dev->end_ptr > REQUEST_SIZE ? dev->end_ptr + REQUEST_SIZE: dev->stop_ptr;
		debug("starting new xfer LBA 0x%08"PRIx32" buf 0x%"PRIx64"–0x%"PRIx64"\n", dev->next_lba, (u64)dev->end_ptr, (u64)end);
		enum iost res = dwmmc_start_request(&dev->xfer, dev->next_lba);
		if (res != IOST_OK) {return (struct async_buf) {iost_u8 + res, iost_u8};}
		_Bool success = dwmmc_add_phys_buffer(&dev->xfer, plat_virt_to_phys(dev->end_ptr), plat_virt_to_phys(end));
		assert(success);
		res = dwmmc_start_xfer(&sdmmc_state, &dev->xfer);
		if (res != IOST_OK) {return (struct async_buf) {iost_u8 + res, iost_u8};}
		dev->next_lba += REQUEST_SIZE / dev->blk.block_size;
		dev->next_end_ptr = end;
	}
	return (struct async_buf) {dev->consume_ptr, dev->end_ptr};
}

static enum iost start(struct async_blockdev *dev_, u64 addr, u8 *buf, u8 *buf_end) {
	struct sd_blockdev *dev = (struct sd_blockdev *)dev_;
	if (buf_end < buf
		|| (size_t)(buf_end - buf) % dev->blk.block_size != 0
		|| addr >= dev->blk.num_blocks
	) {return IOST_INVALID;}
	enum iost res = wait_xfer(dev);
	if (res != IOST_OK) {return res;}
	dev->next_lba = (u32)addr;
	dev->consume_ptr = dev->end_ptr = dev->next_end_ptr = buf;
	dev->stop_ptr = buf_end;
	return IOST_OK;
}

void boot_sd() {
	infos("trying SD\n");
	arch_flush_writes();
	mmu_unmap_range((u64)desc_buf, (u64)desc_buf + 0xfff);
	mmu_flush();
	mmu_map_range((u64)desc_buf, (u64)desc_buf + 0xfff, (u64)desc_buf, MEM_TYPE_UNCACHED);
	mmu_flush();
	flush_range(desc_buf, sizeof(desc_buf));
	arch_flush_writes();

	if (regmap_cru[CRU_CLKGATE_CON+12] & 1 << 13) {
		puts("sramstage left SD disabled\n");
		goto out;
	}
	struct sd_blockdev blk = {
		.blk = {
			.async = {pump},
			.start = start,
		},
		.xfer = {
			.desc = desc_buf,
			.desc_addr = plat_virt_to_phys(desc_buf),
			.desc_cap = ARRAY_SIZE(desc_buf),
		},
	};
	if (!dwmmc_init_late(&sdmmc_state, &blk.card)) {goto shut_down_mshc;}
	if (!parse_cardinfo(&blk)) {goto out;}

	if (!wait_for_boot_cue(BOOT_MEDIUM_SD)) {
		boot_medium_exit(BOOT_MEDIUM_SD);
		return;
	}

	enum iost res = boot_blockdev(&blk.blk);
	if (res == IOST_OK) {
		boot_medium_loaded(BOOT_MEDIUM_SD);
	} else if (res != IOST_GLOBAL) {
		goto out;
	} else {goto shut_down_mshc;}

	dwmmc_wait_xfer(&sdmmc_state, &blk.xfer);
	printf("had read %zu bytes\n", (size_t)(blk.end_ptr - blob_buffer.start));
	goto out;
shut_down_mshc:
	infos("hardware in unknown state, shutting down the MSHC");
	regmap_cru[CRU_CLKGATE_CON+12] = SET_BITS16(1, 1) << 13;
out:
	boot_medium_exit(BOOT_MEDIUM_SD);
}
