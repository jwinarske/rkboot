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
#include <wait_register.h>
#include <cache.h>
#include <dump_mem.h>
#include <die.h>
#include <async.h>
#include <byteorder.h>
#include <iost.h>

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

static void write_cxd(u32 *dest, u32 a, u32 b, u32 c, u32 d) {
	dest[0] = d << 8 | c >> 24;
	dest[1] = c << 8 | b >> 24;
	dest[2] = b << 8 | a >> 24;
	dest[3] = a << 8;
}

enum iost sdhci_init_late(volatile struct sdhci_regs *sdhci, struct sdhci_state *st, struct sdhci_phy *phy, struct mmc_cardinfo *card) {
	timestamp_t start = get_timestamp();
	info("[%"PRIuTS"] SDHCI init\n", start);
	st->version = sdhci->sdhci_version;
	st->caps = (u64)sdhci->capabilities[1] << 32 | sdhci->capabilities[0];
	assert(st->version >= 2);
	u32 ocr;
	enum iost res;
	while (1) {
		if (IOST_OK != (res = sdhci_wait_state(sdhci, st, SDHCI_PRESTS_CMD_INHIBIT, 0, USECS(1000), "CMD1"))) {return res;}
		ocr = sdhci->resp[0];
		printf("OCR: %08"PRIx32"\n", ocr);
		if (ocr & 1 << 31) {break;}
		if (get_timestamp() - start > USECS(1000000)) {
			infos("eMMC init timeout\n");
			return 0;
		}
		usleep(1000);
		if (IOST_OK != (res = sdhci_submit_cmd(sdhci, st, SDHCI_CMD(1) | SDHCI_R3, 0x40ff8000))) {return res;}
	}
	card->rocr = ocr;
	if (~ocr & 1 << 30) {
		infos("eMMC does not support sector addressing\n");
		return IOST_INVALID;
	}
	if (IOST_OK != sdhci_submit_cmd(sdhci, st, SDHCI_CMD(2) | SDHCI_R2, 0)) {return IOST_LOCAL;}
	if (IOST_OK != sdhci_wait_state(sdhci, st, SDHCI_PRESTS_CMD_INHIBIT, 0, USECS(10000), "CMD2")) {return IOST_LOCAL;}
	write_cxd(card->cxd + 4, sdhci->resp[0], sdhci->resp[1], sdhci->resp[2], sdhci->resp[3]);
	info("CID %08"PRIx32" %08"PRIx32" %08"PRIx32" %08"PRIx32"\n",
		card->cxd[4], card->cxd[5], card->cxd[6], card->cxd[7]
	);
	if (IOST_OK != sdhci_submit_cmd(sdhci, st, SDHCI_CMD(3) | SDHCI_R1, 2 << 16)) {return IOST_LOCAL;}
	if (IOST_OK != sdhci_wait_state(sdhci, st, SDHCI_PRESTS_CMD_INHIBIT, 0, USECS(1000), "CMD3")) {return IOST_LOCAL;}
	sdhci->clock_control = 0;
	usleep(10);
	u16 baseclock_mhz = st->caps >> 8 & 0xff;
	u16 div20M = baseclock_mhz / 20;
	assert(div20M < 0x400);
	u16 clkctrl = sdhci->clock_control = SDHCI_CLKCTRL_DIV(div20M) | SDHCI_CLKCTRL_INTCLK_EN;
	if (!wait_u16_set(&sdhci->clock_control, SDHCI_CLKCTRL_INTCLK_STABLE, USECS(100), "SDHCI internal clock")) {return IOST_GLOBAL;}
	if (!phy->setup(phy, SDHCI_PHY_START)) {return IOST_GLOBAL;}
	if (!phy->lock_freq(phy, 20000)) {return IOST_GLOBAL;}
	sdhci->clock_control = clkctrl |= SDHCI_CLKCTRL_SDCLK_EN;
	if (IOST_OK != sdhci_submit_cmd(sdhci, st, SDHCI_CMD(9) | SDHCI_R2, 2 << 16)) {return IOST_LOCAL;}
	if (IOST_OK != sdhci_wait_state(sdhci, st, SDHCI_PRESTS_CMD_INHIBIT, 0, USECS(10000), "CMD9")) {return IOST_LOCAL;}
	write_cxd(card->cxd, sdhci->resp[0], sdhci->resp[1], sdhci->resp[2], sdhci->resp[3]);
	info("CSD %08"PRIx32" %08"PRIx32" %08"PRIx32" %08"PRIx32"\n",
		card->cxd[0], card->cxd[1], card->cxd[2], card->cxd[3]
	);
	if (IOST_OK != sdhci_submit_cmd(sdhci, st, SDHCI_CMD(7) | SDHCI_R1, 2 << 16)) {return IOST_LOCAL;}
	if (IOST_OK != sdhci_submit_cmd(sdhci, st, SDHCI_CMD(16) | SDHCI_R1, 512)) {return IOST_LOCAL;}
	if (IOST_OK != sdhci_submit_cmd(sdhci, st, SDHCI_CMD(6) | SDHCI_R1b,
		MMC_SWITCH_SET_BYTE(EXTCSD_BUS_WIDTH, MMC_BUS_WIDTH_8)
	)) {
		return IOST_LOCAL;
	}
	if (IOST_OK != sdhci_wait_state(sdhci, st, SDHCI_PRESTS_CMD_INHIBIT | SDHCI_PRESTS_DAT_INHIBIT, 0, USECS(10000), "CMD6")) {return IOST_LOCAL;}
	sdhci->host_control1 = (
		sdhci->host_control1
		& ~(SDHCI_HOSTCTRL1_BUS_WIDTH_4 | SDHCI_HOSTCTRL1_DMA_MASK)
	) | SDHCI_HOSTCTRL1_BUS_WIDTH_8 | SDHCI_HOSTCTRL1_SDMA;
	sdhci->block_size = 512;
	sdhci->block_count = 1;
	sdhci->transfer_mode = SDHCI_TRANSMOD_READ;
	if (IOST_OK != sdhci_submit_cmd(sdhci, st, SDHCI_CMD(8) | SDHCI_R1 | SDHCI_CMD_DATA, 0)) {return IOST_LOCAL;}
	if (IOST_OK != sdhci_wait_state(sdhci, st, SDHCI_PRESTS_READ_READY, SDHCI_PRESTS_READ_READY, USECS(10000), "CMD8")) {return IOST_LOCAL;}
	for_range(i, 0, 32) {
		debug("%3"PRIu32":", i * 16);
		for_range(j, 0, 4) {
			u32 val = sdhci->fifo;
			debug(" %08"PRIx32, val);
			u8 *p = card->ext_csd + 16 * i + 4 * j;
			p[0] = val & 0xff;
			p[1] = val >> 8 & 0xff;
			p[2] = val >> 16 & 0xff;
			p[3] = val >> 24;
		}
		debugs("\n");
	}
	if (IOST_OK != sdhci_wait_state(sdhci, st, SDHCI_PRESTS_CMD_INHIBIT | SDHCI_PRESTS_DAT_INHIBIT, 0, USECS(10000), "CMD8")) {return IOST_LOCAL;}
	return IOST_OK;
}

