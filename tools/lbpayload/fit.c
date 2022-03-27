// SPDX-License-Identifier: CC0-1.0
#include "lbpayload.h"
#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static void wbe32(uint8_t *buf, uint32_t v) {
	buf[0] = v >> 24;
	buf[1] = v >> 16 & 0xff;
	buf[2] = v >> 8 & 0xff;
	buf[3] = v & 0xff;
}

struct outbuf {
	uint8_t *start, *write, *end;
};
static void require_space(struct outbuf *buf, size_t want) {
	size_t space = buf->end - buf->write;
	if (space >= want) {return;}
	size_t cap = buf->end - buf->start, len = buf->write - buf->start;
	size_t new_cap; do {
		new_cap = cap * 2;
		if (new_cap < cap) {abort();}
	} while (new_cap - len < want);
	uint8_t *new_buf = realloc(buf->start, new_cap);
	if (!new_buf) {abort();}
	buf->start = new_buf;
	buf->write = new_buf + len;
	buf->end = new_buf + new_cap;
}
static void write_prop(struct outbuf *buf, uint32_t name_offset, const uint8_t *value, size_t val_size) {
	if (val_size > 0xfffffffc) {abort();}
	size_t aligned_size = (val_size + 12 + 3) & ~(size_t)3;
	require_space(buf, aligned_size);
	wbe32(buf->write, 3);
	wbe32(buf->write + 4, val_size);
	wbe32(buf->write + 8, name_offset);
	buf->write += 12;
	memcpy(buf->write, value, val_size);
	buf->write += val_size;
	for (size_t i = 0; i < aligned_size - 12 - val_size; ++i) {
		*buf->write++ = 0;
	}
}
static void write_node_start(struct outbuf *buf, const char *name) {
	size_t len = strlen(name);
	assert(len <= SIZE_MAX - 8);
	size_t aligned = (len + 1 + 4 + 3) & ~(size_t)3;
	require_space(buf, aligned);
	wbe32(buf->write, 1);
	buf->write += 4;
	memcpy(buf->write, name, len);
	buf->write += len;
	for (size_t i = 0; i < aligned - len - 4; ++i) {
		*buf->write++ = 0;
	}
}
static void write_node_end(struct outbuf *buf) {
	require_space(buf, 4);
	wbe32(buf->write, 2);
	buf->write += 4;
}

#define DEFINE_STR(X)\
	X(TIMESTAMP, "timestamp")\
	X(ADDR_CELLS, "#address-cells")\
	X(DESCRIPTION, "description")\
	X(TYPE, "type")\
	X(COMPRESSION, "compression")\
	X(OS, "os")\
	X(ARCH, "arch")\
	X(ENTRY, "entry")\
	X(LOAD, "load")\
	X(DATA_OFFSET, "data-offset")\
	X(DATA_SIZE, "data-size")\

enum {
#define X(name, text) STR_OFF_##name, STR_LAST_##name = STR_OFF_##name + sizeof(text) - 1,
	DEFINE_STR(X)
#undef X
};

#define X(name, text) text "\0"
static unsigned char string_section[] = DEFINE_STR(X);
#undef X

