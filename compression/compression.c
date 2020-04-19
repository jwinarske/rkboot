/* SPDX-License-Identifier: CC0-1.0 */
#include "../include/defs.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>

_Bool decompress(const u8 *in, const u8 *end, u8 **out, u8 *out_end);

int main(int argc, char **argv) {
	size_t buf_size = 0, buf_cap = 128;
	u8 *buf = malloc(buf_cap);
	assert(buf);
	int fd = 0;
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
			return 1;
		}
	}
	fprintf(stderr,"read %zu bytes\n", buf_size);
	u32 out_length = 16 << 20;
	u8 *out = malloc(out_length), *outptr = out;
	assert(out);
	if (!decompress(buf, buf + buf_size, &outptr, out+out_length)) {
		fprintf(stderr, "decompression failed\n");
		return 1;
	}
	while (out < outptr) {
		ssize_t res = write(1, out, outptr - out);
		if (res <= 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
			perror("While writing output");
			return 1;
		}
		if (res > 0) {
			out += res;
		}
	}
	return 0;
}
