/* SPDX-License-Identifier: CC0-1.0 */
#include <main.h>
#include <uart.h>
#include <rk3399.h>
#include <stage.h>
#include <compression.h>
#include "fdt.h"
#include ATF_HEADER_PATH

static image_info_t bl33_image = {
	.image_base = 0x00600000,
	.image_size = 0x01000000,
	.image_max_size = 0x01000000,
};

static entry_point_info_t bl33_ep = {
	.h = {
		.type = PARAM_EP,
		.version = PARAM_VERSION_1,
		.size = sizeof(bl33_ep),
		.attr = EP_NON_SECURE,
	},
};

static bl_params_node_t bl33_node = {
	.image_id = BL33_IMAGE_ID,
	.image_info = &bl33_image,
	.ep_info = &bl33_ep,
};

static bl_params_t bl_params = {
	.h = {
		.type = PARAM_BL_PARAMS,
		.version = PARAM_VERSION_2,
		.size = sizeof(bl_params),
		.attr = 0
	},
	.head = &bl33_node,
};

const struct mapping initial_mappings[] = {
	{.first = 0, .last = 0xf7ffffff, .type = MEM_TYPE_NORMAL},
	{.first = 0xf8000000, .last = 0xff8bffff, .type = MEM_TYPE_DEV_nGnRnE},
	{.first = 0xff8c0000, .last = 0xff8effff, .type = MEM_TYPE_NORMAL},
	{.first = 0xff8f0000, .last = 0xffffffff, .type = MEM_TYPE_DEV_nGnRnE},
	{.first = 0, .last = 0, .type = 0}
};

const struct address_range critical_ranges[] = {
	{.first = __start__, .last = __end__},
	{.first = uart, .last = uart},
	ADDRESS_RANGE_INVALID
};

static u64 elf_magic[3] = {
	0x00010102464c457f,
	0,
	0x0000000100b70002
};

struct elf_header {
	u64 magic[3];
	u64 entry;
	u64 prog_h_off;
	u64 sec_h_off;
	u32 flags;
	u16 elf_h_size;
	u16 prog_h_entry_size;
	u16 num_prog_h;
	u16 sec_h_entry_size;
	u16 num_sec_h;
	u16 sec_h_str_idx;
};

struct program_header {
	u32 type;
	u32 flags;
	u64 offset;
	u64 vaddr;
	u64 paddr;
	u64 file_size;
	u64 mem_size;
	u64 alignment;
};

typedef void (*bl31_entry)(bl_params_t *, u64, u64, u64);

static void write_be64(be64 *x, u64 val) {
	x->v = __builtin_bswap64(val);
}

static void write_be32(be32 *x, u32 val) {
	x->v = __builtin_bswap32(val);
}

static void load_elf(const struct elf_header *header) {
	for_range(i, 0, 16) {
		const u32 *x = (u32*)((u64)header + 16*i);
		printf("%2x0: %08x %08x %08x %08x\n", i, x[0], x[1], x[2], x[3]);
	}
	for_array(i, elf_magic) {
		assert_msg(header->magic[i] == elf_magic[i], "value 0x%016zx at offset %u != 0x%016zx", header->magic[i], 8*i, elf_magic[i]);
	}
	printf("Loading ELF: entry address %zx, %u program headers at %zx\n", header->entry, header->num_prog_h, header->prog_h_off);
	assert(header->prog_h_entry_size == 0x38);
	assert((header->prog_h_off & 7) == 0);
	for_range(i, 0, header->num_prog_h) {
		const struct program_header *ph = (const struct program_header*)((u64)header + header->prog_h_off + header->prog_h_entry_size * i);
		if (ph->type == 0x6474e551) {puts("ignoring GNU_STACK segment\n"); continue;}
		assert_msg(ph->type == 1, "found unexpected segment type %08x\n", ph->type);
		printf("LOAD %08zx…%08zx → %08zx\n", ph->offset, ph->offset + ph->file_size, ph->vaddr);
		assert(ph->vaddr == ph->paddr);
		assert(ph->flags == 7);
		assert(ph->offset % ph->alignment == 0);
		assert(ph->vaddr % ph->alignment == 0);
		u64 alignment = ph->alignment;
		assert(alignment % 16 == 0);
		const u64 *src = (const u64 *)((u64)header + ph->offset), *end = src + ((ph->file_size + alignment - 1) / alignment * (alignment / 8));
		u64 *dest = (u64*)ph->vaddr;
		while (src < end) {*dest++ = *src++;}
	}
}

