/* SPDX-License-Identifier: CC0-1.0 */
#include <main.h>
#include "fdt.h"

__asm__(".section .entry, \"ax\", %progbits;adr x5, #0x10000;add sp, x5, #0;b main");

_Noreturn void main(u64 x0) {
	puts("test stage\n");
	u64 sctlr;
	__asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));
	debug("SCTLR_EL2: %016zx\n", sctlr);
	__asm__ volatile("msr sctlr_el2, %0" : : "r"(sctlr | SCTLR_I));
	const struct fdt_header *fdt = (const struct fdt_header*)x0;
	if (read_be32(&fdt->magic) == 0xd00dfeed) {
		u32 totalsize = read_be32(&fdt->totalsize);
		u32 struct_offset = read_be32(&fdt->struct_offset);
		u32 string_offset = read_be32(&fdt->string_offset);
		u32 reserved_offset = read_be32(&fdt->reserved_offset);
		u32 version = read_be32(&fdt->version);
		u32 compatible = read_be32(&fdt->last_compatible_version);
		printf("found FDT:\n\tversion %u, compatible with %u\n", version, compatible);
		assert_msg(version >= compatible, "version should be no older than the compatibility version\n");
		assert_msg(version >= 16 && compatible <= 17, "incompatible FDT version\n");
		printf("\ttotalsize: %u (0x%x)\n\tstruct offset: %u (0x%x), size", totalsize, totalsize, struct_offset, struct_offset);
		u32 boot_cpu = read_be32(&fdt->boot_cpu);
		u32 string_size = read_be32(&fdt->string_size);
		u32 struct_size = totalsize - struct_offset;
		if (version >= 17) {
			struct_size = read_be32(&fdt->struct_size);
			printf(": %u (0x%x)\n", struct_size, struct_size);
		} else {
			puts(" unknown\n");
		}
		printf("\tstring offset: %u (0x%x), size: %u (0x%x)\n\tboot cpu: %u\n", string_offset, string_offset, string_size, string_size, boot_cpu);
		assert_msg(reserved_offset % 8 == 0, "offset for reserved memory map (0x%x) is not 64-bit aligned\n", reserved_offset);
		assert_msg(reserved_offset < totalsize - 16, "reserved memory map does not fit into totalsize\n");
		const be64 *rsvmap = (be64*)(x0 + reserved_offset);
		if (rsvmap[1].v) {
			for (u32 i = 0; rsvmap[i + 1].v; i += 2) {
				printf("reserve %08zx size %08zx\n", read_be64(rsvmap + i), read_be64(rsvmap + i + 1));
				assert_msg(reserved_offset + i*16 < totalsize - 16, "reserved memory map does not fit into totalsize\n");
			}
		} else {
			puts("no reserved memory regions.\n");
		}
		assert_msg(struct_offset + struct_size >= struct_offset && struct_offset + struct_size <= totalsize, "struct segment does not fit into totalsize\n");
		assert_msg(string_offset + string_size >= string_offset && string_offset + string_size <= totalsize, "string segment does not fit into totalsize (end offset = 0x%x)\n", string_offset + string_size);
		const char *string_seg = (const char *)(x0 + string_offset);
		assert_msg(string_seg[string_size - 1] == 0, "string segment is not zero-terminated");
		u32 depth = 0;
		assert_msg(struct_offset % 4 == 0, "struct segment not word-aligned\n");
		const be32 *struct_seg = (const be32 *)(x0 + struct_offset), *struct_end = struct_seg + struct_size/4;
		assert_msg(read_be32(struct_seg) == 1 && read_be32(struct_seg + 1) == 0, "struct segment does not begin with an empty-name node\n");
		struct_seg += 2;
		const char *indent = "\t\t\t\t\t\t\t\t";
		puts("/ {\n");
		_Bool seen_node = 0;
		while (1) {
			assert_msg(struct_seg < struct_end, "unexpected end of struct segment between commands\n");
			u32 cmd = read_be32(struct_seg++);
			if (cmd == 2) {
				if (!depth) {break;}
				depth -= 1;
			} else {
				assert_msg(cmd == 1 || cmd == 3, "unexpected command word %08x", cmd);
			}
			u32 d = depth;
			while (d > 8) {puts(indent);d -= 8;}
			puts(indent + (8 - d));
			if (cmd == 1) {
				u32 words = 0;
				while (!has_zero_byte(read_be32(struct_seg + words++))) {
					assert_msg(struct_seg + words <= struct_end, "unexpected end of struct segment in node name\n");
				}
				printf("%s {\n", (const char *)struct_seg);
				struct_seg += words;
				depth += 1;
				seen_node = 0;
			} else if (cmd == 3) {
				assert_msg(!seen_node, "found property after subnodes\n");
				assert_msg(struct_seg + 2 <= struct_end, "unexpected end of struct segment in property header\n");
				u32 size = read_be32(struct_seg++);
				u32 name_offset = read_be32(struct_seg++);
				assert_msg(name_offset < string_size, "property name offset beyond string segment size");
				puts(string_seg + name_offset);
				assert_msg(struct_seg + (size + 3)/4 < struct_end, "unexpected end of struct segment in property value\n");
				const char *value_str = (const char *)struct_seg;
				if (size == 0) {
					puts(";\n");
				} else if (value_str[size -1] == 0 && (size % 4 != 0 || !strcmp(string_seg + name_offset, "compatible"))) {
					puts(" = \"");
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
								puts(buf);
								pos = 0;
							}
						} else {
							buf[pos++] = 0;
							puts(buf);
							pos = 0;
							if (c == 0) {
								puts("\", \"");
							} else {
								printf("\\x%02x", (unsigned)c & 0xff);
							}
						}
					}
					buf[pos] = 0;
					puts(buf);
					puts("\";\n");
				} else if (size % 4 == 0) {
					puts(" = < ");
					for_range(i, 0, size/4) {
						u32 v = read_be32(struct_seg + i);
						printf(v <= 32 ? "%u " : "0x%08x ", v);
					}
					puts(">;\n");
				} else {
					puts(" = [ ");
					for_range(i, 0, size) {
						printf("0x%02x ", (unsigned)value_str[i] & 0xff);
					}
					puts("];\n");
				}
				struct_seg += (size + 3) / 4;
			} else {
				assert(cmd == 2);
				puts("};\n");
			}
		}
		puts("}\n");
	} else {
		printf("no FDT found at %zx: magic=%x != d00dfeed\n", (u64)fdt, read_be32(&fdt->magic));
	}
	halt_and_catch_fire();
}