void write_itb(const struct context *ctx, FILE *out) {
	struct outbuf buf = {
		.start = calloc(512, 1),
	};
	if (!buf.start) {abort();}
	buf.write = buf.start;
	wbe32(buf.start, 0xd00dfeed);
	// we'll write totalsize later
	wbe32(buf.start + 8, 56);	// off_dt_struct
	// we'll write off_dt_strings later
	wbe32(buf.start + 16, 40);	// off_dt_rsvmap
	wbe32(buf.start + 20, 17);	// version
	wbe32(buf.start + 24, 16);	// last_comp_version
	wbe32(buf.start + 28, 0);	// boot_cpuid_phys
	wbe32(buf.start + 32, sizeof(string_section));
	// we'll write size_dt_struct later
	memset(buf.start + 40, 0, 16);	// dummy rsvmap
	buf.write = buf.start + 56;
	buf.end = buf.start + 512;
	write_node_start(&buf, "");
	uint8_t cell[4];
	wbe32(cell, 2);
	write_prop(&buf, STR_OFF_ADDR_CELLS, cell, 4);
	// I don't care about the value of the timestamp field.
	// It's here because the 'spec' says it has to, not because I
	// find it a helpful part of the format.
	wbe32(cell, 1);
	write_prop(&buf, STR_OFF_TIMESTAMP, cell, 4);
	write_prop(&buf, STR_OFF_DESCRIPTION, (const uint8_t*)"bla", 4);
	write_node_start(&buf, "images");
	char node_name[32];
	for (size_t i_seg = 0; i_seg < ctx->segments_size; ++i_seg) {
		const struct segment *seg = ctx->segments + i_seg;
		int print_res;
		if (i_seg == ctx->initrd) {
			write_node_start(&buf, "initrd");
			write_prop(&buf, STR_OFF_TYPE, (const uint8_t*)"ramdisk", 8);
			write_prop(&buf, STR_OFF_OS, (const uint8_t*)"linux", 6);
		} else if (seg->role == SEG_ROLE_LOAD || seg->role == SEG_ROLE_BL31) {
			print_res = snprintf(node_name, sizeof(node_name), "elf-seg-%"PRIx64, seg->first);
			if (print_res < 0 || print_res >= sizeof(node_name)) {abort();}
			write_node_start(&buf, node_name);
			write_prop(&buf, STR_OFF_TYPE, (const uint8_t*)"firmware", 9);
		} else if (seg->role == SEG_ROLE_KERNEL) {
			write_node_start(&buf, "kernel");
			write_prop(&buf, STR_OFF_TYPE, (const uint8_t*)"kernel", 7);
			write_prop(&buf, STR_OFF_OS, (const uint8_t*)"linux", 6);
			write_prop(&buf, STR_OFF_ARCH, (const uint8_t*)"arm64", 6);
		} else if (seg->role == SEG_ROLE_DTB) {
			write_node_start(&buf, "dtb");
			write_prop(&buf, STR_OFF_TYPE, (const uint8_t*)"flat_dt", 8);
		} else {abort();}
		uint8_t addr[8];
		wbe32(addr, seg->first >> 32);
		wbe32(addr + 4, seg->first & 0xffffffff);
		write_prop(&buf, STR_OFF_LOAD, addr, 8);
		bool have_entry = false;
		uint64_t entry_addr;
		if (ctx->have_bl31 && ctx->bl31_entry.segment == i_seg) {
			have_entry = true;
			entry_addr = seg->first + ctx->bl31_entry.offset;
		} else if (ctx->have_kernel && ctx->kernel_entry.segment == i_seg) {
			have_entry = true;
			entry_addr = seg->first + ctx->kernel_entry.offset;
		}
		if (have_entry) {
			wbe32(addr, entry_addr >> 32);
			wbe32(addr + 4, entry_addr & 0xffffffff);
			write_prop(&buf, STR_OFF_ENTRY, addr, 8);
		}
		wbe32(cell, seg->size);
		write_prop(&buf, STR_OFF_DATA_SIZE, cell, 4);
		wbe32(cell, seg->offset);
		write_prop(&buf, STR_OFF_DATA_OFFSET, cell, 4);
		write_prop(&buf, STR_OFF_COMPRESSION, (const uint8_t*)"none", 5);
		// another useless property. whatever.
		write_prop(&buf, STR_OFF_DESCRIPTION, (const uint8_t *)"bla", 4);
		write_node_end(&buf);
	}
	write_node_end(&buf);
	write_node_end(&buf);
	require_space(&buf, 4);
	wbe32(buf.write, 9);
	buf.write += 4;

	uint32_t string_off = buf.write - buf.start;
	wbe32(buf.start + 12, string_off);	// off_dt_strings
	wbe32(buf.start + 36, string_off - 56);	// size_dt_struct
	require_space(&buf, sizeof(string_section));
	memcpy(buf.write, string_section, sizeof(string_section));
	buf.write += sizeof(string_section);

	size_t totalsize = buf.write - buf.start;
	uint32_t offset_align_mask = UINT32_C(0xffffffff) << ctx->offset_alignment;
	if (totalsize >= offset_align_mask) {exit(4);}
	uint32_t padding = (UINT32_C(1) << ctx->offset_alignment) - (totalsize & ~offset_align_mask);
	require_space(&buf, padding);
	memset(buf.write, 0, padding);
	buf.write += padding;
	totalsize += padding;
	assert(totalsize <= 0xffffffff);
	wbe32(buf.start + 4, totalsize);

	void *padding_bytes = calloc(1 << ctx->offset_alignment, 1);
	if (!padding_bytes) {abort();}

	size_t written = 0;
	while (written < totalsize) {
		size_t res = fwrite(buf.start + written, 1, totalsize - written, out);
		if (!res) {perror("While writing FIT structure"); exit(2);}
		written += res;
	}

	for (size_t i_seg = 0; i_seg < ctx->segments_size; ++i_seg) {
		const struct segment *seg = ctx->segments + i_seg;
		written = 0;
		while (written < seg->size) {
			size_t res = fwrite(seg->buf + written, 1, seg->size - written, out);
			if (!res) {perror("While writing image"); exit(2);}
			written += res;
		}
		while (written & ~offset_align_mask) {
			size_t res = fwrite(padding_bytes, 1, (1 << ctx->offset_alignment) - (written & ~offset_align_mask), out);
			if (!res) {perror("While writing padding"); exit(2);}
			written += res;
		}
	}
}
