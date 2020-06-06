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
	u64 window_size = (u64)(8 | (window_size_field & 7)) << 7 << (window_size_field >> 3);
	u8 *ptr = buf + 6 + fcs_size;
	char name[20];
	while (1) {
		assert(ptr < buf_end - 3);
		u32 block_header = ptr[0] | (u32)ptr[1] << 8 | (u32)ptr[2] << 16;
		u32 compr_size = block_header >> 3;
		fprintf(stderr, "%"PRIu32"-byte block (header 0x%"PRIx32")\n", compr_size, block_header);
		assert(buf_end - ptr >= compr_size + 3);
		sprintf(name, "%zx.zst", (size_t)(ptr - buf));
		int fd = open(name, O_WRONLY | O_TRUNC | O_CREAT, 0744);
		assert(fd > 0);
		u8 header[6] = {0x28, 0xb5, 0x2f, 0xfd, 0, window_size_field};
		write_buf(fd, header, sizeof(header));
		u64 window_left = window_size;
		while (window_left) {
			u64 padding = 128 << 10;
			if (window_left < 128 << 10) {
				padding = window_left;
			}
			u8 block[4] = {(u8)(padding << 3 | 2), (u8)(padding >> 5), (u8)(padding >> 13), '.'};
			write_buf(fd, block, 4);
			window_left -= padding;
		}
		ptr += 3;
		u8 new_header[3] = {(u8)(block_header | 1) , (u8)(block_header >> 8), (u8)(block_header >> 16)};
		write_buf(fd, new_header, 3);
		write_buf(fd, ptr, compr_size);
		if (block_header & 1) {break;}
		ptr += compr_size;
	}
}
