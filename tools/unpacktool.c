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

/*static u8 *pump(int fd, u8 *buf, u8 *end) {
	while (buf < end) {
		ssize_t res = read(fd, buf, end - buf);
		if (res >= )
	}
}*/

u8 inbuf[4 << 20];
u8 outbuf[16 << 20];

int main(int argc, char **argv) {
	_Bool overwrite = 0;
	u8 *ptr = inbuf, *buf_end = ptr;
	while (*++argv) {
		if (**argv == '-' && (*argv)[1] != 0) {
			char *ptr = *argv + 1;
			while (*ptr) {
				if (*ptr == '-') {
					ptr += 1;
					if (0 == strcmp(ptr, "overwrite")) {
						overwrite = 1;
					} else {
						fprintf(stderr, "unknown long-form option %s\n", ptr);
						return 1;
					}
					break;
				} else if (*ptr == 'f') {
					overwrite = 1;
				} else {
					fprintf(stderr, "unknown short-form option %s\n", ptr);
				}
			}
		} else {
			info("decompressing to \"%s\"\n", *argv);
			int fd;
			if (**argv == '-' && (*argv)[1] == 0) {
				fd = 1;
			} else {
				int flags = O_WRONLY | O_CREAT;
				if (!overwrite) {flags |= O_EXCL;}
				overwrite = 0;
				fd = open(*argv, flags, 0744);
				if (fd < 0) {
					perror("While opening file");
					return 1;
				}
			}
			const struct decompressor *decomp = 0;
			size_t size;
			for_array(i, formats) {
				enum compr_probe_status res;
				while (1) {
					debug("probing %s: ", formats[i].name);
					res = formats[i].decomp->probe(ptr, buf_end, &size);
					if (res <= COMPR_PROBE_LAST_SUCCESS) {
						info("%s probed\n", formats[i].name);
						decomp = formats[i].decomp;
						goto probed;
					} else if (res == COMPR_PROBE_NOT_ENOUGH_DATA) {
						size_t size = buf_end - ptr;
						if (ptr != inbuf) {
							memmove(inbuf, ptr, buf_end - ptr);
							buf_end = inbuf + size;
							ptr = inbuf;
						} else if (size == sizeof(inbuf)) {
							fprintf(stderr, "Probing %s needed more than %zu (0x%zx) bytes of input buffer\n", formats[i].name, size, size);
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
								return 1;
							}
							continue;	/* if the decoder isn't horribly broken, will try again */
						}
						/* EOF case: NOT_ENOUGH_DATA is final, fall through */
						info("EOF\n");
					} else {break;}
				}
				if (res != COMPR_PROBE_WRONG_MAGIC) {
					fprintf(stderr, "failed to probe %s: %s\n", formats[i].name, compr_probe_status_msg[res]);
				} else {
					debug("wrong magic for %s\n", formats[i].name);
				}
			}
			fprintf(stderr, "failed to probe any of: ");
			for_array(i, formats) {fprintf(stderr, " %s", formats[i].name);}
			fputs("\n", stderr);
			return 1;
			probed:;
			struct decompressor_state *state = malloc(decomp->state_size);
			if (!state) {
				fprintf(stderr, "failed to allocate %zu byte decompressor state\n", decomp->state_size);
				return 1;
			}
			ptr = inbuf + (decomp->init(state, ptr, buf_end) - inbuf);
			if (!ptr) {
				fprintf(stderr, "failed to initialize the decompressor\n");
				return 1;
			}
			state->window_start = state->out = outbuf;
			state->out_end = outbuf + sizeof(outbuf);
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
							return 1;
						}
						continue;	/* if the decoder isn't horribly broken, will try again */
					}
					/* EOF case: NEED_MORE_DATA is final, fall through */
				}
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
			if (0 != close(fd)) {
				perror("while closing the file");
			}
			info("decompression finished successfully\n");
		}
	}
	return 0;
}
