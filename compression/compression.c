/* SPDX-License-Identifier: CC0-1.0 */
#include "../include/defs.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include "compression.h"
#include "../include/log.h"

extern const struct decompressor lz4_decompressor, gzip_decompressor;

const char *const compr_probe_status_msg[NUM_COMPR_PROBE_STATUS] = {
#define X(name, msg) msg,
	DEFINE_COMPR_PROBE_STATUS
#undef X
};

const char *const decode_status_msg[NUM_DECODE_STATUS] = {
#define X(name, msg) msg,
	DEFINE_DECODE_STATUS
#undef X
};

const struct decompressor *const formats[] = {
#ifdef HAVE_LZ4
	&lz4_decompressor,
#endif
#ifdef HAVE_GZIP
	&gzip_decompressor,
#endif
	0
};

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

int main(int argc, char **argv) {
	size_t buf_size, ref_size;
	u8 *buf = read_file(0, &buf_size), *ref = 0;
	assert(buf);
	const struct decompressor *const*decomp = formats;
	if (argc > 1 && strcmp("--ignore-error", argv[1])) {
		int fd = open(argv[1], O_RDONLY);
		assert(fd > 0);
		ref = read_file(fd, &ref_size);
		assert(ref);
	}
	size_t size;
	u8 *out;
	struct decompressor_state *state = 0;
	while (1) {
		if (!*decomp) {
			fprintf(stderr, "no known compression formats detected\n");
			return 1;
		}
		enum compr_probe_status status = (*decomp)->probe(buf, buf + buf_size, &size);
		switch (status) {
		case COMPR_PROBE_SIZE_UNKNOWN:
			size = (16 << 20) - LZCOMMON_BLOCK;
			FALLTHROUGH;
		case COMPR_PROBE_SIZE_KNOWN:;
			debug("allocating %zu bytes of decompressor state\n", (*decomp)->state_size);
			state = malloc((*decomp)->state_size);
			assert(state);
			const u8 *ptr = (*decomp)->init(state, buf, buf + buf_size);
			if (!ptr) {
				info("decompression init failed\n");
			}
			info("output buffer size %"PRIu64"\n", size + LZCOMMON_BLOCK);
			out = malloc(size + LZCOMMON_BLOCK);
			assert(out);
			state->out = out;
			state->window_start = out;
			state->out_end = out + size;
			_Bool unlimit = 0;
			while (state->decode) {
				const u8 *end = buf + buf_size;
				if (!unlimit && end - ptr > 100) {
					end = ptr + 100;
				}
				unlimit = 0;
				size_t res = state->decode(state, ptr, end);
				if (res >= NUM_DECODE_STATUS) {
					debug("bumping by %zu (0x%zx), now at 0x%zx\n", res - NUM_DECODE_STATUS, res - NUM_DECODE_STATUS, ptr + (res - NUM_DECODE_STATUS) - buf);
					ptr += res - NUM_DECODE_STATUS;
				} else if (res == DECODE_NEED_MORE_DATA) {
					info("removing size limit\n");
					unlimit = 1;
				} else {
					info("decompression failed, status: %s\n", decode_status_msg[res]);
					if (ref) {
						goto decompressed;
					} else {
						return 1;
					}
				}
			}
			if (status == COMPR_PROBE_SIZE_KNOWN && state->out != out + size) {
				info("decompression did not produce the probed amout of output\n");
				return 1;
			}
			goto decompressed;
		case COMPR_PROBE_WRONG_MAGIC: break;
		default:
			info("probing failed, status: %s\n", compr_probe_status_msg[status]);
		}
		decomp += 1;
	} decompressed:;
	if (ref) {
		size_t min_size = state->out - out < ref_size ? state->out - out : ref_size;
		size_t pos = 0;
		while (pos < min_size && out[pos] == ref[pos]) {pos += 1;}
		u8 a = 0, b = 0;
		if (pos < min_size) {a = out[pos]; b = ref[pos];}
		if (pos == ref_size && ref_size == state->out - out) {
			info("output matches\n");
		} else {
			info("divergence at offset %zu\n", pos);
			memset(out, 0, size);
			u8 *ptr = buf;
			state->out = out;
			state->window_start = out;
			state->out_end = out + pos;
			while (1) {assert(state->decode);
				size_t res = state->decode(state, ptr, buf + buf_size);
				if (res >= NUM_DECODE_STATUS) {
					assert(state->decode || res > NUM_DECODE_STATUS);
					ptr += res - NUM_DECODE_STATUS;
				} else if (res == DECODE_NEED_MORE_SPACE) {break;} else {
					info("decompression failed, status: %s\n", decode_status_msg[res]);
					if (ref) {
						goto decompressed;
					} else {
						return 1;
					}
				}
			}
			info("divergence at offset %zu (0x%zx), got 0x%02"PRIx8", expected 0x%02"PRIx8"\n", pos, pos, a, b);
			return 1;
		}
	}
	while (out < state->out) {
		ssize_t res = write(1, out, state->out - out);
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
