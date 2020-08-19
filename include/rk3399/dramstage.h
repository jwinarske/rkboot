/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

#define DRAM_START ((u64)0)
#define TZRAM_SIZE 0x00200000

HEADER_FUNC u32 dram_size() {return 0xf8000000;}
struct fdt_header;
void transform_fdt(const struct fdt_header *header, void *input_end, void *dest, void *initcpio_start, void *initcpio_end, u64 dram_start, u64 dram_size);


struct async_transfer;
struct payload_desc;
void decompress_payload(struct async_transfer *async, struct payload_desc *payload);
struct stage_store;
_Noreturn void commit(struct payload_desc *payload, struct stage_store *store);
