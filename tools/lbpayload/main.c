// SPDX-License-Identifier: CC0-1.0
#include "lbpayload.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tools.h"

_Noreturn int allocation_failed() {
	fputs("allocation failed", stderr);
	abort();
}

#define DEFINE_CLI_CMD\
	CLI_FILE(BL31, bl31, 0)\
	CLI_FILE(DTB, dtb, 0)\
	CLI_ARG(BOOTARGS, bootargs, 0)\
	CLI_FILE(LINUX, linux, 0)\
	CLI_FILE(ELF, elf, 0)\
	CLI_FILE(INITRD, initrd, 0)\
	CLI_FLAG(HELP, help, 'h')\
	CLI_ARG(COMPRESSOR, compressor, 0)\
	CLI_ARG(COMPRESSOR_OPTION, compressor-option, 0)\

enum cli_cmdfl {
	CLI_CMDFL_NOARG = 0 << 6,
	CLI_CMDFL_STRING = 1 << 6,
	CLI_CMDFL_FILE = 2 << 6,
	CLI_CMDFL_TYPE_MASK = 3 << 6,
	CLI_CMDFL_MASK = 3 << 6
};

enum cli_cmd {
	CLI_CMD_NONE = 0,
#define CLI_ARG(caps, name, short) CLI_CMD_##caps,
#define CLI_FILE(caps, name, short) CLI_CMD_##caps,
#define CLI_FLAG(caps, name, short) CLI_CMD_##caps,
	DEFINE_CLI_CMD
#undef CLI_ARG
#undef CLI_FILE
#undef CLI_FLAG
	NUM_CLI_CMD
};

struct {
	uint8_t len_flags;
	char short_name;
	char name[30];
} cli_cmd_names[NUM_CLI_CMD] = {
	{0},
#define CLI_ARG(caps, name, short) {(uint8_t)(sizeof(#name) - 1) | CLI_CMDFL_STRING, short, #name},
#define CLI_FILE(caps, name, short) {(uint8_t)(sizeof(#name) - 1) | CLI_CMDFL_FILE, short, #name},
#define CLI_FLAG(caps, name, short) {(uint8_t)(sizeof(#name) - 1) | CLI_CMDFL_NOARG, short, #name},
	DEFINE_CLI_CMD
#undef CLI_ARG
#undef CLI_FILE
#undef CLI_FLAG
};

void print_help(char *argv0) {
	fprintf(stderr,
		"Usage: %s [OPTIONS] [PARTS]\n"
		"Create a payload file for use with the levinboot bootloader\n"
		"\nAdding payload parts:\n"
		"      --bl31 <bl31.elf>\n"
		"        add a BL31 image that will be run before the kernel\n"
		"      --dtb <fdt.dtb>\n"
		"        add a flattened device tree blob that is passed to the kernel\n"
		"      --bootargs <args>\n"
		"        set the kernel command line\n"
		"      --linux <Image>\n"
		"        add a kernel in the format of a Linux 'Image'\n"
		"      --elf <kernel.elf>\n"
		"        add a kernel in the format of an ELF file\n"
		"      --initrd <init.cpio>\n"
		"        add an initrd that will be passed to the kernel\n"
		"\nOptions:\n"
		"  -h, --help\n"
		"        display this help and exit\n"
		"      --compressor {none,lz4,gzip,zstd}\n"
		"        set the compressor used for subsequent payload parts.\n"
		"        Use 'none' to disable compression.\n"
		"      --compressor-option <option>\n"
		"        add an option to the compressor invocation.\n"
		"        The options are be reset whenever --compressor is used,\n"
		"        even if the same compressor is specified.\n",
		argv0
	);
}

