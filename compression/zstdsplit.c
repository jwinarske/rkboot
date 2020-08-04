#include "../include/defs.h"
#include "../include/log.h"
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>

static u8 *read_file(int fd, size_t *size) {
	size_t buf_size = 0, buf_cap = 128;
	u8 *buf = malloc(buf_cap);
	assert(buf);
	while (1) {
		if (buf_cap - buf_size < 128) {
			buf = realloc(buf, buf_cap *= 2);
			assert(buf);
		}
		ssize_t res = read(fd, buf + buf_size, buf_cap - buf_size);
		if (res > 0) {
			buf_size += res;
		} else if (!res) {
			break;
		} else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
			perror("While reading input file");
			return 0;
		}
	}
	info("read %zu bytes\n", buf_size);
	*size = buf_size;
	return buf;
}

static void write_buf(int fd, const u8 *buf, size_t size) {
	const u8 *end = buf + size;
	while (buf < end) {
		ssize_t res = write(fd, buf, end - buf);
		assert(res >= 0);
		buf += res;
	}
}

static void write_padding_blocks(int fd, u8 window_size_field) {
	u64 window_left = (u64)(8 | (window_size_field & 7)) << 7 << (window_size_field >> 3);
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

static void extract_full_block(const u8 *buf, const u8 *ptr, u32 block_header, u8 window_size_field) {
	char name[32];
	sprintf(name, "block%zx.zst", (size_t)(ptr - buf));
	printf("%s\n", name);
	int fd = open(name, O_WRONLY | O_TRUNC | O_CREAT, 0744);
	assert(fd > 0);
	u8 header[6] = {0x28, 0xb5, 0x2f, 0xfd, 0, window_size_field};
	write_buf(fd, header, sizeof(header));
	write_padding_blocks(fd, window_size_field);
	ptr += 3;
	u8 new_header[3] = {(u8)(block_header | 1) , (u8)(block_header >> 8), (u8)(block_header >> 16)};
	write_buf(fd, new_header, 3);
	u32 compr_size = block_header >> 3;
	write_buf(fd, ptr, compr_size);
	close(fd);
}

int main(int argc, char **argv) {
	size_t buf_size;
	u8 *buf = read_file(0, &buf_size);
	if (!buf) {return 1;}
	u8 *buf_end = buf + buf_size;
	assert(buf_size >= 9);
	static const u8 magic[4] = {0x28, 0xb5, 0x2f, 0xfd};
	assert(!memcmp(magic, buf, 4));
	u8 frame_header_desc = buf[4];
	assert(~frame_header_desc & 0x20);
	u8 fcs_size = 1 << (frame_header_desc >> 6);
	if (fcs_size == 1) {
		fcs_size = 0;
	}
	u8 window_size_field = buf[5];
	u8 *ptr = buf + 6 + fcs_size;
	while (1) {
		assert(ptr < buf_end - 3);
		u32 block_header = ptr[0] | (u32)ptr[1] << 8 | (u32)ptr[2] << 16;
		u32 block_size = block_header >> 3;
		switch (block_header >> 1 & 3) {
		case 0:
			fprintf(stderr, "%"PRIu32"-byte raw block, skipping\n", block_size);
			assert(buf_end - ptr >= block_size + 3);
			ptr += block_size + 3;
			continue;
		case 1:
			fprintf(stderr, "%"PRIu32"-byte RLE block, skipping\n", block_size);
			assert(buf_end - ptr >= 4);
			ptr += 4;
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
		u8 lsh0 = ptr[3];
		u32 lit_size, regen_size;
		_Bool literals_available = 1, literals_interesting = 0;
		switch (lsh0 >> 1 & 3) {
		case 0:	/* Raw/RLE literals, size format 0 */
			regen_size = lsh0 >> 3;
			lit_size = (lsh0 & 1 ? regen_size : 1) + 1;
			break;
		case 2:	/* Raw/RLE literals, size format 1/2 */
			assert(block_size >= 2);
			regen_size = lsh0 >> 4 | (u32)ptr[4] << 4;
			u8 lsh_size = 2;
			if (lsh0 & 8) {
				lsh_size = 3;
				assert(block_size >= 3);
				regen_size |= (u32)ptr[5] << 12;
			}
			lit_size = (lsh0 & 1 ? regen_size : 1) + lsh_size;
			break;
		case 1:
		case 3:
			assert(block_size >= 3);
			u8 size_format = lsh0 >> 2 & 3;
			u32 lsh1 = ptr[4], lsh2 = ptr[5];
			switch (size_format) {
			case 0:
			case 1:	/* these differ only in the number of streams, not relevant for splitting */
				regen_size = lsh0 >> 4 | (lsh1 << 4 & 0x3f0);
				lit_size = lsh1 >> 6 | lsh2 << 2;
				lit_size += 3;
				break;
			case 2:
				assert(block_size >= 4);
				regen_size = lsh0 >> 4 | lsh1 << 4 | (lsh2 << 12 & 0x3000);
				lit_size = lsh2 >> 2 | (u32)ptr[6] << 6;
				lit_size += 4;
				break;
			case 3:
				assert(block_size >= 4);
				regen_size = lsh0 >> 4 | lsh1 << 4 | (lsh2 << 12 & 0x3f000);
				lit_size = lsh2 >> 6 | (u32)ptr[6] << 2 | (u32)ptr[7] << 10;
				lit_size += 5;
				break;
			}
			literals_interesting = 1;
			if (lsh0 & 1) {
				fprintf(stderr, "block uses treeless literals, cannot extract out of context\n");
				literals_available = 0;
			}
		}
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
		_Bool no_repeat_mode = (scm & scm >> 1 & 0x55) == 0;
		if (no_repeat_mode && literals_available) {
			extract_full_block(buf, ptr, block_header, window_size_field);
		} else {
			if (literals_available && literals_interesting) {
				char name[32];
				sprintf(name, "lit%zx.zst", (size_t)(ptr - buf));
				printf("%s\n", name);
				int fd = open(name, O_WRONLY | O_TRUNC | O_CREAT, 0744);
				assert(fd > 0);
				u8 header[12] = {
					0x28, 0xb5, 0x2f, 0xfd,	/* magic number */
					0xa0,	/* Frame Header Desclriptor: single-segment, 4-byte frame content size */
					regen_size & 0xff, regen_size >> 8 & 0xff, regen_size >> 16, 0,
					(lit_size + 1) << 3 | 5, (lit_size + 1) >> 5 & 0xff, (lit_size + 1) >> 13
				};
				write_buf(fd, header, sizeof(header));
				write_buf(fd, ptr + 3, lit_size);
				u8 zero = 0;
				write_buf(fd, &zero, 1);	/* no sequences */
				close(fd);
			} else if (no_repeat_mode) {
				char name[32];
				sprintf(name, "seq%zx.zst", (size_t)(ptr - buf));
				printf("%s\n", name);
				int fd = open(name, O_WRONLY | O_TRUNC | O_CREAT, 0744);
				assert(fd > 0);
				u32 seq_size = block_size - lit_size;
				u32 out_size = seq_size + 3;
				u8 header[6] = {
					0x28, 0xb5, 0x2f, 0xfd,	/* magic number */
					0, window_size_field,
				};
				write_buf(fd, header, sizeof(header));
				write_padding_blocks(fd, window_size_field);
				u8 header2[6] = {
					out_size << 3 | 5, out_size >> 5 & 0xff, out_size >> 13,
					regen_size << 4 | 0xd, regen_size >> 4 & 0xff, regen_size >> 12
				};
				write_buf(fd, header2, sizeof(header2));
				write_buf(fd, ptr + 3 + lit_size, seq_size);
				close(fd);
			}
		}
		fflush(stdout);
		if (block_header & 1) {break;}
		ptr += block_size + 3;
	}
}
