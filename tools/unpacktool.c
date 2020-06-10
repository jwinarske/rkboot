#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../include/log.h"
#include "../compression/compression.h"

extern const struct decompressor lz4_decompressor, gzip_decompressor, zstd_decompressor;

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

const struct format {
	char name[8];
	const struct decompressor *decomp;
} formats[] = {
#ifdef HAVE_LZ4
	{"LZ4", &lz4_decompressor},
#endif
#ifdef HAVE_GZIP
	{"gzip", &gzip_decompressor},
#endif
#ifdef HAVE_ZSTD
	{"zstd", &zstd_decompressor},
#endif
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

u8 outbuf[16 << 20];

int main(int argc, char **argv) {
	fprintf(stderr, "reading input …");
	size_t buf_size;
	u8 *buf = read_file(0, &buf_size);
	if (!buf) {
		fprintf(stderr, "reading input failed\n");
		return 1;
	}
	fprintf(stderr, "read %zu bytes\n", buf_size);
	const u8 *buf_end = buf + buf_size, *ptr = buf;
	while (*++argv) {
		if (**argv == '-') {
			info("got option argument, but no options are implemented yet");
			return 1;
		} else {
			info("decompressing to \"%s\"\n", *argv);
			int fd = open(*argv, O_WRONLY | O_CREAT | O_EXCL, 0744);
			if (fd < 0) {
				perror("While opening file");
				return 1;
			}
			const struct decompressor *decomp = 0;
			size_t size;
			for_array(i, formats) {
				enum compr_probe_status res = formats[i].decomp->probe(ptr, buf_end, &size);
				if (res <= COMPR_PROBE_LAST_SUCCESS) {
					info("%s probed\n", formats[i].name);
					decomp = formats[i].decomp;
					break;
				} else if (res != COMPR_PROBE_WRONG_MAGIC) {
					fprintf(stderr, "failed to probe %s: %s\n", formats[i].name, compr_probe_status_msg[res]);
				} else {
					debug("wrong magic for %s\n", formats[i].name);
				}
			}
			if (!decomp) {
				fprintf(stderr, "failed to probe any of: ");
				for_array(i, formats) {fprintf(stderr, " %s", formats[i].name);}
				fputs("\n", stderr);
				return 1;
			}
			struct decompressor_state *state = malloc(decomp->state_size);
			if (!state) {
				fprintf(stderr, "failed to allocate %zu byte decompressor state\n", decomp->state_size);
				return 1;
			}
			ptr = decomp->init(state, ptr, buf_end);
			if (!ptr) {
				fprintf(stderr, "failed to initialize the decompressor\n");
				return 1;
			}
			state->window_start = state->out = outbuf;
			state->out_end = outbuf + sizeof(outbuf);
			u8 *last_out = outbuf;
			while (state->decode) {
				size_t res = state->decode(state, ptr, buf_end);
				if (res < NUM_DECODE_STATUS) {
					fprintf(stderr, "failed to decompress: %s", decode_status_msg[res]);
					return 1;
				}
				debug("consumed %zu bytes\n", res - NUM_DECODE_STATUS);
				ptr += res - NUM_DECODE_STATUS;
				assert(state->out <= outbuf + sizeof(outbuf));
				assert(last_out >= outbuf);
				while (state->out > last_out) {
					size_t out_bytes = state->out - last_out;
					assert(out_bytes <= sizeof(outbuf));
					debug("writing %zu bytes … ", out_bytes);
					ssize_t res = write(fd, last_out, out_bytes);
					if (res < 0) {
						perror("While writing decompressed data");
						return 1;
					}
					debug("wrote %zd bytes\n", res);
					last_out += res;
				}
				if (state->window_start > outbuf) {
					assert(state->out > state->window_start);
					size_t window_size = state->out - state->window_start;
					debug("moving %zu-byte window by %zu bytes\n", window_size, state->window_start - outbuf);
					memmove(outbuf, state->window_start, window_size);
					state->window_start = outbuf;
					last_out = state->out = outbuf + window_size;
				}
			}
			info("decompression finished successfully\n");
		}
	}
	return 0;
}
