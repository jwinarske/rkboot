/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

#define DRAM_START ((u64)0)
#define TZRAM_SIZE 0x00200000

HEADER_FUNC u32 dram_size() {return 0xf8000000;}

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
struct stage_store;
struct fdt_header;

struct fdt_addendum {
	u64 initcpio_start, initcpio_end, dram_start, dram_size;
	u32 *entropy;
	size_t entropy_words;
};

void transform_fdt(const struct fdt_header *header, void *input_end, void *dest, const struct fdt_addendum *info);
_Noreturn void commit(struct payload_desc *payload, struct stage_store *store);

/* this enumeration defines the boot order */
#define DEFINE_BOOT_MEDIUM X(SD) X(EMMC) X(NVME) X(SPI)
enum boot_medium {
#define X(name) BOOT_MEDIUM_##name,
	DEFINE_BOOT_MEDIUM
#undef X
	NUM_BOOT_MEDIUM,
	BOOT_CUE_NONE = NUM_BOOT_MEDIUM,
	BOOT_CUE_EXIT,
};
_Bool wait_for_boot_cue(enum boot_medium);
void boot_medium_loaded(enum boot_medium);
void boot_medium_exit(enum boot_medium);

#define DEFINE_DRAMSTAGE_VSTACKS X(SD) X(EMMC) X(NVME) X(SPI)

enum dramstage_vstack {
#define X(name) DRAMSTAGE_VSTACK_##name,
	DEFINE_DRAMSTAGE_VSTACKS
#undef X
	NUM_DRAMSTAGE_VSTACK
};

HEADER_FUNC u64 vstack_base(enum dramstage_vstack vstack) {
	return 0x100008000 + 0x4000 * vstack;
}

#define DEFINE_REGMAP\
	MMIO(STIMER0, stimer0, 0xff860000, struct rktimer_regs)\
	MMIO(CRYPTO1, crypto1, 0xff8b8000, struct rkcrypto_v1_regs)\
	MMIO(SDMMC, sdmmc, 0xfe320000, struct dwmmc_regs)\
	MMIO(EMMC, emmc, 0xfe330000, struct sdhci_regs)\
	MMIO(PCIE_CLIENT, pcie_client, 0xfd000000, u32)\
	MMIO(PCIE_MGMT, pcie_mgmt, 0xfd900000, u32)\
	MMIO(PCIE_RCCONF, pcie_rcconf, 0xfd800000, u32)\
	MMIO(PCIE_CONF_SETUP, pcie_conf_setup, 0xfda00000, u32)\
	MMIO(PCIE_ADDR_XLATION, pcie_addr_xlation, 0xfdc00000, struct rkpcie_addr_xlation)\

enum regmap_id {
#define MMIO(name, snake, addr, type) REGMAP_##name,
	DEFINE_REGMAP
#undef MMIO
	NUM_REGMAP
};

#define REGMAP_BASE(map) (void *)(uintptr_t)(0xfffff000 - 0x1000 * map)

HEADER_FUNC void *regmap_base(enum regmap_id map) {
	return REGMAP_BASE(map);
}

#define MMIO(name, snake, addr, type) static volatile type UNUSED *const regmap_##snake = REGMAP_BASE(REGMAP_##name);
	DEFINE_REGMAP
#undef MMIO
