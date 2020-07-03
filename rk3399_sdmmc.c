/* SPDX-License-Identifier: CC0-1.0 */
#include <assert.h>
#include <dwmmc.h>
#include <async.h>
#include <log.h>
#include <gic.h>
#include <gic_regs.h>
#include <exc_handler.h>
#include <rk3399.h>
#include <mmu.h>
#include "elfloader.h"

static struct async_transfer sdmmc_async = {};

static const u32 sdmmc_intid = 97, sdmmc_irq_threshold = 128;

static void handle_sdmmc_interrupt(volatile struct dwmmc_regs *sdmmc, struct async_transfer *async) {
	u32 items_to_read = 0, rintsts = sdmmc->rintsts, ack = 0;
	assert((rintsts & DWMMC_ERROR_INT_MASK) == 0);
	if (rintsts & DWMMC_INT_DATA_TRANSFER_OVER) {
		ack |= DWMMC_INT_DATA_TRANSFER_OVER;
		items_to_read = sdmmc->status >> 17 & 0x1fff;
	}
	if (rintsts & DWMMC_INT_RX_FIFO_DATA_REQ) {
		ack |= DWMMC_INT_RX_FIFO_DATA_REQ;
		if (items_to_read < sdmmc_irq_threshold) {
			items_to_read = sdmmc_irq_threshold;
		}
	}
	if (items_to_read) {
		u32 *buf = (u32*)async->buf;
		size_t pos = async->pos;
		assert(pos % 4 == 0);
		for_range(i, 0, items_to_read) {
			buf[pos/4] = *(volatile u32 *)0xfe320200;
			pos += 4;
		}
		async->pos = pos;
	}
#ifdef DEBUG_MSG
	if (unlikely(!ack)) {
		dwmmc_print_status(sdmmc);
		debug("don't know what to do with interrupt\n");
	} else if (ack == 0x20) {
		debugs(".");
	} else {
		debug("ack %"PRIx32"\n", ack);
	}
#endif
	sdmmc->rintsts = ack;
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
		__asm__("msr DAIFClr, #0xf");
		if (grp0_intid == sdmmc_intid) {
			spew("SDMMC interrupt, buf=0x%zx\n", (size_t)sdmmc_async.buf);
			handle_sdmmc_interrupt(sdmmc, &sdmmc_async);
		} else if (grp0_intid == 1023) {
			debugs("spurious interrupt\n");
		} else {
			die("unexpected group 0 interrupt");
		}
	}
	__asm__ volatile("msr DAIFSet, #0xf;msr "ICC_EOIR0_EL1", %0" : : "r"(grp0_intid));
}

static void UNUSED rk3399_sdmmc_start_irq_read(u32 sector) {
	fiq_handler_spx = irq_handler_spx = irq_handler;
	gicv2_setup_spi(gic500d, sdmmc_intid, 0x80, 1, IGROUP_0 | INTR_LEVEL);
	sdmmc->intmask = DWMMC_ERROR_INT_MASK | DWMMC_INT_DATA_TRANSFER_OVER | DWMMC_INT_RX_FIFO_DATA_REQ | DWMMC_INT_TX_FIFO_DATA_REQ;
	sdmmc->ctrl |= DWMMC_CTRL_INT_ENABLE;
	assert(sdmmc_async.total_bytes % 512 == 0);
	sdmmc->blksiz = 512;
	sdmmc->bytcnt = sdmmc_async.total_bytes;
	enum dwmmc_status st = dwmmc_wait_cmd_done(sdmmc, 18 | DWMMC_R1 | DWMMC_CMD_DATA_EXPECTED, sector, 1000);
	dwmmc_check_ok_status(sdmmc, st, "CMD18 (READ_MULTIPLE_BLOCK)");
}
static void UNUSED rk3399_sdmmc_end_irq_read() {
	gicv2_disable_spi(gic500d, sdmmc_intid);
	fiq_handler_spx = irq_handler_spx = 0;
}

void udelay(u32 usec);

void load_from_sd(struct payload_desc *payload, u8 *buf, size_t buf_size) {
	static const u32 sd_start_sector = 4 << 11; /* offset 4 MiB */
	struct async_transfer *async = &sdmmc_async;
	async->total_bytes = buf_size;
	async->buf = buf;
	async->pos = 0;

	/* hclk_sd = 200 MHz */
	cru[CRU_CLKSEL_CON + 13] = SET_BITS16(1, 0) << 15 | SET_BITS16(5, 4) << 8;
	/* clk_sdmmc = 24 MHz / 30 = 800 kHz */
	cru[CRU_CLKSEL_CON + 16] = SET_BITS16(3, 5) << 8 | SET_BITS16(7, 29);
	dsb_st();
	cru[CRU_CLKGATE_CON+6] = SET_BITS16(1, 0) << 1;
	cru[CRU_CLKGATE_CON+12] = SET_BITS16(1, 0) << 13;
	/* drive phase 180° */
	cru[CRU_SDMMC_CON] = SET_BITS16(1, 1);
	cru[CRU_SDMMC_CON + 0] = SET_BITS16(2, 1) << 1 | SET_BITS16(8, 0) << 3 | SET_BITS16(1, 0) << 11;
	cru[CRU_SDMMC_CON + 1] = SET_BITS16(2, 0) << 1 | SET_BITS16(8, 0) << 3 | SET_BITS16(1, 0) << 11;
	cru[CRU_SDMMC_CON] = SET_BITS16(1, 0);
	/* mux out the SD lines */
	grf[GRF_GPIO4B_IOMUX] = SET_BITS16(2, 1) | SET_BITS16(2, 1) << 2 | SET_BITS16(2, 1) << 4 | SET_BITS16(2, 1) << 6 | SET_BITS16(2, 1) << 8 | SET_BITS16(2, 1) << 10;
	/* mux out card detect */
	pmugrf[PMUGRF_GPIO0A_IOMUX] = SET_BITS16(2, 1) << 14;
	/* reset SDMMC */
	cru[CRU_SOFTRST_CON + 7] = SET_BITS16(1, 1) << 10;
	udelay(100);
	cru[CRU_SOFTRST_CON + 7] = SET_BITS16(1, 0) << 10;
	udelay(2000);

	mmu_map_mmio_identity(0xfe320000, 0xfe320fff);
	dsb_ishst();
	info("starting SDMMC\n");
	dwmmc_init(sdmmc);

#if !CONFIG_ELFLOADER_IRQ
#if !CONFIG_ELFLOADER_DMA
	dwmmc_read_poll(sdmmc, sd_start_sector, async->buf, async->total_bytes);
#else
	pmusgrf[PMUSGRF_DDR_RGN_CON+16] = SET_BITS16(1, 1) << 9;
	dwmmc_read_poll_dma(sdmmc, sd_start_sector, async->buf, async->total_bytes);
#endif
	async->pos = async->total_bytes;
#else
	rk3399_sdmmc_start_irq_read(sd_start_sector);
#endif

	decompress_payload(async, payload);

#if CONFIG_ELFLOADER_IRQ
	rk3399_sdmmc_end_irq_read();
#endif

	printf("had read %zu bytes\n", async->pos);
}
