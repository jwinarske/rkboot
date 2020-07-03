/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

struct payload_desc {
	u8 *elf_start, *elf_end;
	u8 *fdt_start, *fdt_end;
	u8 *kernel_start, *kernel_end;
#if CONFIG_ELFLOADER_INITCPIO
	u8 *initcpio_start, *initcpio_end;
#endif
};

struct async_transfer;
void decompress_payload(struct async_transfer *async, struct payload_desc *payload);
