/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

#define DRAM_START ((u64)0)
#define TZRAM_SIZE 0x00200000

struct sched_runnable_list;
enum rk3399_board {
	BOARD_UNKNOWN,
	BOARD_ROCKPRO64,
	BOARD_PINEBOOK_PRO,
};
extern _Atomic(u32) rk3399_detected_board;
extern struct sched_runnable_list rk3399_board_detection_waiters;

void rk3399_probe_board();

void boot_sd();
void boot_emmc();
void boot_spi();
void boot_nvme();

extern u32 entropy_buffer[];
extern u16 entropy_words;
void pull_entropy(_Bool keep_running);

/* access to these is only allowed by the currently cued boot medium thread */
struct async_transfer;
struct async_blockdev;
struct payload_desc;
struct payload_desc *get_payload_desc();
enum iost decompress_payload(struct async_transfer *async);
enum iost boot_blockdev(struct async_blockdev *blk);

/* boot commit functions: only run after all boot medium threads have finished running */
struct fdt_header;

struct fdt_addendum {
	u64 fdt_address;
	u64 initcpio_start, initcpio_end, dram_start, dram_size;
	u32 *entropy;
	size_t entropy_words;
	u32 boot_cpu;
};

_Bool transform_fdt(struct fdt_header *out_header, u32 *out_end, const struct fdt_header *header, const char *in_end, struct fdt_addendum *info);
_Noreturn void commit(struct payload_desc *payload);

/* this enumeration defines the boot order */
#define DEFINE_BOOT_MEDIUM(X) X(SD) X(EMMC) X(NVME) X(SPI)
enum boot_medium {
#define X(name) BOOT_MEDIUM_##name,
	DEFINE_BOOT_MEDIUM(X)
#undef X
	NUM_BOOT_MEDIUM,
	BOOT_CUE_NONE = NUM_BOOT_MEDIUM,
	BOOT_CUE_EXIT,
};
_Static_assert(4 * NUM_BOOT_MEDIUM <= 32, "boot state does not fit into 32 bits");
_Bool wait_for_boot_cue(enum boot_medium);
void boot_medium_loaded(enum boot_medium);
void boot_medium_exit(enum boot_medium);

#define DEFINE_VSTACK(X) X(CPU0) X(MONITOR) X(BOARD_PROBE) DEFINE_BOOT_MEDIUM(X)
#define VSTACK_DEPTH UINT64_C(0x3000)

#define DEFINE_REGMAP(MMIO)\
	MMIO(GIC500D, gic500d, 0xfee00000, struct gic_distributor)\
	MMIO(GIC500R, gic500r, 0xfef00000, struct gic_redistributor)\
	MMIO(STIMER0, stimer0, 0xff860000, struct rktimer_regs)\
	MMIO(CRYPTO1, crypto1, 0xff8b8000, struct rkcrypto_v1_regs)\
	MMIO(SDMMC, sdmmc, 0xfe320000, struct dwmmc_regs)\
	MMIO(EMMC, emmc, 0xfe330000, struct sdhci_regs)\
	MMIO(PCIE_CLIENT, pcie_client, 0xfd000000, u32)\
	MMIO(PCIE_MGMT, pcie_mgmt, 0xfd900000, u32)\
	MMIO(PCIE_RCCONF, pcie_rcconf, 0xfd800000, u32)\
	MMIO(PCIE_CONF_SETUP, pcie_conf_setup, 0xfda00000, u32)\
	MMIO(PCIE_ADDR_XLATION, pcie_addr_xlation, 0xfdc00000, struct rkpcie_addr_xlation)\
	MMIO(SPI1, spi1, 0xff1d0000, struct rkspi_regs)\
	MMIO(I2C4, i2c4, 0xff3d0000, struct rki2c_regs)\
	MMIO(PWM, pwm, 0xff420000, struct rkpwm_regs)\
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
#define DEFINE_REGMAP64K(X)\
	X(PMUSGRF, pmusgrf, 0xff330000, u32)\
	X(GRF, grf, 0xff770000, u32)\

#include "vmmap.h"
