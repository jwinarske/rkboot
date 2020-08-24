/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>
#include <plat.h>

enum {
	GRF_SOC_CON9 = 0x6224 >> 2,	/* 0, 20–26 */
	GRF_GPIO2C_IOMUX = 0xe008 >> 2,
	GRF_GPIO4B_IOMUX = 0xe024 >> 2,
	GRF_GPIO4C_IOMUX = 0xe028 >> 2,
	GRF_GPIO2D_P = 0xe04c >> 2,
	GRF_SOC_CON5 = 0xc214 >> 2,	/* 5–6 */
	GRF_SOC_CON0 = 0xe200 >> 2,	/* 0–4, 5_PCIE, 7–8, 9_PCIE */
	GRF_SOC_STATUS = 0xe2a0 >> 2,
	GRF_DDRC_CON = 0xe380 >> 2,
	GRF_EMMCCORE_CON = 0xf000 >> 2,
	GRF_EMMCPHY_CON = 0xf780 >> 2,
	GRF_EMMCPHY_STATUS = 0xf7a0 >> 2,
};

enum {
	PMUGRF_GPIO0A_IOMUX = 0,
	PMUGRF_GPIO0B_IOMUX = 0x004 >> 2,
	PMUGRF_GPIO1A_IOMUX = 0x010 >> 2,
	PMUGRF_GPIO1B_IOMUX = 0x014 >> 2,
	PMUGRF_GPIO1C_IOMUX = 0x018 >> 2,
	PMUGRF_GPIO0B_P = 0x044 >> 2,
	PMUGRF_OS_REG2 = 0x308 >> 2,
	PMUGRF_OS_REG3 = 0x30c >> 2,
};

enum {
	PMUSGRF_DDR_RGN_CON = 0,
	PMUSGRF_SOC_CON4 = 0xe010 >> 2,
};

enum {
	CRU_LPLL_CON = 0,
	CRU_BPLL_CON = 0x20 >> 2,
	CRU_DPLL_CON = 0x040 >> 2,
	CRU_CPLL_CON = 0x060 >> 2,
	CRU_GPLL_CON = 0x080 >> 2,
	CRU_CLKSEL_CON = 0x100 >> 2,
	CRU_CLKGATE_CON = 0x300 >> 2,
	CRU_SOFTRST_CON = 0x400 >> 2,
	CRU_SDMMC_CON = 0x580 >> 2,
};

enum {
	PMU_PWRDN_CON = 0x14 >> 2,
	PMU_PWRDN_ST = 0x18 >> 2,
	PMU_SFT_CON = 0x24 >> 2,
	PMU_BUS_IDLE_REQ = 0x60 >> 2,
	PMU_BUS_IDLE_ST = 0x64 >> 2,
	PMU_BUS_IDLE_ACK = 0x64 >> 2,
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

struct rkspi_regs;
static volatile struct rkspi_regs *const spi1 = (volatile struct rkspi_regs*)0xff1d0000;

struct rki2c_regs;
static volatile struct rki2c_regs *const i2c4 = (volatile struct rki2c_regs *)0xff3d0000;

struct rksaradc_regs;
static volatile struct rksaradc_regs *const saradc = (volatile struct rksaradc_regs *)0xff100000;

struct rkgpio_regs;
static volatile struct rkgpio_regs *const gpio0 = (volatile struct rkgpio_regs *)0xff720000;

struct dwmmc_regs;
static volatile struct dwmmc_regs *const sdmmc = (volatile struct dwmmc_regs*)0xfe320000;

struct rktimer_regs;
static volatile struct rktimer_regs *const stimer0 = (volatile struct rktimer_regs *)0xff860000;
static volatile struct rktimer_regs *const stimer6 = (volatile struct rktimer_regs *)0xff868000;

struct sdhci_regs;
static volatile struct sdhci_regs *const emmc = (volatile struct sdhci_regs *)0xfe330000;

struct rkcrypto_v1_regs;
static volatile struct rkcrypto_v1_regs *const crypto1 = (volatile struct rkcrypto_v1_regs *)0xff8b8000;
