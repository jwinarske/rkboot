/* SPDX-License-Identifier: CC0-1.0 */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

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

u8 inbuf[4 << 20];
u8 *ptr = inbuf, *buf_end = inbuf;
u8 outbuf[16 << 20];
char stderrbuf[1 << 16];

bool probe(const struct format *format, size_t *uncomp_size) {
	enum compr_probe_status res;
	while (1) {
		debug("probing %s: ", format->name);
		res = format->decomp->probe(ptr, buf_end, uncomp_size);
		if (res <= COMPR_PROBE_LAST_SUCCESS) {
			info("%s probed\n", format->name);
			return true;
		} else if (res == COMPR_PROBE_NOT_ENOUGH_DATA) {
			size_t size = buf_end - ptr;
			if (ptr != inbuf) {
				memmove(inbuf, ptr, buf_end - ptr);
				buf_end = inbuf + size;
				ptr = inbuf;
			} else if (size == sizeof(inbuf)) {
				fprintf(stderr, "Probing %s needed more than %zu (0x%zx) bytes of input buffer\n", format->name, size, size);
			}
			ssize_t read_res = read(0, buf_end, sizeof(inbuf) - size);
			debug("read %zd (%zx) bytes while probing\n", read_res, read_res);
			if (read_res > 0) {
				assert(read_res <= sizeof(inbuf) - size);
				buf_end += read_res;
				debugs("retrying\n");
				continue;
			}
			if (read_res < 0) {
				int e = errno;
				if (e != EINTR && e != EAGAIN && e != EWOULDBLOCK) {
					perror("While reading input");
					exit(1);
				}
				continue;	/* if the decoder isn't horribly broken, will try again */
			}
			/* EOF case: NOT_ENOUGH_DATA is final, fall through */
			info("EOF\n");
		} else {break;}
	}
	if (res != COMPR_PROBE_WRONG_MAGIC) {
		fprintf(stderr, "failed to probe %s: %s\n", format->name, compr_probe_status_msg[res]);
	} else {
		debug("wrong magic for %s\n", format->name);
	}
	return false;
}

