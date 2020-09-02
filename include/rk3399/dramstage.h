/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

#define DRAM_START ((u64)0)
#define TZRAM_SIZE 0x00200000

HEADER_FUNC u32 dram_size() {return 0xf8000000;}

void boot_sd();
void boot_emmc();
void boot_spi();

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
#define DEFINE_BOOT_MEDIUM X(SD) X(EMMC) X(SPI)
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

#define DEFINE_DRAMSTAGE_VSTACKS X(SD) X(EMMC) X(SPI)

enum dramstage_vstack {
#define X(name) DRAMSTAGE_VSTACK_##name,
	DEFINE_DRAMSTAGE_VSTACKS
#undef X
	NUM_DRAMSTAGE_VSTACK
};

HEADER_FUNC u64 vstack_base(enum dramstage_vstack vstack) {
	return 0x100006000 + 0x2000 * vstack;
}
