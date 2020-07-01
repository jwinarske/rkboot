/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <rk3399.h>
#include <aarch64.h>
#include <rkspi_regs.h>
#include <mmu.h>

extern struct async_transfer spi1_async;

void rkspi_start_irq_flash_read(u32 addr);
void rkspi_end_irq_flash_read();

static inline void UNUSED rk3399_spi_setup() {
	mmu_map_mmio_identity(0xff1d0000, 0xff1d0fff);
	cru[CRU_CLKGATE_CON+23] = SET_BITS16(1, 0) << 11;
	/* clk_spi1 = CPLL/8 = 100 MHz */
	cru[CRU_CLKSEL_CON+59] = SET_BITS16(1, 0) << 15 | SET_BITS16(7, 7) << 8;
	dsb_st();
	cru[CRU_CLKGATE_CON+9] = SET_BITS16(1, 0) << 13;
	spi1->baud = 2;
}

static inline void UNUSED rk3399_spi_teardown() {
	mmu_unmap_range(0xff1d0000, 0xff1d0fff);
	cru[CRU_CLKGATE_CON+9] = SET_BITS16(1, 1) << 13;
}
