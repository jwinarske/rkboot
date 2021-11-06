#include <rk3399/dramstage.h>
#include <assert.h>
#include <stdbool.h>

#include <fdt.h>
#include <log.h>
#include <die.h>

#define DEFINE_INSSTR(X)\
	X(DEVICE_TYPE, "device_type")\
	X(REG, "reg")\
	X(INITRD_START, "linux,initrd-start")\
	X(INITRD_END, "linux,initrd-end")\
	X(KASLR_SEED, "kaslr-seed")\
	X(RNG_SEED, "rng-seed")

enum {
#define X(name, str) INSSTR_##name,
	DEFINE_INSSTR(X)
#undef X
	NUM_INSSTR
};
enum {
#define X(name, str) INSSTR_OFF_##name, INSSTR_END_##name = INSSTR_OFF_##name + sizeof(str) - 1,
	DEFINE_INSSTR(X)
#undef X
};

static char inserted_strings[] =
#define X(name, str) str "\0"
	DEFINE_INSSTR(X)
#undef X
;

static u32 *insert_chosen_props(u32 *out, u32 *out_end, const struct fdt_addendum *info, u32 addr_cells, u32 string_offset) {
	info("%zu words of entropy\n", info->entropy_words);
	if (info->initcpio_start != info->initcpio_end) {
		if (out_end - out < 6) {return 0;}
		if ((out_end - out) - 6 < 2 * addr_cells) {return 0;}
		*out++ = be32(3);
		*out++ = be32(4 * addr_cells);
		*out++ = be32(string_offset + INSSTR_OFF_INITRD_START);
		for_range(i, 0, addr_cells - 1) {*out++ = 0;}
		*out++ = be32(info->initcpio_start);
		*out++ = be32(3);
		*out++ = be32(4 * addr_cells);
		*out++ = be32(string_offset + INSSTR_OFF_INITRD_END);
		for_range(i, 0, addr_cells - 1) {*out++ = 0;}
		*out++ = be32(info->initcpio_end);
	}
	if (info->entropy_words >= 2) {
		if (out_end - out < 5) {return 0;}
		*out++ = be32(3);
		*out++ = be32(8);
		*out++ = be32(string_offset + INSSTR_OFF_KASLR_SEED);
		*out++ = info->entropy[0];
		*out++ = info->entropy[1];
	}
	if (info->entropy_words > 2) {
		if (out_end - out < 3) {return 0;}
		if ((size_t)(out_end - out) - 3 < info->entropy_words - 2) {return 0;}
		*out++ = be32(3);
		*out++ = be32(4 * (info->entropy_words - 2));
		*out++ = be32(string_offset + INSSTR_OFF_RNG_SEED);
		for_range(i, 2, info->entropy_words) {
			*out++ = info->entropy[i];
		}
	}
	return out;
}

static u32 *insert_memory_node(u32 *out, u32 *out_end, const struct fdt_addendum *info, u32 addr_cells, u32 size_cells, u32 string_size) {
	if (
		(info->dram_start > 0 && !addr_cells)
		|| (info->dram_start > 0xffffffff && addr_cells < 2)
		|| (info->dram_size > 0 && !size_cells)
		|| (info->dram_size > 0xffffffff && size_cells < 2)
		|| out_end - out < 12 + addr_cells + size_cells
	) {return 0;}
	memcpy(out, "\0\0\0\x01memory\0\0\0\0\0\x03\0\0\0\x07", 20);
	out += 5;
	*out++ = be32(string_size + INSSTR_OFF_DEVICE_TYPE);
	memcpy(out, "memory\0\0\0\0\0\x03", 12);
	out += 3;
	*out++ = be32((addr_cells + size_cells) * 4);
	*out++ = be32(string_size + INSSTR_OFF_REG);
	for_range(i, 0, addr_cells) {
		*out++ = addr_cells - i > 2 ? 0 : be32(info->dram_start >> ((addr_cells - i - 1) * 32));
	}
	for_range(i, 0, size_cells) {
		*out++ = size_cells - i > 2 ? 0 : be32(info->dram_size >> ((size_cells - i - 1) * 32));
	}
	*out++ = be32(FDT_CMD_NODE_END);
	return out;
}