uint8_t *read_file(int fd, size_t *size) {
	size_t cap = 128, pos = 0;
	uint8_t *buf = malloc(cap);
	if (!buf) {
		perror("While reading input file");
		abort();
	}
	while (1) {
		buf = realloc(buf, cap);
		if (!buf) {
			perror("While reading input file");
			abort();
		}
		ssize_t res = read(fd, buf + pos, pos - cap);
		if (!res) {
			*size = pos;
			return buf;
		} else if (res < 0) {
			int code = errno;
			if (code != EAGAIN && code != EWOULDBLOCK && code != EINTR) {
				*size = code;
				return 0;
			}
		} else {
			assert(cap - pos >= (size_t)size);
			pos += (size_t)size;
		}
	}
}

static uint64_t padded_size(const struct segment *s) {
	if (s->alignment > 63) {return ~(uint64_t)0;}
	uint64_t mask = ~(uint64_t)0 << s->alignment;
	return (~mask | s->last) - (mask & s->first);
}

static void sort(void *arr, size_t size, size_t len, int (*cmp)(void *, void *, void*), void *arg) {
	for (size_t frontier = 1; frontier < len; ++frontier) {
		for (size_t i = frontier; i; --i) {
			char *el = (char*)arr + i * size;
			if (cmp(el - size, el, arg) <= 0) {break;}
			for (char *p = el - size; p < el; ++p) {
				char t = *p;
				*p = p[size];
				p[size] = t;
			}
		}
	}
}

static int cmp_segment(void *a_, void *b_, void *segs_) {
	const struct segment *segs = (const struct segment *)segs_;
	const struct segment *a = segs + *(const size_t *)a_;
	const struct segment *b = segs + *(const size_t *)b_;

#define CMP(a, b) if ((a) < (b)) {return -1;} if ((b) < (a)) {return 1;}
	uint64_t asize = padded_size(a), bsize = padded_size(b);
	CMP(bsize, asize)
	CMP(b->alignment, a->alignment)
	CMP(a->first, b->first)
	CMP(b->size, a->size)
	return 0;
}

static void dump_segment(const struct segment *seg) {
	printf("%016"PRIx64" %016"PRIx64" %3"PRIu8"\n", seg->first, seg->last, seg->alignment);
}

struct mem_region {
	uint64_t first, last;
	unsigned allocate : 1;
};

static const struct mem_region rk3399_regions[] = {
	{.first = 0, .last = 0x000fffff, .allocate = 0},	/* TZMEM */
	{.first = 0x00100000, .last = 0x03ffffff, .allocate = 1},
	/* dramstage memory */
	{.first = 0x08000000, .last = 0xf7ffffff, .allocate = 1},
	{.first = 0xff3b0000, .last = 0xff3b2000, .allocate = 0},	/* PMUSRAM */
	{.first = 0xff8c0000, .last = 0xff8effff, .allocate = 0},	/* SRAM */
};

static void layout_segments(struct context *ctx) {
	size_t *order = calloc(ctx->segments_size, sizeof(size_t));
	if (!order) {perror("While sorting the segments"); abort();}
	for (size_t i = 0; i < ctx->segments_size; ++i) {order[i] = i;}
	sort(order, sizeof(size_t), ctx->segments_size, cmp_segment, ctx->segments);
	printf("Sorted segments:\n");
	const struct mem_region *regions = rk3399_regions, *regions_end = regions + sizeof(rk3399_regions)/sizeof(rk3399_regions[0]);
	const struct mem_region *region = regions;
	uint64_t last = 0;
	size_t i = 0;
	for (; i < ctx->segments_size; ++i) {
		const struct segment *seg = ctx->segments + order[i];
		printf("%2zu: ", order[i]);
		dump_segment(seg);
		while (region < regions_end && seg->last > region->last) {
			region += 1;
		}
		if (region >= regions_end || seg->first < region->first) {
			fprintf(stderr, "Segment 0x%"PRIx64"–0x%"PRIx64" touches invalid address ranges\n", seg->first, seg->last);
			exit(3);
		}
		if (i && seg->first <= last) {
			fprintf(stderr, "Segment 0x%"PRIx64"–0x%"PRIx64" overlaps other segments\n", seg->first, seg->last);
			exit(3);
		}
		last = seg->last;
	}
	
}

struct fixup {size_t pos, value;};

