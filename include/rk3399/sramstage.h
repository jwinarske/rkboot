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

#define DEFINE_SRAMSTAGE_VSTACKS\
	X(DDRC0) X(DDRC1) X(SDMMC) X(EMMC) X(PCIE)

enum sramstage_vstack {
#define X(name) SRAMSTAGE_VSTACK_##name,
	DEFINE_SRAMSTAGE_VSTACKS
#undef X
	NUM_SRAMSTAGE_VSTACK
};

HEADER_FUNC u64 vstack_base(enum sramstage_vstack vstack) {
	return 0xffe01400 + 0x2000 * vstack;
}

#define DEFINE_REGMAP\
	MMIO(EMMC, emmc, 0xfe330000, struct sdhci_regs)\
	MMIO(SDMMC, sdmmc, 0xfe320000, struct dwmmc_regs)\
	MMIO(CIC, cic, 0xff620000, u32)\
	MMIO(STIMER0, stimer0, 0xff860000, struct rktimer_regs)\
	MMIO(CRYPTO1, crypto1, 0xff8b8000, struct rkcrypto_v1_regs)\
	MMIO(PCIE_CLIENT, pcie_client, 0xfd000000, u32)\
	MMIO(PCIE_MGMT, pcie_mgmt, 0xfd900000, u32)\
	MMIO(PCIE_CONF_SETUP, pcie_conf_setup, 0xfda00000, u32)\

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
