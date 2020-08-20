/* SPDX-License-Identifier: CC0-1.0 */
#include <rk3399/dramstage.h>
#include <assert.h>

#include <mmu.h>
#include <log.h>
#include <die.h>
#include <exc_handler.h>
#include <gic.h>
#include <gic_regs.h>
#include <dwmmc.h>
#include <dwmmc_dma.h>
#include <dwmmc_helpers.h>

#include <rk3399.h>
#include <async.h>

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
	dsb_st();
	return 1;
}

struct async_transfer sdmmc_async = {};

static const u32 sdmmc_intid = 97, sdmmc_irq_threshold = 128;

static void UNUSED handle_sdmmc_interrupt_pio(volatile struct dwmmc_regs *sdmmc, struct async_transfer *async) {
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
		dwmmc_print_status(sdmmc, "unexpected interrupt ");
	} else if (ack == 0x20) {
		debugs(".");
	} else {
		debug("ack %"PRIx32"\n", ack);
	}
#endif
	sdmmc->rintsts = ack;
}

#if CONFIG_ELFLOADER_DMA
struct dwmmc_dma_state sdmmc_dma_state = {};
#endif

static void UNUSED rk3399_sdmmc_start_irq_read(u32 sector) {
	gicv2_setup_spi(gic500d, sdmmc_intid, 0x80, 1, IGROUP_0 | INTR_LEVEL);
#if !CONFIG_ELFLOADER_DMA
	sdmmc->intmask = DWMMC_ERROR_INT_MASK | DWMMC_INT_DATA_TRANSFER_OVER | DWMMC_INT_RX_FIFO_DATA_REQ | DWMMC_INT_TX_FIFO_DATA_REQ;
#else
	dwmmc_setup_dma(sdmmc);
	dwmmc_init_dma_state(&sdmmc_dma_state);
	sdmmc_dma_state.buf = sdmmc_async.buf;
	sdmmc_dma_state.bytes_left = sdmmc_async.total_bytes;
	sdmmc_dma_state.bytes_transferred = 0;
	sdmmc->desc_list_base = (u32)(uintptr_t)&sdmmc_dma_state.desc;
#endif
	sdmmc->ctrl |= DWMMC_CTRL_INT_ENABLE;
	assert(sdmmc_async.total_bytes % 512 == 0);
	sdmmc->blksiz = 512;
	sdmmc->bytcnt = sdmmc_async.total_bytes;
	enum dwmmc_status st = dwmmc_wait_cmd_done(sdmmc, 18 | DWMMC_R1 | DWMMC_CMD_DATA_EXPECTED, sector, 1000);
	dwmmc_check_ok_status(sdmmc, st, "CMD18 (READ_MULTIPLE_BLOCK)");
}
static void UNUSED rk3399_sdmmc_end_irq_read() {
	gicv2_disable_spi(gic500d, sdmmc_intid);
#if CONFIG_ELFLOADER_DMA
	/* make sure the iDMAC is suspended before we hand off */
    u32 tmp;
	while (((tmp = sdmmc->idmac_status) & (DWMMC_IDMAC_INTMASK_ABNORMAL | DWMMC_IDMAC_INT_CARD_ERROR)) == 0 && (tmp >> 13 & 15) > 1) {
		__asm__("yield");
	}
#endif
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
	if (!dwmmc_init_late(sdmmc, &svc)) {
		boot_medium_exit(BOOT_MEDIUM_SD);
		return;
	}

	if (!wait_for_boot_cue(BOOT_MEDIUM_SD)) {
		boot_medium_exit(BOOT_MEDIUM_SD);
		return;
	}
	static const u32 sd_start_sector = 4 << 11; /* offset 4 MiB */
	init_blob_buffer(&sdmmc_async);

#if !CONFIG_ELFLOADER_IRQ
#if !CONFIG_ELFLOADER_DMA
	dwmmc_read_poll(sdmmc, sd_start_sector, sdmmc_async.buf, sdmmc_async.total_bytes);
#else
	dwmmc_read_poll_dma(sdmmc, sd_start_sector, sdmmc_async.buf, sdmmc_async.total_bytes);
#endif
	sdmmc_async.pos = sdmmc_async.total_bytes;
#else
	rk3399_sdmmc_start_irq_read(sd_start_sector);
#endif

	if (decompress_payload(&sdmmc_async)) {boot_medium_loaded(BOOT_MEDIUM_SD);}

#if CONFIG_ELFLOADER_IRQ
	rk3399_sdmmc_end_irq_read();
#endif

	printf("had read %zu bytes\n", sdmmc_async.pos);
	boot_medium_exit(BOOT_MEDIUM_SD);
}