bool transform_fdt(struct fdt_header *out_header, u32 *out_end, const struct fdt_header *header, const u32 *in_end, struct fdt_addendum *info) {
	// don't pass an output buffer larger than 4GiB
	assert(out_end - (u32*)out_header <= 0x40000000);

	out_header->magic = 0;

	if (be32(header->magic) != 0xd00dfeed) {return false;}
	u32 version = be32(header->version), compatible = be32(header->last_compatible_version);
	if (version < compatible || version < 16 || compatible > 17) {return false;}

	size_t words = in_end - (const u32*)header;
	u32 totalsize = be32(header->totalsize);
	if (totalsize > words * 4) {return false;}
	u32 struct_offset = be32(header->struct_offset);
	if (struct_offset % 4 != 0 || struct_offset > words * 4) {return false;}
	const u32 *toks = (const u32*)header + struct_offset / 4;
	const u32 *toks_end = in_end;
	if (version >= 17) {
		u32 struct_size = be32(header->struct_size);
		if (struct_size % 4 != 0 || struct_size / 4 > toks_end - toks) {return false;}
		toks_end = toks + struct_size / 4;
	}
	if (be32(toks[0]) != FDT_CMD_NODE || toks[1] != 0) {return false;}

	u32 string_size = be32(header->string_size);
	u32 string_offset = be32(header->string_offset);
	if (
		string_offset >= totalsize
		|| string_offset + string_size > totalsize
		|| string_offset + string_size < string_offset
	) {return false;}
	const char *str = (const char*)header + string_offset;

	u32 reserved_offset = be32(header->reserved_offset);
	if (reserved_offset % 8 != 0 || reserved_offset >= totalsize) {return false;}
	const u64 *resv = (const u64*)header + reserved_offset / 8, *resv_end = resv + (totalsize - reserved_offset) / 8;

	u32 *out = (u32*)out_header;
	if (out_end - out < 20) {return false;}
	// header is 44 bytes (11 words) long,
	// 1 word padding,
	// 16 bytes (4 words) are used to store the reserved area
	// covering the FDT itself
	memset(out + 12, 0, 16);
	out += 16;
	if (info->initcpio_start != info->initcpio_end) {
		*(u64*)out = be64(info->initcpio_start);
		out += 2;
		*(u64*)out = be64(info->initcpio_end - info->initcpio_start);
		out += 2;
	}
	while (1) {
		if (resv_end - resv < 2) {return false;}
		u64 start = *resv++, length = *resv++;
		if (out_end - out < 4) {return false;}
		if (!length) {break;}
		*(u64*)out = start;
		out += 2;
		*(u64*)out = length;
		out += 2;
	}
	memset(out, 0, 16);
	out += 4;
	out_header->reserved_offset = be32(48);
	u32 out_struct_offset = (char*)out - (char*)out_header;
	out_header->struct_offset = be32(out_struct_offset);

	char path[256];
	u32 path_len = 0, skip_len = 0;
	u32 addr_cells = 0, size_cells = 0;
	bool in_chosen = false;
	do {
		u32 size = fdt_token_size(toks, toks_end);
		if (!size) {
			info("invalid token\n");
			return false;
		}
		u32 size_words = (size + 3) / 4;
		u32 cmd = be32(*toks);
		if (cmd == FDT_CMD_NOP) {toks += 1; continue;}
		if (in_chosen && cmd != FDT_CMD_PROP) {
			out = insert_chosen_props(out, out_end, info, addr_cells, string_size);
			if (!out) {return false;}
			in_chosen = false;
		}
		if (cmd == FDT_CMD_NODE) {
			if (path_len == 1 && size == 11 && 0 == memcmp(toks + 1, "memory", 7)) {
				skip_len = path_len;
			}
			if (sizeof(path) - path_len < size - 4) {return false;}
			memcpy(path + path_len, toks + 1, size - 4);
			path_len += size - 4;
			if (path_len == 8 && 0 == memcmp(path, "\0chosen", 8)) {
				in_chosen = true;
			}
		} else if (cmd == FDT_CMD_NODE_END) {
			if (path_len > 1) {
				// the first byte of the path should always be
				// a zero, so this will not overrun
				while (path[--path_len - 1]) {}
			} else if (path_len == 1) {
				out = insert_memory_node(out, out_end, info, addr_cells, size_cells, string_size);
				if (!out) {return false;}
				path_len = 0;
			} else {return false;}
		} else {
			assert(cmd == FDT_CMD_PROP);
			u32 offset = be32(toks[2]);
			if (offset >= string_size) {return false;}
			if (path_len == 1 && size == 16) {
				if (0 == strncmp(str + offset, "#address-cells", string_size - offset)) {
					if (addr_cells) {return false;}
					addr_cells = be32(toks[3]);
					// let's not deal with addresses larger than 128b
					if (addr_cells > 4) {return false;}
				} else if (0 == strncmp(str + offset, "#size-cells", string_size - offset)) {
					if (size_cells) {return false;}
					size_cells = be32(toks[3]);
					if (size_cells > 4) {return false;}
				}
			} else if (in_chosen) {
				char replaced_props[][24] = {
					"linux,initrd-start", "linux,initrd-end", "kaslr-seed", "rng-seed"
				};
				for_array(i, replaced_props) {
					if (0 == strncmp(str + offset, replaced_props[i], string_size - offset)) {
						skip_len = path_len;
					}
				}
			}
		}
		if (!skip_len) {
			if (out_end - out < size_words) {return false;}
			memcpy(out, toks, size_words * 4);
			out += size_words;
		}
		toks += size_words;
		if (path_len <= skip_len) {skip_len = 0;}
	} while (path_len);
	if (be32(*toks) != FDT_CMD_END) {return false;}
	if (out_end <= out) {return false;}
	*out++ = be32(FDT_CMD_END);
	debug("FDT transform done\n");

	u32 out_string_offset = (char*)out - (char*)out_header;
	out_header->string_offset = be32(out_string_offset);
	out_header->struct_size = be32(out_string_offset - out_struct_offset - 4);

	u32 out_string_size = sizeof(inserted_strings) + string_size;
	out_header->string_size = be32(out_string_size);
	if (out_string_size < string_size || out_string_size > (out_end - out) * 4) {return false;}
	memcpy(out, str, string_size);
	memcpy((char*)out + string_size, inserted_strings, sizeof(inserted_strings));

	u32 out_totalsize = out_string_offset + out_string_size;
	out_header->totalsize = be32(out_totalsize);
	*((u64*)out_header + 6) = be64(info->fdt_address);
	*((u64*)out_header + 7) = be64(out_totalsize);
	out_header->version = be32(17);
	out_header->last_compatible_version = be32(16);
	out_header->boot_cpu = be32(info->boot_cpu);
	out_header->magic = be32(0xd00dfeed);
	return true;
}