struct outbuf {
	DECL_VEC(uint8_t, bytes);
	DECL_VEC(struct fixup, fixups);
};

static size_t vuint_length(uint64_t val) {
	size_t length = 0;
	do {
		length += 1;
		val >>= 7;
	} while (val);
	return length;
}

static void write_vuint(struct outbuf *bst, uint64_t val) {
	size_t length = vuint_length(val);
	if (bst->bytes_cap - bst->bytes_size < length) {
		assert(bst->bytes_cap > SIZE_MAX >> 1);
		bst->bytes_cap <<= 1;
		bst->bytes = realloc(bst->bytes, bst->bytes_cap);
		assert(bst->bytes);
	}
	bst->bytes_size += length;
	uint8_t *ptr = bst->bytes + length;
	do {
		*--ptr = val;
		val >>= 8;
	} while (val);
	if (length % 8) {
		uint8_t mask = (1 << (length % 8 - 1)) - 1;
		*ptr |= mask << (8 - length % 8);
	}
	length /= 8;
	while (length--) {*--ptr = 0xff;}
}

static void output_payload(struct context *ctx) {
	struct outbuf header;
	INIT_VEC(header.bytes);
	uint64_t magic = 0;
	for (size_t i = 0; i < 7; ++i) {
		magic |= (uint64_t)"levinbt"[i] << (56 - i * 8);
	}
	write_vuint(&header, magic);
	write_vuint(&header, 0);	/* version */
	write_vuint(&header, 9);	/* disk alignment (shift) */
	struct fixup *wrapper_length = BUMP(header.fixups);
	wrapper_length->pos = header.bytes_size;
	/*size_t delta = 0;
	for (size_t i = 0; i < ctx->segments_size; ++i) {
		const struct segment *seg = ctx->segments + i;
		struct fixup *seg_length = BUMP(header.fixups);
	}*/
}

