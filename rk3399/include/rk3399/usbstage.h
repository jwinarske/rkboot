/* SPDX-License-Identifier: CC0-1.0 */
#pragma once

#define DEFINE_VSTACK(X) X(CPU0)
#define VSTACK_DEPTH UINT64_C(0x3000)

#define DEFINE_REGMAP(MMIO)\
	MMIO(GIC500D, gic500d, 0xfee00000, struct gic_distributor)\
	MMIO(GIC500R, gic500r, 0xfef00000, struct gic_redistributor)\
	MMIO(STIMER0, stimer0, 0xff860000, struct rktimer_regs)\
	MMIO(CRYPTO1, crypto1, 0xff8b8000, struct rkcrypto_v1_regs)\
	MMIO(SPI1, spi1, 0xff1d0000, struct rkspi_regs)\
	MMIO(UART, uart, 0xff1a0000, struct uart)\
	MMIO(CRU, cru, 0xff760000, u32)\
	MMIO(PMU, pmu, 0xff310000, u32)\
	MMIO(PMUCRU, pmucru, 0xff750000, u32)\
	MMIO(PMUGRF, pmugrf, 0xff320000, u32)\
	/* the generic SoC registers are last, because they are referenced often, meaning they get addresses 0xffffxxxx, which can be generated in a single MOVN instruction */
#define DEFINE_REGMAP64K(X)\
	X(OTG0, otg0, 0xfe800000, void)\
	X(PMUSGRF, pmusgrf, 0xff330000, u32)\
	X(GRF, grf, 0xff770000, u32)\

#include "vmmap.h"
