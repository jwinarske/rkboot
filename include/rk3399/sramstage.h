/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

void pmu_cru_setup();
void rk3399_init_sdmmc();
struct sdhci_state;
void emmc_init(struct sdhci_state *st);

struct stage_store;
u32 end_sramstage(struct stage_store *store);

#define DEFINE_SRAMSTAGE_VSTACKS\
	X(DDRC0) X(DDRC1) X(SDMMC) X(EMMC)

enum sramstage_vstack {
#define X(name) SRAMSTAGE_VSTACK_##name,
	DEFINE_SRAMSTAGE_VSTACKS
#undef X
	NUM_SRAMSTAGE_VSTACK
};

HEADER_FUNC u64 vstack_base(enum sramstage_vstack vstack) {
	return 0x100005400 + 0x2000 * vstack;
}
