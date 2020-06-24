/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

enum {
	GRF_GPIO2C_IOMUX = 0xe008 >> 2,
	GRF_GPIO4B_IOMUX = 0xe024 >> 2,
	GRF_GPIO4C_IOMUX = 0xe028 >> 2,
	GRF_SOC_CON0 = 0xe200 >> 2,
	GRF_SOC_CON7 = 0xe21c >> 2,
	GRF_DDRC_CON = 0xe380 >> 2
};

enum {
	PMUGRF_GPIO0A_IOMUX = 0,
	PMUGRF_GPIO0B_IOMUX = 0x004 >> 2,
	PMUGRF_GPIO1A_IOMUX = 0x010 >> 2,
	PMUGRF_GPIO1B_IOMUX = 0x014 >> 2,
	PMUGRF_GPIO0B_P = 0x044 >> 2,
	PMUGRF_OS_REG2 = 0x308 >> 2,
	PMUGRF_OS_REG3 = 0x30c >> 2,
};

enum {PMUSGRF_SOC_CON4 = 0xe010 >> 2};

enum {
	CRU_LPLL_CON = 0,
	CRU_BPLL_CON = 0x20 >> 2,
	CRU_DPLL_CON = 0x040 >> 2,
	CRU_CPLL_CON = 0x060 >> 2,
	CRU_GPLL_CON = 0x080 >> 2,
	CRU_CLKSEL_CON = 0x100 >> 2,
	CRU_SOFTRST_CON = 0x400 >> 2,
	CRU_SDMMC_CON = 0x580 >> 2,
};

enum {
	PMU_SFT_CON = 0x24 >> 2,
	PMU_BUS_IDLE_REQ = 0x60 >> 2,
	PMU_BUS_IDLE_ST = 0x64 >> 2,
	PMU_DDR_SREF_ST = 0x98 >> 2,
	PMU_NOC_AUTO_ENA = 0xd8 >> 2,
};

enum {
	PMUCRU_PPLL_CON = 0,
	PMUCRU_CLKSEL_CON = 0x80 >> 2,
	PMUCRU_CLKFRAC_CON = 0x98 >> 2,
	PMUCRU_CLKGATE_CON = 0x100 >> 2,
	PMUCRU_SOFTRST_CON = 0x110 >> 2,
	PMUCRU_RSTNHOLD_CON = 0x120 >> 2,
	PMUCRU_GATEDIS_CON = 0x130 >> 2
};

enum {
	CIC_STATUS = 4
};

enum {CYCLES_PER_MICROSECOND = TICKS_PER_MICROSECOND};

struct gic_distributor;
struct gic_redistributor;
static volatile struct gic_distributor *const gic500d = (volatile struct gic_distributor *)0xfee00000;
static volatile struct gic_redistributor *const gic500r = (volatile struct gic_redistributor *)0xfef00000;
static volatile u32 *const pmu = (volatile u32 *)0xff310000;
static volatile u32 *const pmugrf = (volatile u32 *)0xff320000;
static volatile u32 *const pmusgrf = (volatile u32 *)0xff330000;
static volatile u32 *const cic = (volatile u32 *)0xff620000;
static volatile u32 *const cru = (volatile u32 *)0xff760000;
static volatile u32 *const pmucru = (volatile u32 *)0xff750000;
static volatile u32 *const grf = (volatile u32 *)0xff770000;

#define SET_BITS16(number, value) (((((u32)1 << number) - 1) << 16) | ((u32)(u16)(value) & (((u32)1 << number) - 1)))
#define SET_BITS32(number, value) (((((u64)1 << number) - 1) << 32) | (u64)((u32)(value) & (((u32)1 << number) - 1)))

struct rkspi;
static volatile struct rkspi *const spi1 = (volatile struct rkspi*)0xff1d0000;

struct rki2c_regs;
static volatile struct rki2c_regs *const i2c4 = (volatile struct rki2c_regs *)0xff3d0000;

enum {SPI_MAX_RECV = 0xfffe};
void spi_read_flash(u8 *buf, u32 size);