_Bool sdhci_try_abort(volatile struct sdhci_regs *sdhci, struct sdhci_state *st) {
	if (IOST_OK != sdhci_wait_state(sdhci, st, SDHCI_PRESTS_CMD_INHIBIT, 0, USECS(10000), "abort transfer")) {return 0;}
	if (~sdhci->present_state & SDHCI_PRESTS_DAT_INHIBIT) {return 1;}
	sdhci->cmd = SDHCI_CMD(12) | SDHCI_CMD_ABORT | SDHCI_R1b;
	if (IOST_OK != sdhci_wait_state(sdhci, st, SDHCI_PRESTS_CMD_INHIBIT, 0, USECS(10000), "CMD12")) {return 0;}
	sdhci->swreset = SDHCI_SWRST_CMD | SDHCI_SWRST_DAT;
	timestamp_t start = get_timestamp();
	while (sdhci->swreset & (SDHCI_SWRST_CMD | SDHCI_SWRST_DAT)) {
		if (get_timestamp() - start > USECS(10000)) {return 0;}
		sched_yield();
	}
	return 1;
}

extern struct sdhci_phy emmc_phy;
struct sdhci_state emmc_state = {};

struct emmc_blockdev {
	struct async_blockdev blk;
	u32 address_shift;
	u8 *readptr, *invalidate_ptr;
	struct mmc_cardinfo card;
};

