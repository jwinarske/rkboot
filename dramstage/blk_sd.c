/* SPDX-License-Identifier: CC0-1.0 */
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
#include "../elfloader.h"
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

static struct async_transfer sdmmc_async = {};

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
static struct dwmmc_dma_state dma_state;
#endif

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
#if !CONFIG_ELFLOADER_DMA
			handle_sdmmc_interrupt_pio(sdmmc, &sdmmc_async);
#else
			dwmmc_handle_dma_interrupt(sdmmc, &dma_state);
			sdmmc_async.pos = dma_state.bytes_transferred;
#endif
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
#if !CONFIG_ELFLOADER_DMA
	sdmmc->intmask = DWMMC_ERROR_INT_MASK | DWMMC_INT_DATA_TRANSFER_OVER | DWMMC_INT_RX_FIFO_DATA_REQ | DWMMC_INT_TX_FIFO_DATA_REQ;
#else
	dwmmc_setup_dma(sdmmc);
	dwmmc_init_dma_state(&dma_state);
	dma_state.buf = sdmmc_async.buf;
	dma_state.bytes_left = sdmmc_async.total_bytes;
	dma_state.bytes_transferred = 0;
	sdmmc->desc_list_base = (u32)(uintptr_t)&dma_state.desc;
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
	fiq_handler_spx = irq_handler_spx = 0;
#if CONFIG_ELFLOADER_DMA
	/* make sure the iDMAC is suspended before we hand off */
    u32 tmp;
	while (((tmp = sdmmc->idmac_status) & (DWMMC_IDMAC_INTMASK_ABNORMAL | DWMMC_IDMAC_INT_CARD_ERROR)) == 0 && (tmp >> 13 & 15) > 1) {
		__asm__("yield");
	}
#endif
}


_Bool load_from_sd(struct payload_desc *payload, u8 *buf, size_t buf_size) {
	infos("trying SD");
	mmu_map_mmio_identity(0xfe320000, 0xfe320fff);
	dsb_ishst();
	if (cru[CRU_CLKGATE_CON+12] & 1 << 13) {
		puts("sramstage left SD disabled\n");
		return 0;
	}
	struct dwmmc_signal_services svc = {
		.set_clock = set_clock,
		.set_signal_voltage = 0,
		.frequencies_supported = 1 << DWMMC_CLOCK_400K | 1 << DWMMC_CLOCK_25M | 1 << DWMMC_CLOCK_50M,
		.voltages_supported = 1 << DWMMC_SIGNAL_3V3,
	};
	if (!dwmmc_init_late(sdmmc, &svc)) {return 0;}

	static const u32 sd_start_sector = 4 << 11; /* offset 4 MiB */
	struct async_transfer *async = &sdmmc_async;
	async->total_bytes = buf_size;
	async->buf = buf;
	async->pos = 0;

#if CONFIG_ELFLOADER_DMA
	/* set DRAM as Non-Secure */
	pmusgrf[PMUSGRF_DDR_RGN_CON+16] = SET_BITS16(1, 1) << 9;
#endif

#if !CONFIG_ELFLOADER_IRQ
#if !CONFIG_ELFLOADER_DMA
	dwmmc_read_poll(sdmmc, sd_start_sector, async->buf, async->total_bytes);
#else
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
	return 1;
}
