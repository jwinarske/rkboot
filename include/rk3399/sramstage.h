/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

void pmu_cru_setup();
void misc_init();
void rk3399_init_sdmmc();
struct sdhci_state;
void emmc_init(struct sdhci_state *st);
void pcie_init();

struct stage_store;
u32 end_sramstage(struct stage_store *store);

#define DEFINE_VSTACK\
	X(CPU0) X(DDRC0) X(DDRC1) X(SDMMC) X(EMMC) X(PCIE)
#define VSTACK_DEPTH UINT64_C(0x1000)

#define DEFINE_REGMAP\
	MMIO(EMMC, emmc, 0xfe330000, struct sdhci_regs)\
	MMIO(SDMMC, sdmmc, 0xfe320000, struct dwmmc_regs)\
	MMIO(GIC500D, gic500d, 0xfee00000, struct gic_distributor)\
	MMIO(GIC500R, gic500r, 0xfef00000, struct gic_redistributor)\
	MMIO(CIC, cic, 0xff620000, u32)\
	MMIO(STIMER0, stimer0, 0xff860000, struct rktimer_regs)\
	MMIO(CRYPTO1, crypto1, 0xff8b8000, struct rkcrypto_v1_regs)\
	MMIO(PCIE_CLIENT, pcie_client, 0xfd000000, u32)\
	MMIO(PCIE_MGMT, pcie_mgmt, 0xfd900000, u32)\
	MMIO(PCIE_RCCONF, pcie_rcconf, 0xfd800000, u32)\
	MMIO(PCIE_CONF_SETUP, pcie_conf_setup, 0xfda00000, u32)\
	MMIO(PCIE_ADDR_XLATION, pcie_addr_xlation, 0xfdc00000, struct rkpcie_addr_xlation)\
	MMIO(SPI1, spi1, 0xff1d0000, struct rkspi_regs)\
	MMIO(I2C4, i2c4, 0xff3d0000, struct rki2c_regs)\
	MMIO(GPIO0, gpio0, 0xff720000, struct rkgpio_regs)\
	MMIO(GPIO1, gpio1, 0xff730000, struct rkgpio_regs)\
	MMIO(GPIO2, gpio2, 0xff780000, struct rkgpio_regs)\
	MMIO(GPIO3, gpio3, 0xff788000, struct rkgpio_regs)\
	MMIO(GPIO4, gpio4, 0xff790000, struct rkgpio_regs)\
	MMIO(UART, uart, 0xff1a0000, struct uart)\
	MMIO(CRU, cru, 0xff760000, u32)\
	MMIO(PMU, pmu, 0xff310000, u32)\
	MMIO(PMUCRU, pmucru, 0xff750000, u32)\
	MMIO(PMUGRF, pmugrf, 0xff320000, u32)\
	/* the generic SoC registers are last, because they are referenced often, meaning they get addresses 0xffffxxxx, which can be generated in a single MOVN instruction */
#define DEFINE_REGMAP64K\
	X(DMC, dmc, 0xffa80000, u32)\
	X(PMUSGRF, pmusgrf, 0xff330000, u32)\
	X(GRF, grf, 0xff770000, u32)\

#include "vmmap.h"

#define DEFINE_RK3399_INIT_FLAGS\
	X(DDRC0_INIT, 15) X(DDRC1_INIT, 15)\
	X(DDRC0_READY, 30) X(DDRC1_READY, 30)\
	X(DRAM_TRAINING, 40) X(DRAM_READY, 50)\
	X(SD_INIT, 100) X(EMMC_INIT, 100)\
	X(PCIE, 100)
enum {
#define X(name, timeout) RK3399_INIT_##name##_BIT,
	DEFINE_RK3399_INIT_FLAGS
#undef X
	NUM_RK3399_INIT
};
_Static_assert(NUM_RK3399_INIT <= sizeof(size_t) * 8, "too many initialization flags");
enum {
#define X(name, timeout) RK3399_INIT_##name = (size_t)1 << RK3399_INIT_##name##_BIT,
	DEFINE_RK3399_INIT_FLAGS
#undef X
};
_Static_assert(sizeof(size_t) == sizeof(_Atomic(size_t)), "atomic size_t has differing size");
extern _Atomic(size_t) rk3399_init_flags;
void rk3399_set_init_flags(size_t);