static _Alignas(4096) u32 adma2_32_desc[2048] = {};

static enum iost start(struct async_blockdev *dev_, u64 addr, u8 *buf, u8 *buf_end) {
	if (IOST_OK != sdhci_wait_state(emmc, &emmc_state, SDHCI_PRESTS_CMD_INHIBIT, 0, USECS(10000), "transfer start ")) {return IOST_GLOBAL;}
	if (emmc->present_state & SDHCI_PRESTS_DAT_INHIBIT) {
		if (!sdhci_try_abort(emmc, &emmc_state)) {return IOST_GLOBAL;}
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
	return sdhci_submit_cmd(emmc, &emmc_state,
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

static _Alignas(4096) u8 partition_table_buffer[4 * 4096];

enum iost boot_blockdev(struct async_blockdev *blk) {
	assert(blk->block_size <= 8192 && blk->block_size >= 128);
	assert(blk->block_size % 128 == 0);
	enum iost res;
	if (IOST_OK != (res = blk->start(blk, 0, partition_table_buffer, partition_table_buffer + sizeof(partition_table_buffer)))) {return res;}
	struct async_buf buf = blk->async.pump(&blk->async, 0, blk->block_size);
	dump_mem(partition_table_buffer, 512);
	if (buf.start[510] != 0x55 || buf.start[511] != 0xaa) {
		puts("no MBR found\n");
		return IOST_INVALID;
	}
	for_range(partition, 0, 4) {
		if (buf.start[446 + 16 * partition + 4] == 0xee) {
			goto gpt_found;
		}
	}
	puts("MBR does not contain a GPT entry\n");
	return IOST_INVALID;
	gpt_found:
	buf = blk->async.pump(&blk->async, 512, 2 * blk->block_size - 512);
	buf.start += blk->block_size - 512;
	dump_mem(buf.start, 92);
	if (from_le64(*(u64 *)buf.start) != 0x5452415020494645) {
		puts("wrong GPT signature\n");
		return IOST_INVALID;
	}
	if (from_le32(*(u32 *)(buf.start + 8)) != 0x00010000) {
		puts("unrecognized GPT version\n");
		return IOST_INVALID;
	}
	if (from_le32(*(u32 *)(buf.start + 12)) != 92) {
		puts("unexpected GPT header size\n");
		return IOST_INVALID;
	}
	if (from_le64(*(u64 *)(buf.start + 24)) != 1) {
		puts("current LBA does not match\n");
		return IOST_INVALID;
	}
	u64 backup_lba = from_le64(*(u64 *)(buf.start + 32));
	if (backup_lba != blk->num_blocks - 1) {
		puts("backup header not on last LBA\n");
		return IOST_INVALID;
	}
	u64 first_usable_lba = from_le64(*(u64 *)(buf.start + 40));
	u64 partition_table_size = first_usable_lba - 2;
	if (first_usable_lba > 1024 || partition_table_size < 16 * 1024 / blk->block_size) {
		puts("bogus partition table size\n");
		return IOST_INVALID;
	}
	u64 last_usable = from_le64(*(u64 *)(buf.start + 48));
	if (last_usable != blk->num_blocks -  partition_table_size - 2) {
		puts("wrong last usable LBA\n");
		return IOST_INVALID;
	}
	u32 entries_per_block = blk->block_size / 128;
	u32 num_partition_entries = (u32)partition_table_size * entries_per_block;
	buf = blk->async.pump(&blk->async, blk->block_size, 128);
	u32 mask = 0;
	u64 first[3], last[3];
	u8 str[2] = "A";
	for_range(i, 0, num_partition_entries) {
#ifdef DEBUG_MSG
		dump_mem(buf.start, 128);
#endif
		if ((size_t)(buf.end - buf.start) < 128) {
			blk->start(blk,
				2 + i / entries_per_block,
				partition_table_buffer,
				partition_table_buffer + sizeof(partition_table_buffer)
			);
			buf = blk->async.pump(&blk->async, 0, 128);
		}
		for_range(j, 0, 16) {
			if (buf.start[j] != 0) {goto entry_exists;}
		}
		debug("entry %"PRIu32": empty\n", i);
		buf = blk->async.pump(&blk->async, 128, i < num_partition_entries - 1 ? 128 : 0);
		continue;
	entry_exists:;
		u64 head = from_le64(*(u64 *)buf.start);
		u64 tail = from_be64(*(u64 *)(buf.start + 8));
		printf("entry %"PRIu32": %08"PRIx32"-%04"PRIx16"-%04"PRIx64"-%04"PRIx64"-%012"PRIx64,
			i, (u32)head, (u16)(head >> 32), head >> 48, tail >> 48, tail & 0xffffffffffff
		);
		u32 index = 3;
		if (head == 0x46f68e5ee5ab07a0 && tail == 0x9ce841a518929b7c) {index = 0;}
		if (index >= 3) {
			puts(" ignored\n");
		} else if (mask & 1 << index) {
			puts(" duplicate ignored\n");
		} else {
			first[index] = from_le64(*(u64 *)(buf.start + 32));
			last[index] = from_le64(*(u64 *)(buf.start + 40));
			str[0] = 'A' + index;
			printf(" levinboot payload %s: %"PRIu64"–%"PRIu64"\n", str, first[index], last[index]);
			if (first[index] > last[index] || last[index] > last_usable) {
				puts("bogus LBAs, ignoring");
			} else {
				mask |= 1 << index;
			}
		}
		buf = blk->async.pump(&blk->async, 128, i < num_partition_entries - 1 ? 128 : 0);
	}
	if (mask == 0) {return IOST_INVALID;}
	u32 used_index = 0;
	if (mask == 2 || mask == 6) {used_index = 1;}
	if (mask == 4 || mask == 5) {used_index = 2;}
	u64 used_first = first[used_index], used_last = last[used_index];
	str[0] = 'A' + used_index;
	printf("using payload %s\n", str);
	u32 max_length = ((60 << 20) + blk->block_size - 1) / blk->block_size;
	if (used_last - used_first >= max_length) {
		puts("selected payload partition is larger than the buffer, clipping\n");
		used_last = used_first + max_length - 1;
	}
	if (IOST_OK != (res = blk->start(blk, used_first, blob_buffer.start, blob_buffer.end))) {return res;}
	if (!decompress_payload(&blk->async)) {return IOST_INVALID;}
	return IOST_OK;
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

_Bool parse_cardinfo() {
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
	if (IOST_OK != sdhci_init_late(emmc, &emmc_state, &emmc_phy, &blk.card)) {
		infos("eMMC init failed\n");
		goto shut_down_emmc;
	}
	if (!parse_cardinfo()) {goto out;}

	infos("eMMC init done\n");
	if (!wait_for_boot_cue(BOOT_MEDIUM_EMMC)) {goto out;}
	enum iost res = boot_blockdev(&blk.blk);
	if (res == IOST_OK) {boot_medium_loaded(BOOT_MEDIUM_EMMC);}
	if (res == IOST_GLOBAL) {goto shut_down_emmc;}

	if (sdhci_try_abort(emmc, &emmc_state)) {
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
