/* SPDX-License-Identifier: CC0-1.0 */
#include <assert.h>
#include <dwmmc.h>
#include <async.h>
#include <log.h>
#include <gic.h>
#include <gic_regs.h>
#include <exc_handler.h>
#include <rk3399.h>

struct async_transfer sdmmc_async = {};

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

void rk3399_sdmmc_start_irq_read(u32 sector) {
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
void rk3399_sdmmc_end_irq_read() {
	gicv2_disable_spi(gic500d, sdmmc_intid);
	fiq_handler_spx = irq_handler_spx = 0;
}
