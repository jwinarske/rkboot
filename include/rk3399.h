/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

enum {
	GRF_GPIO2C_IOMUX = 0xe008 >> 2,
	GRF_GPIO4C_IOMUX = 0xe028 >> 2,
	GRF_SOC_CON0 = 0xe200 >> 2,
	GRF_SOC_CON7 = 0xe21c >> 2,
	GRF_DDRC_CON = 0xe380 >> 2
};

enum {
	PMUGRF_GPIO0B_IOMUX = 0x004 >> 2,
	PMUGRF_GPIO0B_P = 0x044 >> 2,
	PMUGRF_OS_REG2 = 0x308 >> 2,
	PMUGRF_OS_REG3 = 0x30c >> 2,
};

enum {PMUSGRF_SOC_CON4 = 0xe010 >> 2};

enum {
	CRU_LPLL_CON = 0,
	CRU_BPLL_CON = 0x20 >> 2,
	CRU_DPLL_CON = 0x040 >> 2,
	CRU_CLKSEL_CON = 0x100 >> 2,
	CRU_SOFTRST_CON = 0x400 >> 2,
};

enum {
	PMU_SFT_CON = 0x24 >> 2,
	PMU_BUS_IDLE_REQ = 0x60 >> 2,
	PMU_BUS_IDLE_ST = 0x64 >> 2,
	PMU_DDR_SREF_ST = 0x98 >> 2,
	PMU_NOC_AUTO_ENA = 0xd8 >> 2,
};

enum {
	CIC_STATUS = 4
};

enum {CYCLES_PER_MICROSECOND = 24};

static volatile u32 *const pmu = (volatile u32 *)0xff310000;
static volatile u32 *const pmugrf = (volatile u32 *)0xff320000;
static volatile u32 *const pmusgrf = (volatile u32 *)0xff330000;
static volatile u32 *const cic = (volatile u32 *)0xff620000;
static volatile u32 *const cru = (volatile u32 *)0xff760000;
static volatile u32 *const pmucru = (volatile u32 *)0xff750000;
static volatile u32 *const grf = (volatile u32 *)0xff770000;

#define SET_BITS16(number, value) (((((u32)1 << number) - 1) << 16) | ((u32)(u16)(value) & (((u32)1 << number) - 1)))
#define SET_BITS32(number, value) (((((u64)1 << number) - 1) << 32) | (u64)((u32)(value) & (((u32)1 << number) - 1)))