static void copy_subtree(const be32 **const src, be32 **const dest, const be32 *src_end) {
	u32 depth = 0;
	u32 cmd;
	assert(*src + 1 <= src_end);
	do {
		cmd = read_be32((*src)++);
		assert(*src <= src_end);
		write_be32((*dest)++, cmd);
		if (cmd == 1) {
			depth += 1;
			u32 v; do {
				assert(*src + 2 <= src_end);
				v = (*src)++->v;
				(*dest)++->v = v;
			} while(!has_zero_byte(v));
		} else if (cmd == 3) {
			assert(*src + 3 <= src_end);
			u32 size = read_be32((*src));
			size = (size+3)/4 + 2; /* include size and property name offset */
			assert(*src + size + 1 < src_end);
			while(size--) {
				(*dest)++->v = (*src)++->v;
			}
		} else {
			assert(cmd == 2);
			assert(depth > 0);
			depth -= 1;
		}
	} while (depth > 0);
}

static char *memcpy(char *dest, const char *src, size_t len) {
	char *ret = dest;
	while (len--) {
		*dest++ = *src++;
	}
	return ret;
}

#define DRAM_START 0
#define TZRAM_SIZE 0x00200000

static u32 dram_size() {return 0xf8000000;}

static void transform_fdt(const struct fdt_header *header, void *dest) {
	const u32 src_size = read_be32(&header->totalsize);
	const u64 src_end = (u64)header + src_size;
	const u32 version = read_be32(&header->version);
	const u32 compatible = read_be32(&header->last_compatible_version);
	assert(version >= compatible);
	assert(compatible <= 17 && compatible >= 16);
	be64 *dest_rsvmap = (be64 *)((u64)dest + ((sizeof(struct fdt_header) + 7) & ~(u64)7));
	const be64 *src_rsvmap = (const be64 *)((u64)header + read_be32(&header->reserved_offset));
	while (1) {
		assert((u64)(src_rsvmap + 2) <= src_end);
		u64 start = src_rsvmap++->v;
		u64 length = src_rsvmap++->v;
		if (!length) {	/* insert fdt address entry at the end */
			write_be64(dest_rsvmap++, (u64)dest);
			dest_rsvmap++->v = 0;
			dest_rsvmap++->v = 0;
			dest_rsvmap++->v = 0;
			break;
		}
		dest_rsvmap++->v = start;
		dest_rsvmap++->v = length;
	}
	be32 *dest_struct = (be32 *)dest_rsvmap, *dest_struct_start = dest_struct;
	const be32 *src_struct = (const be32*)((u64)header + read_be32(&header->struct_offset));
	u32 addr_cells = 0, size_cells = 0;
	const be32 *src_struct_end = version >= 17 ? src_struct + read_be32(&header->struct_size)/4 : (const be32*)header + src_size/4;
	assert(read_be32(src_struct++) == 1 && src_struct++->v == 0);
	write_be32(dest_struct++, 1);
	dest_struct++->v = 0;
	const char *src_string = (const char*)((u64)header + read_be32(&header->string_offset));
	u32 cmd;
	while ((cmd = read_be32(src_struct)) == 3) {
		assert((u64)(src_struct + 3) < src_end);
		u32 size = read_be32(src_struct + 1);
		const char *name = src_string + read_be32(src_struct + 2);
		debug("prop %s\n", name);
		if (!strcmp("#address-cells", name)) {
			puts("addr\n");
			assert(addr_cells == 0 && size == 4);
			addr_cells = read_be32(src_struct + 3);
		} else if (!strcmp("#size-cells", name)) {
			puts("size\n");
			assert(size_cells == 0 && size == 4);
			size_cells = read_be32(src_struct + 3);
		}
		assert((u64)(src_struct + (size+3)/4 + 3) < src_end); /* plus the next command */
		for_range(i, 0, (size+3)/4 + 3) {
			dest_struct++->v = src_struct++->v;
		}
	}
	assert(addr_cells >= 1 && size_cells >= 1);

	while ((cmd = read_be32(src_struct)) == 1) {
		if (read_be32(src_struct + 1) == 0x63686f73 && read_be32(src_struct + 2) == 0x656e0000) { /* "chosen" */
			break;
		}
		copy_subtree(&src_struct, &dest_struct, src_struct_end);
	}

	static const char device_type[] = {'d', 'e', 'v', 'i', 'c', 'e', '_', 't', 'y', 'p', 'e', 0};
	static const u32 memory_node1[] = {
		1, 0x6d656d6f, 0x72790000, 3, 7, 0 /*overwritten later*/, 0x6d656d6f, 0x72790000, 3
	};
	for_array(i, memory_node1) {write_be32(dest_struct+i, memory_node1[i]);}
	u32 string_size = read_be32(&header->string_size);
	write_be32(dest_struct+5, string_size);
	dest_struct += ARRAY_SIZE(memory_node1);
	write_be32(dest_struct++, (addr_cells + size_cells) * 4);
	write_be32(dest_struct++, string_size + sizeof(device_type));
	while (--addr_cells) {dest_struct++->v = 0;}
	write_be32(dest_struct++, DRAM_START + TZRAM_SIZE);
	while (--size_cells) {dest_struct++->v = 0;}
	write_be32(dest_struct++, dram_size() - TZRAM_SIZE);
	write_be32(dest_struct++, 2);
	while (read_be32(src_struct) == 1) {copy_subtree(&src_struct, &dest_struct, src_struct_end);}
	assert(read_be32(src_struct) == 2);
	write_be32(dest_struct++, 2);

	char *dest_string = (char *)dest_struct, *dest_string_start = dest_string;
	memcpy(dest_string, src_string, string_size);
	dest_string += string_size;
	memcpy(dest_string, device_type, sizeof(device_type));
	dest_string += sizeof(device_type);
	memcpy(dest_string, "reg", 4);
	dest_string += 4;
	struct fdt_header *dest_header = dest;
	write_be32(&dest_header->magic, 0xd00dfeed);
	u32 totalsize = (u32)(dest_string - (char *)dest);
	write_be32(&dest_header->totalsize, totalsize);
	write_be64(dest_rsvmap - 3, totalsize);
	write_be32(&dest_header->string_offset, (u32)((char*)dest_string_start - (char *)dest));
	write_be32(&dest_header->struct_offset, (u32)((char*)dest_struct_start - (char *)dest));
	write_be32(&dest_header->reserved_offset, (u32)((sizeof(struct fdt_header) + 7) & ~(u64)7));
	write_be32(&dest_header->version, 17);
	write_be32(&dest_header->last_compatible_version, 16);
	dest_header->boot_cpu.v = header->boot_cpu.v;
	write_be32(&dest_header->string_size, (u32)(dest_string - dest_string_start));
	write_be32(&dest_header->struct_size, (u32)(dest_struct - dest_struct_start)*4);
}

