// SPDX-License-Identifier: CC0-1.0
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tools.h"

_Noreturn int allocation_failed();

struct segment {
	// The first `size` bytes starting at `first` are filled with the
	// file data stored behind `buf`. The bytes after that until
	// and including `last_init` are filled with null bytes. The
	// bytes after that until and including `last` are not
	// initialized and can be used for bounce buffers, but not
	// for other segments.
	uint64_t first, last_init, last;
	const uint8_t *buf;
	uint32_t offset;
	size_t size;
	uint8_t alignment;
#define SEG_ADDR_FIXED 0xff
	uint8_t role;
	// no special semantics, just load it into RAM
#define SEG_ROLE_LOAD 0
	// the main executable
#define SEG_ROLE_KERNEL 1
	// a firmware executable that will give control to the kernel
#define SEG_ROLE_BL31 2
	// a device tree blob. The space after last_init is used to
	// construct the version with runtime info attached.
#define SEG_ROLE_DTB 3
};

struct rel_addr {
	size_t segment;
	uint64_t offset;
};

struct context {
	DECL_VEC(struct segment, segments);
	uint8_t offset_alignment;
	size_t *processing_order;
	struct rel_addr bl31_entry;
	struct rel_addr kernel_entry;
	size_t initrd;
	size_t fdt, fdt_transformed;
	char *bootargs, *bootargs_end;
	unsigned have_bl31 : 1;
	unsigned have_kernel : 1;
	unsigned have_fdt : 1;
	unsigned have_initrd : 1;
};

bool load_elf(struct context *ctx, const uint8_t *buf, size_t size, struct rel_addr *entry);
void dump_segment(const struct segment *);
void layout_segments(struct context *ctx);
