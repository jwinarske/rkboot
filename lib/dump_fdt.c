/* SPDX-License-Identifier: CC0-1.0 */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include <die.h>
#include <fdt.h>

bool dump_fdt(const struct fdt_header *fdt) {
	if (be32(fdt->magic) != 0xd00dfeed) {
		printf("no FDT found at %zx: magic=%x != d00dfeed\n", (u64)fdt, be32(fdt->magic));
		return false;
	}
	u32 totalsize = be32(fdt->totalsize);
	u32 struct_offset = be32(fdt->struct_offset);
	u32 string_offset = be32(fdt->string_offset);
	u32 reserved_offset = be32(fdt->reserved_offset);
	u32 version = be32(fdt->version);
	u32 compatible = be32(fdt->last_compatible_version);
	printf("found FDT:\n\tversion %u, compatible with %u\n", version, compatible);
	if (version < compatible) {
		printf("version should be no older than the compatibility version\n");
		return false;
	}
	if (version < 16 || compatible > 17) {
		printf("incompatible FDT version\n");
		return false;
	}
	printf("\ttotalsize: %u (0x%x)\n\tstruct offset: %u (0x%x), size", totalsize, totalsize, struct_offset, struct_offset);
	u32 boot_cpu = be32(fdt->boot_cpu);
	u32 string_size = be32(fdt->string_size);
	u32 struct_size = totalsize - struct_offset;
	if (version >= 17) {
		struct_size = be32(fdt->struct_size);
		printf(": %u (0x%x)\n", struct_size, struct_size);
	} else {
		puts(" unknown");
	}
	printf("\tstring offset: %u (0x%x), size: %u (0x%x)\n\tboot cpu: %u\n", string_offset, string_offset, string_size, string_size, boot_cpu);
	if (reserved_offset % 8) {
		printf("offset for reserved memory map (0x%x) is not 64-bit aligned\n", reserved_offset);
		return false;
	}
	if (reserved_offset >= totalsize - 16) {
		printf("reserved memory map does not fit into totalsize\n");
		return false;
	}
	const u64 *rsvmap = (u64*)((const char *)fdt + reserved_offset);
	if (rsvmap[1]) {
		for (u32 i = 0; rsvmap[i + 1]; i += 2) {
			printf("reserve %08zx size %08zx\n", be64(rsvmap[i]), be64(rsvmap[i + 1]));
			if (reserved_offset + i*16 >= totalsize - 16) {
				printf("reserved memory map does not fit into totalsize\n");
			}
		}
	} else {
		puts("no reserved memory regions.");
	}
	if (struct_offset + struct_size < struct_offset || struct_offset + struct_size > totalsize) {
		printf("struct segment does not fit into totalsize\n");
		return false;
	}
	if (string_offset + string_size < string_offset || string_offset + string_size > totalsize) {
		printf("string segment does not fit into totalsize (end offset = 0x%x)\n", string_offset + string_size);
		return false;
	}
	const char *string_seg = (const char *)fdt + string_offset;
	if (string_seg[string_size - 1] != 0) {
		printf("string segment is not zero-terminated");
		return false;
	}
	if (struct_offset % 4 != 0) {
		printf("struct segment not word-aligned\n");
		return false;
	}
	u32 depth = 0;
	const u32 *struct_seg = (const u32 *)((const char *)fdt + struct_offset), *struct_end = struct_seg + struct_size/4;
	if (be32(*struct_seg) != 1 && struct_seg[1] != 0) {
		printf("struct segment does not begin with an empty-name node\n");
		return false;
	}
	struct_seg += 2;
	const char *indent = "\t\t\t\t\t\t\t\t";
	puts("/ {");
	bool seen_node = false;
	while (1) {
		if (struct_seg >= struct_end) {
			printf("unexpected end of struct segment between commands\n");
			return false;
		}
		u32 cmd = be32(*struct_seg++);
		if (cmd == FDT_CMD_NOP) {continue;}
		if (cmd == FDT_CMD_NODE_END) {
			if (!depth) {break;}
			depth -= 1;
			seen_node = true;
		} else if (cmd != FDT_CMD_NODE && cmd != FDT_CMD_PROP) {
			printf("unexpected command word %08x", cmd);
			return false;
		}
		u32 d = depth;
		while (d > 8) {printf("%s", indent);d -= 8;}
		printf("%s", indent + (8 - d));
		if (cmd == FDT_CMD_NODE) {
			u32 words = 0;
			while (!fdt_has_zero_byte(be32(struct_seg[words++]))) {
				if (struct_end - struct_seg < words) {
					printf("unexpected end of struct segment in node name\n");
					return false;
				}
			}
			printf("%s {\n", (const char *)struct_seg);
			struct_seg += words;
			depth += 1;
			seen_node = false;
		} else if (cmd == FDT_CMD_PROP) {
			if (seen_node) {
				printf("found property after subnodes\n");
				return false;
			}
			if (struct_end - struct_seg < 2) {
				printf("unexpected end of struct segment in property header\n");
				return false;
			}
			u32 size = be32(*struct_seg++);
			u32 name_offset = be32(*struct_seg++);
			if (name_offset >= string_size) {
				printf("property name offset beyond string segment size");
				return false;
			}
			if (struct_end - struct_seg < (size + 3)/4) {
				printf("unexpected end of struct segment in property value\n");
				return false;
			}
			const char *name = string_seg + name_offset;
			const char *value_str = (const char *)struct_seg;
			if (size == 0) {
				printf("%s;\n", name);
			} else if (value_str[size - 1] == 0 && (size % 4 != 0 || !strcmp(name, "compatible"))) {
				printf("%s = \"", name);
				const u32 bufsize = 16;
				char buf[bufsize];
				u32 pos = 0;
				for_range(i, 0, size - 1) {
					assert(pos < bufsize - 1);
					char c = value_str[i];
					if (c >= 0x20 && c < 0x7f) {
						buf[pos++] = c;
						if (pos == bufsize - 1) {
							buf[bufsize - 1] = 0;
							printf("%s", buf);
							pos = 0;
						}
					} else {
						buf[pos++] = 0;
						printf("%s", buf);
						pos = 0;
						if (c == 0) {
							printf("\", \"");
						} else {
							printf("\\x%02x", (unsigned)c & 0xff);
						}
					}
				}
				buf[pos] = 0;
				printf("%s\";\n", buf);
			} else if (size % 4 == 0) {
				printf("%s = < ", name);
				for_range(i, 0, size/4) {
					u32 v = be32(struct_seg[i]);
					printf(v <= 32 ? "%u " : "0x%08x ", v);
				}
				puts(">;");
			} else {
				printf("%s = [ ", name);
				for_range(i, 0, size) {
					printf("0x%02x ", (unsigned)value_str[i] & 0xff);
				}
				puts("];");
			}
			struct_seg += (size + 3) / 4;
		} else {
			assert(cmd == 2);
			puts("};");
		}
	}
	puts("}");
	return true;
}
