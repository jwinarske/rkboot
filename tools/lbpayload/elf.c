// SPDX-License-Identifier: CC0-1.0
#include "lbpayload.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static inline uint16_t rle16(const uint8_t *ptr) {
	return ptr[0] | (uint16_t)ptr[1] << 8;
}
static inline uint32_t rle32(const uint8_t *ptr) {
	return rle16(ptr) | (uint32_t)rle16(ptr + 2) << 16;
}
static inline uint64_t rle64(const uint8_t *ptr) {
	return rle32(ptr) | (uint64_t)rle32(ptr + 4) << 32;
}

enum {
	ELFCLASS32 = 1,
	ELFCLASS64,
};
enum {
	ELF_LITTLE_ENDIAN = 1,
	ELF_BIG_ENDIAN,
};
#define DEFINE_ELF_PT X(NULL) X(LOAD) X(DYNAMIC) X(INTERP) X(NOTE)\
	X(SHLIB) X(PHDR) X(TLS)
enum {
#define X(name) ELF_PT_##name,
	DEFINE_ELF_PT
#undef X
	NUM_ELF_PT
};

enum {
	ELF_GNU_STACK = 0x6474e551,
};

const char elf_pt_names[NUM_ELF_PT][16] = {
#define X(name) #name,
	DEFINE_ELF_PT
#undef X
};

bool load_elf(struct context *ctx, const uint8_t *buf, size_t size, struct rel_addr *entry) {
	if (size < 64) {
		fprintf(stderr, "File is not large enough for ELF header\n");
		return false;
	}
	if (buf[0] != 0x7f || buf[1] != 'E' || buf[2] != 'L' || buf[3] != 'F') {
		fprintf(stderr, "File is not ELF\n");
		return false;
	}
	if (
		buf[4] != ELFCLASS64 || buf[5] != ELF_LITTLE_ENDIAN
		|| buf[6] != 1 || rle32(buf + 20) != 1
	) {
		fprintf(stderr, "ELF is not version 1 little-endian ELFCLASS64\n");
		return false;
	}
	entry->segment = SIZE_MAX;
	entry->offset = rle64(buf + 24);
	uint64_t phoff = rle64(buf + 32);
	size_t phentsize = rle16(buf + 54);
	if (phentsize < 56) {
		fprintf(stderr, "bogus e_phentsize = 0x%zx\n", phentsize);
		return false;
	}
	size_t phnum = rle16(buf + 56);
	if (phnum * phentsize > size || phoff > size - phnum * phentsize) {
		fprintf(stderr, "program headers do not fit: size = 0x%zx e_phoff = 0x%zx e_phentsize = 0x%zx\n", size, phoff, phentsize);
		return false;
	}
	if ((phoff | phentsize) & 7) {
		fprintf(stderr, "program headers not aligned: e_phoff = 0x%zx, e_phentsize = 0x%zx", phoff, phentsize);
	}
	for (size_t i_ph = 0; i_ph < phnum; ++i_ph) {
		const uint8_t *ph = buf + phoff + phentsize * i_ph;
		uint32_t type = rle32(ph);
		switch (type) {
		case ELF_PT_NULL:
		case ELF_PT_NOTE:
		case ELF_PT_PHDR:
			fprintf(stderr, "ignoring %s segment\n", elf_pt_names[type]);
			break;
		case ELF_GNU_STACK:
			fprintf(stderr, "ignoring GNU_STACK segment\n");
			break;
		case ELF_PT_LOAD:
			fprintf(stderr, "LOAD segment\n");
			uint64_t offset = rle64(ph + 8);
			uint64_t addr = rle64(ph + 16);
			uint64_t filesz = rle64(ph + 32);
			uint64_t memsz = rle64(ph + 40);
			if (memsz < filesz) {
				fprintf(stderr, "memory size (0x%"PRIx64") smaller than file size (0x%"PRIx64")\n", memsz, filesz);
				return false;
			}
			if (filesz > size || offset > size - filesz) {
				fprintf(stderr, "segment extends beyond EOF");
				return false;
			}
			uint64_t last = addr + memsz - 1;
			if (entry->segment == SIZE_MAX
			&& addr <= entry->offset
			&& entry->offset <= last) {
				entry->segment = ctx->segments_size;
				entry->offset -= addr;
			}
			*BUMP(ctx->segments) = (struct segment) {
				.first = addr,
				.last_init = last,
				.last = last,
				.buf = buf + offset,
				// should fit in size_t since we have it in memory
				.size = filesz,
				.role = SEG_ROLE_LOAD,
				.alignment = SEG_ADDR_FIXED,
			};
			break;
		default:
			if (type >= NUM_ELF_PT) {
				fprintf(stderr, "unknown segment type 0x%"PRIx32"\n", type);
			} else {
				fprintf(stderr, "unexpected %s segment\n", elf_pt_names[type]);
			}
			return false;
		}
	}
	return true;
}
