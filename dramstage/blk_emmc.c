/* SPDX-License-Identifier: CC0-1.0 */
#include <rk3399/dramstage.h>
#include <rk3399/payload.h>
#include <inttypes.h>
#include <assert.h>
#include <stdatomic.h>

#include <log.h>
#include <mmu.h>
#include <aarch64.h>
#include <mmc.h>
#include <rk3399.h>
#include <sdhci.h>
#include <sdhci_regs.h>
#include <sdhci_helpers.h>
#include <gic.h>
#include <timer.h>
#include <cache.h>
#include <die.h>
#include <async.h>
#include <iost.h>
#include <dump_mem.h>

static void UNUSED mmc_print_csd_cid(u32 *cxd) {
	if ((cxd[3] >> 30) == 0) {
		puts("unknown CSD structure 0\n");
		return;
	}
	u32 version = cxd[3] >> 26 & 15;
	if (version < 2) {
		printf("unimplemented: old MMC version %"PRIu32" CxD\n", version);
		return;
	} else if (version < 5) {
		
	} else {
		printf("unknown CxD version %"PRIu32"\n", version);
		return;
	}
}

extern struct sdhci_phy emmc_phy;
struct sdhci_state emmc_state = {
	.regs = emmc,
	.phy = &emmc_phy,
};

struct emmc_blockdev {
	struct async_blockdev blk;
	struct sdhci_xfer xfer;
	u8 *consume_ptr, *end_ptr, *next_end_ptr, *stop_ptr;
	u32 next_lba;
	unsigned address_shift : 4;
	struct mmc_cardinfo card;
};

static u8 iost_u8[NUM_IOST];

static enum iost wait_xfer(struct emmc_blockdev *dev) {
	debugs("waiting for xfer\n");
	enum iost res = sdhci_wait_xfer(&emmc_state, &dev->xfer);
	if (res != IOST_OK) {return res;}
	invalidate_range(dev->end_ptr, dev->next_end_ptr - dev->end_ptr);
	dev->end_ptr = dev->next_end_ptr;
	return IOST_OK;
}

enum {REQUEST_SIZE = 1 << 20};

static struct async_buf pump(struct async_transfer *async, size_t consume, size_t min_size) {
	struct emmc_blockdev *dev = (struct emmc_blockdev *)async;
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
		if (!sdhci_reset_xfer(&dev->xfer)) {return (struct async_buf) {iost_u8 + IOST_INVALID, iost_u8};}
		_Bool success = sdhci_add_phys_buffer(&dev->xfer, plat_virt_to_phys(dev->end_ptr), plat_virt_to_phys(end));
		assert(success);
		enum iost res = sdhci_start_xfer(&emmc_state, &dev->xfer, dev->next_lba);
		if (res != IOST_OK) {return (struct async_buf) {iost_u8 + res, iost_u8};}
		dev->next_lba += REQUEST_SIZE / dev->blk.block_size;
		dev->next_end_ptr = end;
	}
	return (struct async_buf) {dev->consume_ptr, dev->end_ptr};
}

static enum iost start(struct async_blockdev *dev_, u64 addr, u8 *buf, u8 *buf_end) {
	struct emmc_blockdev *dev = (struct emmc_blockdev *)dev_;
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

static _Alignas(4096) struct sdhci_adma2_desc8 desc_buf[4096 / sizeof(struct sdhci_adma2_desc8)];

static _Bool parse_cardinfo(struct emmc_blockdev *dev) {
#ifdef DEBUG_MSG
	dump_mem(&dev->card.ext_csd, sizeof(dev->card.ext_csd));
#endif
	if (!mmc_cardinfo_understood(&dev->card)) {
		infos("unknown CSD or EXT_CSD structure version");
		return 0;
	}
	if (dev->card.ext_csd[EXTCSD_REV] < 2) {
		infos("EXT_CSD revision <2, cannot read sector count");
		return 0;
	} else if (dev->card.ext_csd[EXTCSD_REV] >= 6) {
		if (dev->card.ext_csd[EXTCSD_DATA_SECTOR_SIZE] == 1) {
			dev->blk.block_size = 4096;
		} else if (dev->card.ext_csd[EXTCSD_DATA_SECTOR_SIZE] != 0) {
			infos("unknown data sector size");
			return 0;
		} else {
			dev->blk.block_size = 512;
		}
	} else {
		dev->blk.block_size = 512;
	}
	dev->blk.num_blocks = mmc_sector_count(&dev->card);
	info("eMMC has %"PRIu64" %"PRIu32"-byte sectors\n", dev->blk.num_blocks, dev->blk.block_size);
	if (~dev->card.rocr & 1 << 30) {dev->address_shift = 9;}
	return 1;
}

void boot_emmc() {
	infos("trying eMMC\n");
	mmu_map_mmio_identity(0xfe330000, 0xfe33ffff);
	mmu_unmap_range((u64)&desc_buf, (u64)&desc_buf + sizeof(desc_buf) - 1);
	dsb_ish();
	mmu_map_range((u64)&desc_buf, (u64)&desc_buf + sizeof(desc_buf) - 1, (u64)&desc_buf, MEM_TYPE_UNCACHED);
	dsb_ishst();
	
	if (cru[CRU_CLKGATE_CON+6] & 7 << 12) {
		puts("sramstage left eMMC disabled\n");
		boot_medium_exit(BOOT_MEDIUM_EMMC);
		return;
	}

	struct emmc_blockdev blk = {
		.blk = {
			.async = {pump},
			.start = start,
		},
		.xfer = {
			.desc8 = desc_buf,
			.desc_addr = plat_virt_to_phys(&desc_buf),
			.desc_cap = ARRAY_SIZE(desc_buf),
		},
	};
	if (IOST_OK != sdhci_init_late(&emmc_state, &blk.card)) {
		infos("eMMC init failed\n");
		goto shut_down_emmc;
	}
	if (!parse_cardinfo(&blk)) {goto out;}

	infos("eMMC init done\n");
	if (!wait_for_boot_cue(BOOT_MEDIUM_EMMC)) {goto out;}
	enum iost res = boot_blockdev(&blk.blk);
	if (res == IOST_OK) {boot_medium_loaded(BOOT_MEDIUM_EMMC);}
	if (res == IOST_GLOBAL) {goto shut_down_emmc;}

	if (sdhci_try_abort(&emmc_state)) {
		infos("eMMC transfers ended\n");
		boot_medium_exit(BOOT_MEDIUM_EMMC);
		return;
	}
	infos("eMMC abort failed, shutting down the controller\n");
shut_down_emmc:
	emmc->power_control = 0;
	gicv2_disable_spi(gic500d, 43);
	gicv2_wait_disabled(gic500d);
	/* shut down phy */
	grf[GRF_EMMCPHY_CON+6] = SET_BITS16(2, 0);
	dsb_st();	/* GRF_EMMCPHY_CONx seem to be in the clk_emmc domain. ensure completion to avoid hangs */
	/* gate eMMC clocks */
	cru[CRU_CLKGATE_CON+6] = SET_BITS16(3, 7) << 12;
	cru[CRU_CLKGATE_CON+32] = SET_BITS16(1, 1) << 8 | SET_BITS16(1, 1) << 10;
out:
	boot_medium_exit(BOOT_MEDIUM_EMMC);
	return;
}