#ifdef CONFIG_ELFLOADER_DECOMPRESSION
extern const struct decompressor gzip_decompressor;

const u8 *decompress(const u8 *data, const u8 *end, u8 *out, u8 *out_end) {
	size_t size;
	u64 start = get_timestamp();
	if (gzip_decompressor.probe(data, end, &size) < COMPR_PROBE_LAST_SUCCESS) {
		info("gzip probed\n");
		struct decompressor_state *state = (struct decompressor_state *)end;
		assert(data = gzip_decompressor.init(state, data, end));
		state->out = state->window_start = out;
		state->out_end = out_end;
		while (state->decode) {
			size_t res = state->decode(state, data, end);
			assert_msg(res >= NUM_DECODE_STATUS, "decompression failed, status: %zu\n", res);
			data += res - NUM_DECODE_STATUS;
		}
		info("decompressed %zu bytes in %zu μs\n", state->out - out, (get_timestamp() - start) / CYCLES_PER_MICROSECOND);
	} else {die("couldn't probe");}
	return data;
}
#endif

_Noreturn u32 ENTRY main() {
	puts("elfloader\n");
	struct stage_store store;
	stage_setup(&store);
	setup_mmu();
	u64 elf_addr = 0x00200000, fdt_addr = 0x00500000, fdt_out_addr = 0x00580000, payload_addr = 0x00680000, UNUSED blob_addr = 0x02000000;
#ifdef CONFIG_ELFLOADER_DECOMPRESSION
	u8 *blob = (u8 *)blob_addr, *blob_end = blob + (16 << 20);
#ifdef CONFIG_ELFLOADER_SPI
	u64 xfer_start = get_timestamp();
	spi_read_flash(blob, blob_end - blob);
	u64 xfer_end = get_timestamp();
	printf("transfer finished after %zu μs\n", (xfer_end - xfer_start) / CYCLES_PER_MICROSECOND);
#endif
	const u8 *blob2 = decompress(blob, blob_end, (u8 *)elf_addr, (u8 *)fdt_addr);
	blob2 = decompress(blob2, blob_end, (u8 *)fdt_addr, (u8 *)fdt_out_addr);
	blob2 = decompress(blob2, blob_end, (u8 *)payload_addr, (u8 *)blob_addr);
#endif
	const struct elf_header *header = (const struct elf_header*)elf_addr;
	load_elf(header);

	transform_fdt((const struct fdt_header *)fdt_addr, (void *)fdt_out_addr);

	bl33_ep.pc = payload_addr;
	bl33_ep.spsr = 9; /* jump into EL2 with SPSel = 1 */
	bl33_ep.args.arg0 = fdt_out_addr;
	bl33_ep.args.arg1 = 0;
	bl33_ep.args.arg2 = 0;
	bl33_ep.args.arg3 = 0;
	setup_pll(cru + CRU_BPLL_CON, 297);
	stage_teardown(&store);
	while (~uart->line_status & 0x60) {__asm__ volatile("yield");}
	uart->shadow_fifo_enable = 0;
	((bl31_entry)header->entry)(&bl_params, 0, 0, 0);
	puts("return\n");
	halt_and_catch_fire();
}
