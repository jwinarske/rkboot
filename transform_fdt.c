#include <assert.h>
#include <fdt.h>
#include <log.h>
#include <die.h>

static void write_be64(be64 *x, u64 val) {
	x->v = __builtin_bswap64(val);
}

static void write_be32(be32 *x, u32 val) {
	x->v = __builtin_bswap32(val);
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

void transform_fdt(const struct fdt_header *header, void *dest, void *initcpio_start, void *initcpio_end, u64 dram_start, u64 dram_size) {
	assert_unimpl(dram_start <= 0xffffffff && dram_size <= 0xffffffff, "64-bit DRAM address/size");
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
		if (!length) {
			if (initcpio_start) {
				assert(initcpio_end);
				write_be64(dest_rsvmap++, (u64)initcpio_start);
				write_be64(dest_rsvmap++, (u64)(initcpio_end - initcpio_start));
			}
			/* insert fdt address entry at the end */
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

	u32 string_size = read_be32(&header->string_size);
	static const char initrd_start[] = "linux,initrd-start", initrd_end[] = "linux,initrd-end";

	_Bool have_memory = 0, have_chosen = 0;
	while ((cmd = read_be32(src_struct)) == 1) {
		assert((u64)(src_struct + 3) <= src_end);
		if (read_be32(src_struct + 1) == 0x63686f73 && read_be32(src_struct + 2) == 0x656e0000) { /* "chosen" */
			have_chosen = 1;
			for_range(i, 0, 3) {dest_struct++->v = src_struct++->v;}
			while (read_be32(src_struct) == 3) {
				copy_subtree(&src_struct, &dest_struct, src_struct_end);
				assert((u64)src_struct < src_end);
			}
			if (initcpio_start) {
				write_be32(dest_struct++, 3);
				write_be32(dest_struct++, 4 * addr_cells);
				write_be32(dest_struct++, string_size);
				for_range(i, 0, addr_cells - 1) {dest_struct++->v = 0;}
				write_be32(dest_struct++, (u32)(u64)initcpio_start);
				write_be32(dest_struct++, 3);
				write_be32(dest_struct++, 4 * addr_cells);
				write_be32(dest_struct++, string_size + sizeof(initrd_start));
				for_range(i, 0, addr_cells - 1) {dest_struct++->v = 0;}
				write_be32(dest_struct++, (u32)(u64)initcpio_end);
			}
			assert(read_be32(src_struct++) == 2);
			write_be32(dest_struct++, 2);
			break;
		} else if (read_be32(src_struct + 1) == 0x6d656d6f && read_be32(src_struct + 2) == 0x72790000) {
			have_memory = 1;
		}
		copy_subtree(&src_struct, &dest_struct, src_struct_end);
		assert((u64)src_struct < src_end);
	}
	assert_unimpl(have_chosen/* || !initcpio_start*/, "inserting a /chosen node");
	while (read_be32(src_struct) == 1) {
		copy_subtree(&src_struct, &dest_struct, src_struct_end);
	}
	assert(read_be32(src_struct) == 2);

	static const char device_type[] = "device_type";
	static const u32 memory_node1[] = {
		1, 0x6d656d6f, 0x72790000, 3, 7, 0 /*overwritten later*/, 0x6d656d6f, 0x72790000, 3
	};
	if (!have_memory) {
		const u32 str_offset = string_size + sizeof(initrd_start) + sizeof(initrd_end);
		for_array(i, memory_node1) {write_be32(dest_struct+i, memory_node1[i]);}
		write_be32(dest_struct+5, str_offset);
		dest_struct += ARRAY_SIZE(memory_node1);
		write_be32(dest_struct++, (addr_cells + size_cells) * 4);
		write_be32(dest_struct++, str_offset + sizeof(device_type));
		for_range(i, 0, addr_cells - 1) {dest_struct++->v = 0;}
		write_be32(dest_struct++, dram_start);
		for_range(i, 0, size_cells - 1) {dest_struct++->v = 0;}
		write_be32(dest_struct++, dram_size);
		write_be32(dest_struct++, 2);
	}

	write_be32(dest_struct++, 2);

	char *dest_string = (char *)dest_struct, *dest_string_start = dest_string;
	memcpy(dest_string, src_string, string_size);
	dest_string += string_size;
	memcpy(dest_string, initrd_start, sizeof(initrd_start));
	dest_string += sizeof(initrd_start);
	memcpy(dest_string, initrd_end, sizeof(initrd_end));
	dest_string += sizeof(initrd_end);
	if (!have_memory) {
		memcpy(dest_string, device_type, sizeof(device_type));
		dest_string += sizeof(device_type);
		memcpy(dest_string, "reg", 4);
		dest_string += 4;
	}

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
