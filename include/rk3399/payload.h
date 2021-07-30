/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <async.h>

struct payload_desc {
	u8 *elf_start, *elf_end;
	u8 *fdt_start, *fdt_end;
	u8 *kernel_start, *kernel_end;
#if CONFIG_DRAMSTAGE_INITCPIO
	u8 *initcpio_start, *initcpio_end;
#endif
};

static const u64 elf_addr = 0x04200000, fdt_addr = 0x00100000, fdt_out_addr = 0x00180000, payload_addr = 0x00280000;
static const u64 blob_addr = 0x04400000;
static const u64 initcpio_addr = 0x08000000;

static const struct async_buf blob_buffer = {(u8 *)blob_addr, (u8 *)initcpio_addr};
