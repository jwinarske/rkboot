/* SPDX-License-Identifier: CC0-1.0 */
#include <rk3399/dramstage.h>
#include <rk3399/payload.h>
#include <assert.h>
#include <stdatomic.h>
#include <inttypes.h>

#include <die.h>
#include <log.h>
#include <async.h>
#include <timer.h>
#include <compression.h>
#include <runqueue.h>
#include <dump_mem.h>

static _Alignas(16) u8 decomp_state[1 << 14];
extern const struct decompressor lz4_decompressor, gzip_decompressor, zstd_decompressor;

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

static void UNUSED async_wait(struct async_transfer *async) {
	struct async_buf buf = async->pump(async, 0, 0);
	size_t size;
	do {
		size = buf.end - buf.start;
		buf = async->pump(async, 0, size + 1);
	} while ((size_t)(buf.end - buf.start) > size);
}

static const char *const decode_status_msg[NUM_DECODE_STATUS] = {
#define X(name, msg) msg,
	DEFINE_DECODE_STATUS
#undef X
};

static _Bool decompress(struct async_transfer *async, u8 *out, u8 **out_end) {
#ifdef ASYNC_WAIT
	async_wait(async);
#endif
	struct decompressor_state *state = (struct decompressor_state *)decomp_state;;
	u64 start = get_timestamp();
	struct async_buf buf = async->pump(async, 0, 1);
	size_t size;
	for_array(i, formats) {
		enum compr_probe_status status;
		while ((status = formats[i].decomp->probe(buf.start, buf.end, &size)) == COMPR_PROBE_NOT_ENOUGH_DATA) {
			buf = async->pump(async, 0, buf.end - buf.start + 1);
		}
		if (status <= COMPR_PROBE_LAST_SUCCESS) {
			assert(sizeof(decomp_state) >= formats[i].decomp->state_size);
			info("%s probed\n", formats[i].name);
			{
				const u8 *data = formats[i].decomp->init(state, buf.start, buf.end);
				assert(data);
				buf = async->pump(async, data - buf.start, 0);
			}
			if (!buf.start) {return 0;}
			state->out = state->window_start = out;
			debug("output buffer: 0x%"PRIx64"–0x%"PRIx64"\n", (u64)out, (u64)*out_end);
			state->out_end = *out_end;
			while (state->decode) {
				if (!buf.start) {return 0;}
				sched_yield();
				size_t res = state->decode(state, buf.start, buf.end);
				if (res == DECODE_NEED_MORE_DATA) {
					size_t min_size = buf.end - buf.start + 1;
					buf = async->pump(async, 0, min_size);
					if ((size_t)(buf.end - buf.start) >= min_size) {continue;}
				} else if (res > NUM_DECODE_STATUS) {
					size_t consume = res - NUM_DECODE_STATUS;
					buf = async->pump(async, consume, buf.end - buf.start - consume);
					continue;
				}
				info("decompression failed, status: %zu (%s)\n", res, decode_status_msg[res]);
				return 0;
			}
			info("decompressed %zu bytes in %zu μs\n", state->out - out, (get_timestamp() - start) / TICKS_PER_MICROSECOND);
			*out_end = state->out;
			return 1;
		}
	}
#if DEBUG_MSG
	dump_mem(buf.start, (size_t)(buf.end - buf.start) < 1024 ? (size_t)(buf.end - buf.start) : 1024);
#endif
	infos("couldn't probe\n");
	return 0;
}

_Bool decompress_payload(struct async_transfer *async) {
	struct payload_desc *payload = get_payload_desc();
	payload->elf_end -= LZCOMMON_BLOCK;
	if (!decompress(async, payload->elf_start, &payload->elf_end)) {return 0;}
	payload->fdt_end -= LZCOMMON_BLOCK;
	if (!decompress(async, (u8 *)fdt_addr, &payload->fdt_end)) {return 0;}
	payload->kernel_end -= LZCOMMON_BLOCK;
	if (!decompress(async, (u8 *)payload_addr, &payload->kernel_end)) {return 0;}
#ifdef CONFIG_ELFLOADER_INITCPIO
	payload->initcpio_end -= LZCOMMON_BLOCK;
	if (!decompress(async, (u8 *)initcpio_addr, &payload->initcpio_end)) {return 0;}
#endif
	return 1;
}