void decompress(int fd, bool output_limit, u64 limit_pos) {
	const struct decompressor *decomp = 0;
	size_t size;
	for_array(i, formats) {
		if (probe(formats + i, &size)) {
			decomp = formats[i].decomp;
			goto probed;
		}
	}
	fprintf(stderr, "failed to probe any of: ");
	for_array(i, formats) {fprintf(stderr, " %s", formats[i].name);}
	fputs("\n", stderr);
	exit(1);
probed:;
	struct decompressor_state *state = malloc(decomp->state_size);
	if (!state) {
		fprintf(stderr, "failed to allocate %zu byte decompressor state\n", decomp->state_size);
		exit(1);
	}
	ptr = inbuf + (decomp->init(state, ptr, buf_end) - inbuf);
	if (!ptr) {
		fprintf(stderr, "failed to initialize the decompressor\n");
		exit(1);
	}
	u64 buffer_pos = 0;
	state->window_start = state->out = outbuf;
	state->out_end = outbuf + (output_limit && limit_pos < sizeof(outbuf) ? limit_pos : sizeof(outbuf));
	u8 *last_out = outbuf;
	while (state->decode) {
		size_t res = state->decode(state, ptr, buf_end);
		if (res == DECODE_NEED_MORE_DATA) {
			size_t size = buf_end - ptr;
			if (ptr != inbuf) {
				memmove(inbuf, ptr, buf_end - ptr);
				buf_end = inbuf + size;
				ptr = inbuf;
			} else if (size == sizeof(inbuf)) {
				fprintf(stderr, "Decoder needed more than %zu (0x%zx) bytes of input buffer\n", size, size);
			}
			ssize_t res = read(0, buf_end, sizeof(inbuf) - size);
			debug("read %zd (%zx) bytes while decompressing\n", res, res);
			if (res > 0) {
				assert(res <= sizeof(inbuf) - size);
				buf_end += res;
				continue;
			}
			if (res < 0) {
				int e = errno;
				if (e != EINTR && e != EAGAIN && e != EWOULDBLOCK) {
					perror("While reading input");
					exit(1);
				}
				continue;	/* if the decoder isn't horribly broken, will try again */
			}
			/* EOF case: NEED_MORE_DATA is final, fall through */
		} else if (res == DECODE_NEED_MORE_SPACE) {
			if (state->window_start == outbuf) {
				fprintf(stderr, "ran out of output buffer\n");
				exit(1);
			}
			assert(state->out > state->window_start);
			size_t window_size = state->out - state->window_start;
			size_t move_amount = state->window_start - outbuf;
			buffer_pos += move_amount;
			debug("moving %zu-byte window by %zu bytes\n", window_size, move_amount);
			if (output_limit && limit_pos - buffer_pos < sizeof(outbuf)) {
				info("enforcing output limit\n");
				state->out_end = outbuf + (limit_pos - buffer_pos);
			}
			memmove(outbuf, state->window_start, window_size);
			state->window_start = outbuf;
			last_out = state->out = outbuf + window_size;
			continue;
		}
		if (res < NUM_DECODE_STATUS) {
			fprintf(stderr, "failed to decompress: %s\n", decode_status_msg[res]);
			exit(1);
		}
		debug("consumed %zu bytes\n", res - NUM_DECODE_STATUS);
		ptr += res - NUM_DECODE_STATUS;
		assert(state->out <= outbuf + sizeof(outbuf));
		assert(last_out >= outbuf);
		while (state->out > last_out) {
			size_t out_bytes = state->out - last_out;
			assert(out_bytes <= sizeof(outbuf));
			debug("writing %zu bytes … ", out_bytes);
			fflush(stderr);
			ssize_t res = write(fd, last_out, out_bytes);
			if (res < 0) {
				perror("While writing decompressed data");
				exit(1);
			}
			debug("wrote %zd bytes\n", res);
			last_out += res;
		}
		fflush(stderr);
	}
}

int main(int argc, char **argv) {
	setvbuf(stderr, stderrbuf, _IOFBF, sizeof(stderrbuf));
	_Bool overwrite = 0, output_limit = 0;
	u64 limit_pos = 0;
	while (*++argv) {
		if (**argv == '-' && (*argv)[1] != 0) {
			char *ptr = *argv + 1;
			while (*ptr) {
				if (*ptr == '-') {
					ptr += 1;
					if (0 == strcmp(ptr, "overwrite")) {
						overwrite = 1;
					} else if (0 == strcmp(ptr, "output-limit")) {
						if (!*++argv) {
							fprintf(stderr, "--%s needs a parameter\n", ptr);
							return 1;
						}
						if (1 != sscanf(*argv, "%"SCNu64, &limit_pos)) {
							fprintf(stderr, "could not parse argument: --%s %s\n", ptr, *argv);
							return 1;
						}
						output_limit = 1;
					} else {
						fprintf(stderr, "unknown long-form option %s\n", ptr);
						return 1;
					}
					break;
				} else if (*ptr == 'f') {
					ptr += 1;
					overwrite = 1;
				} else {
					fprintf(stderr, "unknown short-form option %s\n", ptr);
					return 1;
				}
			}
		} else {
			char *filename = *argv;
			info("decompressing to \"%s\"\n", filename);
			int fd;
			if (*filename == '-' && filename[1] == 0) {
				fd = 1;
			} else {
				int flags = O_WRONLY | O_CREAT;
				if (!overwrite) {flags |= O_EXCL;}
				overwrite = 0;
				fd = open(filename, flags, 0744);
				if (fd < 0) {
					perror("While opening file");
					return 1;
				}
			}
			decompress(fd, output_limit, limit_pos);
			if (0 != close(fd)) {
				perror("while closing the file");
				return 1;
			}
			info("decompression finished successfully\n");
			fflush(stderr);
		}
	}
	return 0;
}
