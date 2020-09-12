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
	u32 address_shift;
	u8 *readptr, *invalidate_ptr;
	struct mmc_cardinfo card;
};

static _Alignas(4096) u32 adma2_32_desc[2048] = {};

static enum iost start(struct async_blockdev *dev_, u64 addr, u8 *buf, u8 *buf_end) {
	if (IOST_OK != sdhci_wait_state(&emmc_state, SDHCI_PRESTS_CMD_INHIBIT, 0, USECS(10000), "transfer start ")) {return IOST_GLOBAL;}
	if (emmc->present_state & SDHCI_PRESTS_DAT_INHIBIT) {
		if (!sdhci_try_abort(&emmc_state)) {return IOST_GLOBAL;}
	}
	if (atomic_load_explicit(&emmc_state.int_st, memory_order_acquire) >> 16) {return IOST_GLOBAL;}
	struct emmc_blockdev *dev = (struct emmc_blockdev *)dev_;
	dev->invalidate_ptr = dev->readptr = buf;
	puts("test");
	if (addr >= dev->blk.num_blocks >> dev->address_shift) {return IOST_INVALID;}
	size_t size = buf_end - buf;
	if ((uintptr_t)buf_end > 0xffffffff && size > 0x10000 * ARRAY_SIZE(adma2_32_desc) / 2) {return IOST_INVALID;}
	u8 *old_buf = atomic_exchange_explicit(&emmc_state.sdma_buf, 0, memory_order_acq_rel);
	assert(!old_buf);
	emmc_state.sdma_buffer_size = 4096;
	emmc_state.buf_end = buf_end;
	atomic_store_explicit(&emmc_state.sdma_buf, buf, memory_order_release);
	emmc->normal_int_st = SDHCI_INT_DMA;
	emmc->int_signal_enable = 0xfffff0ff;
	emmc->block_count = size / 512;
	emmc->transfer_mode = SDHCI_TRANSMOD_READ
		| SDHCI_TRANSMOD_DMA
		| SDHCI_TRANSMOD_AUTO_CMD12
		| SDHCI_TRANSMOD_MULTIBLOCK
		| SDHCI_TRANSMOD_BLOCK_COUNT;
	emmc->system_addr = (u32)(uintptr_t)buf;
	return sdhci_submit_cmd(&emmc_state,
		SDHCI_CMD(18) | SDHCI_R1 | SDHCI_CMD_DATA, addr
	);
}

static struct async_buf pump(struct async_transfer *async, size_t consume, size_t min_size) {
	struct emmc_blockdev *blk = (struct emmc_blockdev *)async;
	blk->readptr += consume;
	while (1) {
		struct async_buf res = {blk->readptr, atomic_load_explicit(&emmc_state.sdma_buf, memory_order_acquire)};
		if (!res.end || (size_t)(res.end - res.start) >= min_size) {
			if (!res.end) {res.end = emmc_state.buf_end;}
			if (res.end > blk->invalidate_ptr) {
				invalidate_range(blk->invalidate_ptr, res.end - blk->invalidate_ptr);
				blk->invalidate_ptr = res.end;
			}
			return res;
		}
		 debug("waiting for eMMC: %"PRIx64" end %"PRIx64"\n", (u64)res.end, (u64)emmc_state.buf_end);
		call_cc_ptr2_int1(sched_finish_u8ptr, &emmc_state.sdma_buf, &emmc_state.interrupt_waiters, (uintptr_t)res.end);
		debugs("eMMC woke up\n");
	}
}

static struct emmc_blockdev blk = {
	.blk = {
		.async = {pump},
		.start = start,
		.block_size = 512,
	},
	.readptr = 0,
	.invalidate_ptr = 0,
	.address_shift = 0,
};

static _Bool parse_cardinfo() {
#ifdef DEBUG_MSG
	dump_mem(&blk.card.ext_csd, sizeof(blk.card.ext_csd));
#endif
	if (!mmc_cardinfo_understood(&blk.card)) {
		infos("unknown CSD or EXT_CSD structure version");
		return 0;
	}
	if (blk.card.ext_csd[EXTCSD_REV] < 2) {
		infos("EXT_CSD revision <2, cannot read sector count");
		return 0;
	} else if (blk.card.ext_csd[EXTCSD_REV] >= 6) {
		if (blk.card.ext_csd[EXTCSD_DATA_SECTOR_SIZE] == 1) {
			blk.blk.block_size = 4096;
		} else if (blk.card.ext_csd[EXTCSD_DATA_SECTOR_SIZE] != 0) {
			infos("unknown data sector size");
			return 0;
		}
	}
	blk.blk.num_blocks = mmc_sector_count(&blk.card);
	info("eMMC has %"PRIu64" %"PRIu32"-byte sectors\n", blk.blk.num_blocks, blk.blk.block_size);
	if (~blk.card.rocr & 1 << 30) {blk.address_shift = 9;}
	return 1;
}

void boot_emmc() {
	infos("trying eMMC\n");
	mmu_map_mmio_identity(0xfe330000, 0xfe33ffff);
	mmu_unmap_range((u64)&adma2_32_desc, (u64)&adma2_32_desc + sizeof(adma2_32_desc) - 1);
	dsb_ish();
	mmu_map_range((u64)&adma2_32_desc, (u64)&adma2_32_desc + sizeof(adma2_32_desc) - 1, (u64)&adma2_32_desc, MEM_TYPE_WRITE_THROUGH);
	dsb_ishst();
	
	if (cru[CRU_CLKGATE_CON+6] & 7 << 12) {
		puts("sramstage left eMMC disabled\n");
		boot_medium_exit(BOOT_MEDIUM_EMMC);
		return;
	}
	if (IOST_OK != sdhci_init_late(&emmc_state, &blk.card)) {
		infos("eMMC init failed\n");
		goto shut_down_emmc;
	}
	if (!parse_cardinfo()) {goto out;}

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
