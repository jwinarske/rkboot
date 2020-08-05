#include <unistd.h>
#undef NDEBUG
#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>

#include "../include/defs.h"
#include "../include/log.h"
#include "zstd_internal.h"

static void write_buf(int fd, const u8 *buf, size_t size) {
	const u8 *end = buf + size;
	while (buf < end) {
		ssize_t res = write(fd, buf, end - buf);
		assert(res >= 0);
		buf += res;
	}
}

static void write_padding_blocks(int fd, u64 window_left) {
	while (window_left) {
		u64 padding = 128 << 10;
		if (window_left < 128 << 10) {
			padding = window_left;
		}
		u8 block[4] = {(u8)(padding << 3 | 2), (u8)(padding >> 5), (u8)(padding >> 13), '.'};
		write_buf(fd, block, 4);
		window_left -= padding;
	}
}

u8 inbuf[4 << 20];

int main(int UNUSED argc, char UNUSED **argv) {
	u8 *buf_end = inbuf;
	while (buf_end < inbuf + sizeof(inbuf)) {
		ssize_t res = read(0, buf_end, inbuf + sizeof(inbuf) - buf_end);
		info("initial read %zd (0x%zx) bytes\n", res, res);
		if (res == 0) {break;}
		if (res < 0) {
			int e = errno;
			if (e != EINTR && e != EWOULDBLOCK && e != EAGAIN) {
				perror("during initial read");
				return 1;
			}
			continue;
		}
		buf_end += res;
	}
	assert(buf_end - inbuf >= 9);
	static const u8 magic[4] = {0x28, 0xb5, 0x2f, 0xfd};
	assert(!memcmp(magic, inbuf, 4));
	u8 frame_header_desc = inbuf[4];
	assert(~frame_header_desc & 0x20);
	u8 fcs_size = 1 << (frame_header_desc >> 6);
	if (fcs_size == 1) {
		fcs_size = 0;
	}
	u8 window_size_field = inbuf[5];
	u64 pos = 6 + fcs_size;
	u8 *ptr = inbuf + pos;
	int fd = 0;
	u64 padding = 0;
	while (1) {
		assert(ptr < buf_end - 3);
		u32 block_header = ptr[0] | (u32)ptr[1] << 8 | (u32)ptr[2] << 16;
		u32 block_size = block_header >> 3;
		u32 input_required = block_size + 3;
		if ((block_header >> 1 & 3) == 1) {input_required = 4;}
		if ((block_header & 1) == 0) {input_required += 3;}
		assert(input_required <= sizeof(inbuf));
		size_t available_input = buf_end - ptr;
		if (available_input < input_required) {
			if (ptr != inbuf) {
				memmove(inbuf, ptr, available_input);
				ptr = inbuf;
				buf_end = inbuf + available_input;
			}
			ssize_t res = read(0, buf_end, sizeof(inbuf) - available_input);
			info("read %zd (0x%zx) bytes\n", res, res);
			if (res > 0) {
				buf_end += res;
				continue;
			}
			if (res < 0) {
				int e = errno;
				if (e != EINTR && e != EWOULDBLOCK && e != EAGAIN) {
					perror("while reading");
					return 1;
				}
				continue;
			}
			fprintf(stderr, "unexpected EOF\n");
			return 1;
		}
		assert(available_input >= input_required);
		switch (block_header >> 1 & 3) {
		case 0:
			fprintf(stderr, "%"PRIu32"-byte raw block\n", block_size);
			assert(block_size < sizeof(inbuf));
			assert(buf_end - ptr >= block_size + 3);
			ptr += block_size + 3;
			pos += block_size + 3;
			padding += block_size;
			continue;
		case 1:
			fprintf(stderr, "%"PRIu32"-byte RLE block\n", block_size);
			assert(buf_end - ptr >= 4);
			ptr += 4;
			pos += 4;
			padding += block_size;
			continue;
		case 2:
			break;
		case 3:
			fprintf(stderr, "fatal: reserved block type encountered\n");
			exit(1);
		}
		fprintf(stderr, "%"PRIu32"-byte compressed block\n", block_size);
		assert(block_size >= 1);
		assert(buf_end - ptr >= block_size + 3);
		struct literals_probe probe = zstd_probe_literals(ptr + 3, ptr + 3 + block_size);
		if (probe.flags & ZSTD_TRUNCATED) {
			fprintf(stderr, "literals section was truncated\n");
			return 1;
		}
		u32 lit_size = probe.end - ptr - 3;
		fprintf(stderr, "%"PRIu32" (0x%"PRIx32") bytes literals section\n", lit_size, lit_size);
		assert(block_size >= lit_size + 1);
		u8 ssh0 = ptr[3 + lit_size];
		u32 num_seq_size;
		if (ssh0 < 128) {
			num_seq_size = 1;
		} else if (ssh0 < 255) {
			num_seq_size = 2;
		} else {
			num_seq_size = 3;
		}
		assert(block_size >= lit_size + num_seq_size);
		u8 scm = ptr[3 + lit_size + num_seq_size];
		assert((scm & 3) == 0);
		_Bool repeat_mode = (scm & scm >> 1 & 0x55) != 0;
		if ((~probe.flags & ZSTD_TREELESS) && !repeat_mode) {
			if (fd) {
				u8 last_block[3] = {1, 0, 0};
				write_buf(fd, last_block, sizeof(last_block));
				if (close(fd)) {
					perror("While closing file");
					return 1;
				}
			}
			char name[32];
			sprintf(name, "block%"PRIx64".zst", pos);
			printf("%s\n", name);
			fd = open(name, O_WRONLY | O_TRUNC | O_CREAT, 0744);
			assert(fd > 0);
			u8 header[6] = {0x28, 0xb5, 0x2f, 0xfd, 0, window_size_field};
			write_buf(fd, header, sizeof(header));
			write_padding_blocks(fd, (u64)(8 | (window_size_field & 7)) << 7 << (window_size_field >> 3));
			padding = 0;
		} else if (!fd) {
			fprintf(stderr, "repeat mode or treeless literals in the first compressed block");
			return 1;
		} else {
			fprintf(stderr, "appending\n");
			if (fd) {
				write_padding_blocks(fd, padding);
				padding = 0;
			}
		}
		write_buf(fd, ptr, block_size + 3);
		fflush(stdout);
		if (block_header & 1) {break;}
		ptr += block_size + 3;
		pos += block_size + 3;
	}
	if (fd && close(fd)) {
		perror("While closing last file");
		return 1;
	}
	return 0;
}
