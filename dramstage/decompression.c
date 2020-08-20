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

static size_t wait_for_data(struct async_transfer *async, size_t old_pos) {
	size_t pos;
	if (old_pos >= async->total_bytes) {
		die("waited for more data than planned\n");
	}
	while ((pos = atomic_load_explicit(&async->pos, memory_order_acquire)) <= old_pos) {
		debug("waiting for data …\n");
		sched_yield();
	}
	return pos;
}

static void UNUSED async_wait(struct async_transfer *async) {
	while (async->pos < async->total_bytes) {
#if CONFIG_ELFLOADER_SPI
		spew("idle pos=0x%zx rxlvl=%"PRIu32", rxthreshold=%"PRIu32"\n", spi1_state.pos, spi->rx_fifo_level, spi->rx_fifo_threshold);
#elif CONFIG_ELFLOADER_SD
#ifdef SPEW_MSG
		spew("idle ");dwmmc_print_status(sdmmc);
#endif
#endif
		sched_yield();
	}
}

static const char *const decode_status_msg[NUM_DECODE_STATUS] = {
#define X(name, msg) msg,
	DEFINE_DECODE_STATUS
#undef X
};

static size_t decompress(struct async_transfer *async, size_t offset, u8 *out, u8 **out_end) {
#ifdef ASYNC_WAIT
	async_wait(async);
#endif
	struct decompressor_state *state = (struct decompressor_state *)decomp_state;
	size_t size, xfer_pos = atomic_load_explicit(&async->pos, memory_order_acquire);
	u64 start = get_timestamp();
	const u8 *buf = async->buf;
	for_array(i, formats) {
		enum compr_probe_status status;
		while ((status = formats[i].decomp->probe(buf + offset, buf + xfer_pos, &size)) == COMPR_PROBE_NOT_ENOUGH_DATA) {
			xfer_pos = wait_for_data(async, xfer_pos);
		}
		if (status <= COMPR_PROBE_LAST_SUCCESS) {
			assert(sizeof(decomp_state) >= formats[i].decomp->state_size);
			info("%s probed\n", formats[i].name);
			{
				const u8 *data = formats[i].decomp->init(state, buf + offset, buf + xfer_pos);
				assert(data);
				offset = data - buf;
			}
			state->out = state->window_start = out;
			debug("output buffer: 0x%"PRIx64"–0x%"PRIx64"\n", (u64)out, (u64)*out_end);
			state->out_end = *out_end;
			while (state->decode) {
				size_t res = state->decode(state, buf + offset, buf + xfer_pos);
				if (res == DECODE_NEED_MORE_DATA) {
					xfer_pos = wait_for_data(async, xfer_pos);
				} else {
					if (res < NUM_DECODE_STATUS) {
						info("decompression failed, status: %zu (%s)\n", res, decode_status_msg[res]);
						return 0;
					}
					offset += res - NUM_DECODE_STATUS;
				}
				sched_yield();
			}
			info("decompressed %zu bytes in %zu μs\n", state->out - out, (get_timestamp() - start) / TICKS_PER_MICROSECOND);
			*out_end = state->out;
			return offset;
		}
	}
#if DEBUG_MSG
	dump_mem(buf + offset, xfer_pos - offset < 1024 ? xfer_pos - offset : 1024);
#endif
	infos("couldn't probe\n");
	return 0;
}

_Bool decompress_payload(struct async_transfer *async) {
	struct payload_desc *payload = get_payload_desc();
	payload->elf_end -= LZCOMMON_BLOCK;
	size_t offset = decompress(async, 0, payload->elf_start, &payload->elf_end);
	if (!offset) {return 0;}
	payload->fdt_end -= LZCOMMON_BLOCK;
	offset = decompress(async, offset, (u8 *)fdt_addr, &payload->fdt_end);
	if (!offset) {return 0;}
	payload->kernel_end -= LZCOMMON_BLOCK;
	offset = decompress(async, offset, (u8 *)payload_addr, &payload->kernel_end);
	if (!offset) {return 0;}
#ifdef CONFIG_ELFLOADER_INITCPIO
	payload->initcpio_end -= LZCOMMON_BLOCK;
	offset = decompress(async, offset, (u8 *)initcpio_addr, &payload->initcpio_end);
	if (!offset) {return 0;}
#endif
	return 1;
}