int main(int argc, char **argv) {
	struct context ctx;
	INIT_VEC(ctx.segments);
	char **cur_arg = argv;
	char *arg = *++cur_arg;
	size_t fdt_transform_reserve = 0x2000;
	uint8_t default_alignment = 6;
	while (arg) {
		enum cli_cmd cmd = CLI_CMD_NONE;
		char *cmd_arg = 0;
		uint8_t cmd_type = CLI_CMDFL_NOARG;
		if (arg[0] != '-') {
			cmd_arg = arg;
			arg = *++cur_arg;
		} else if (arg[1] != '-') {
			for (enum cli_cmd i = 1; i < NUM_CLI_CMD; ++i) {
				if (cli_cmd_names[i].short_name == arg[1]) {
					cmd = i;
					break;
				}
			}
			if (!cmd) {
				fprintf(stderr, "ERROR: unknown short-format option %s\n", arg);
				print_help(argv[0]);
				return 1;
			}
			if (arg[2] != 0) {
				arg[1] = '-';
				++arg;
			} else {
				arg = *++cur_arg;
			}
		} else {
			for (enum cli_cmd i = 1; i < NUM_CLI_CMD; ++i) {
				size_t len = cli_cmd_names[i].len_flags & ~CLI_CMDFL_MASK;
				if (strncmp(arg + 2, cli_cmd_names[i].name, len) == 0) {
					cmd = i;
					break;
				}
			}
			if (!cmd) {
				fprintf(stderr, "ERROR: unknown long-format option %s\n", arg);
				print_help(argv[0]);
				return 1;
			}
			uint8_t len_flags = cli_cmd_names[cmd].len_flags;
			size_t len = len_flags & ~CLI_CMDFL_MASK;
			char terminator = arg[len + 2];
			cmd_type = len_flags & CLI_CMDFL_TYPE_MASK;
			if (cmd_type != CLI_CMDFL_NOARG) {
				if (terminator == 0) {
					cmd_arg = *++cur_arg;
					if (!cmd_arg) {
						fprintf(stderr, "ERROR: Option %s has no argument\n", arg);
						print_help(argv[0]);
						return 1;
					}
				} else if (terminator == '=') {
					cmd_arg = arg + len + 3;
				}
			} else {
				if (terminator == '=') {
					fprintf(stderr, "ERROR: Flag %s takes no argument\n", arg);
					print_help(argv[0]);
					return 1;
				} else if (terminator != 0) {
					fprintf(stderr, "ERROR: unknown long-format option %s\n", arg);
					print_help(argv[0]);
					return 1;
				}
			}
			arg = *++cur_arg;
		}
		assert(cmd || cmd_arg);
		if (!cmd_arg) {
			fprintf(stderr, "flag %s\n", cli_cmd_names[cmd].name);
		} else if (!cmd_arg) {
			fprintf(stderr, "positional arg %s\n", cmd_arg);
		} else {
			fprintf(stderr, "option %s = %s\n", cli_cmd_names[cmd].name, cmd_arg);
		}
		uint8_t *buf = 0;
		size_t size = 0;
		if (cmd_type == CLI_CMDFL_FILE) {
			assert(cmd_arg);
			if (0 == strcmp("-", cmd_arg)) {
				fprintf(stderr, "reading from stdin\n");
				buf = read_file(0, &size);
				if (!buf) {
					perror("While reading input");
					exit(2);
				}
			} else {
				fprintf(stderr, "reading from %s\n", cmd_arg);
				int fd = open(cmd_arg, O_RDONLY);
				if (fd < 0) {
					perror("While opening file");
					exit(2);
				}
				struct stat statbuf;
				if (fstat(fd, &statbuf) < 0) {
					perror("While obtaining file size");
					exit(2);
				}
				if (statbuf.st_size > SIZE_MAX) {
					fprintf(stderr, "file too large for size_t, can't load");
					exit(3);
				}
				size = statbuf.st_size;
				void *map = mmap(0, size, PROT_READ, MAP_SHARED, fd, 0);
				if (MAP_FAILED == map) {
					perror("While memory-mapping file");
					exit(2);
				}
				buf = map;
			}
		}
		switch (cmd) {
		case CLI_CMD_NONE:
			fprintf(stderr, "ERROR: positional arguments are not supported\n");
			print_help(argv[0]);
			return 1;
		case CLI_CMD_HELP:
			print_help(argv[0]);
			return 0;
		case CLI_CMD_BL31:
			if (!load_elf(&ctx, buf, size, &ctx.bl31_entry)) {return 3;}
			ctx.have_bl31 = 1;
			break;
		case CLI_CMD_ELF:
			if (!load_elf(&ctx, buf, size, &ctx.kernel_entry)) {return 3;}
			ctx.have_kernel = 1;
			break;
		case CLI_CMD_INITRD:
			ctx.initrd = ctx.segments_size;
			*BUMP(ctx.segments) = (struct segment) {
				.first = 0,
				.last_init = size - 1,
				.last = size - 1,
				.buf = buf,
				.size = size,
				.role = SEG_ROLE_LOAD,
				.alignment = default_alignment,
			};
			ctx.have_initrd = 1;
			break;
		case CLI_CMD_DTB:
			if (size >= SIZE_MAX / 2 - fdt_transform_reserve - 8) {
				fprintf(stderr, "FDT is too large\n");
				return 1;
			}
			size_t aligned_size = (size + 7) >> 3 << 3;
			ctx.fdt = ctx.segments_size;
			*BUMP(ctx.segments) = (struct segment) {
				.first = 0,
				.last_init = size - 1,
				.last = aligned_size + size + fdt_transform_reserve - 1,
				.buf = buf,
				.size = size,
				.role = SEG_ROLE_LOAD,
				.alignment = default_alignment > 3 ? default_alignment : 3,
			};
			ctx.have_initrd = 1;
			break;
		default: fprintf(stderr, "unexpected command %u\n", (unsigned)cmd); abort();
		}
	}
	for (size_t i = 0; i < ctx.segments_size; ++i) {
		dump_segment(ctx.segments + i);
	}
	layout_segments(&ctx);
	return 0;
}
