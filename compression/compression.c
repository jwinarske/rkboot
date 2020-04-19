/* SPDX-License-Identifier: CC0-1.0 */
#include "../include/defs.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include "compression.h"

enum compr_probe_status probe_lz4(const u8 *in, const u8 *end, u64 *size);
const u8 *decompress_lz4(const u8 *in, const u8 *end, u8 **out, u8 *out_end);

const char *compr_probe_status_msg[NUM_COMPR_PROBE_STATUS] = {
#define X(name, msg) msg,
	DEFINE_COMPR_PROBE_STATUS
#undef X
};

const struct format {
	enum compr_probe_status (*probe)(const u8 *in, const u8 *end, u64 *size);
	const u8 *(*decompress)(const u8 *in, const u8 *end, u8 **out, u8 *out_end);
} formats[] = {
#ifdef HAVE_LZ4
	{.probe = probe_lz4, .decompress = decompress_lz4},
#endif
	{.probe = 0}
};

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
	const struct format *fmt = formats;
	u64 size;
	u8 *out, *outptr;
	while (1) {
		if (!fmt->probe) {
			fprintf(stderr, "no known compression formats detected\n");
			return 1;
		}
		enum compr_probe_status status = fmt->probe(buf, buf + buf_size, &size);
		switch (status) {
		case COMPR_PROBE_SIZE_UNKNOWN:
			size = (16 << 20) - LZCOMMON_BLOCK;
			FALLTHROUGH;
		case COMPR_PROBE_SIZE_KNOWN:
			fprintf(stderr, "output buffer size %"PRIu64"\n", size + LZCOMMON_BLOCK);
			out = malloc(size + LZCOMMON_BLOCK);
			assert(out);
			outptr = out;
			if (!fmt->decompress(buf, buf + buf_size, &outptr, out+size)) {
				fprintf(stderr, "decompression failed\n");
				return 1;
			}
			if (status == COMPR_PROBE_SIZE_KNOWN && outptr != out + size) {
				fprintf(stderr, "decompression did not produce the probed amout of output\n");
				return 1;
			}
			goto decompressed;
		case COMPR_PROBE_WRONG_MAGIC: break;
		default:
			fprintf(stderr, "probing failed, status: %s\n", compr_probe_status_msg[status]);
		}
		fmt += 1;
	} decompressed:;
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
