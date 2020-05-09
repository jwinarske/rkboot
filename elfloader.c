/* SPDX-License-Identifier: CC0-1.0 */
#include <inttypes.h>
#include <main.h>
#include <uart.h>
#include <rk3399.h>
#include <stage.h>
#include <compression.h>
#include <rkspi.h>
#include <gic.h>
#include "fdt.h"
#include ATF_HEADER_PATH

static image_info_t bl33_image = {
	.image_base = 0x00600000,
	.image_size = 0x01000000,
	.image_max_size = 0x01000000,
};

static entry_point_info_t bl33_ep = {
	.h = {
		.type = PARAM_EP,
		.version = PARAM_VERSION_1,
		.size = sizeof(bl33_ep),
		.attr = EP_NON_SECURE,
	},
};

static bl_params_node_t bl33_node = {
	.image_id = BL33_IMAGE_ID,
	.image_info = &bl33_image,
	.ep_info = &bl33_ep,
};

static bl_params_t bl_params = {
	.h = {
		.type = PARAM_BL_PARAMS,
		.version = PARAM_VERSION_2,
		.size = sizeof(bl_params),
		.attr = 0
	},
	.head = &bl33_node,
};

static const struct mapping initial_mappings[] = {
	{.first = 0, .last = 0xf7ffffff, .type = MEM_TYPE_NORMAL},
	{.first = 0xf8000000, .last = 0xff8bffff, .type = MEM_TYPE_DEV_nGnRnE},
	{.first = 0xff8c0000, .last = 0xff8effff, .type = MEM_TYPE_NORMAL},
	{.first = 0xff8f0000, .last = 0xffffffff, .type = MEM_TYPE_DEV_nGnRnE},
	{.first = 0, .last = 0, .type = 0}
};

static const struct address_range critical_ranges[] = {
	{.first = __start__, .last = __end__},
	{.first = uart, .last = uart},
	ADDRESS_RANGE_INVALID
};

static u64 elf_magic[3] = {
	0x00010102464c457f,
	0,
	0x0000000100b70002
};

struct elf_header {
	u64 magic[3];
	u64 entry;
	u64 prog_h_off;
	u64 sec_h_off;
	u32 flags;
	u16 elf_h_size;
	u16 prog_h_entry_size;
	u16 num_prog_h;
	u16 sec_h_entry_size;
	u16 num_sec_h;
	u16 sec_h_str_idx;
};

struct program_header {
	u32 type;
	u32 flags;
	u64 offset;
	u64 vaddr;
	u64 paddr;
	u64 file_size;
	u64 mem_size;
	u64 alignment;
};

typedef void (*bl31_entry)(bl_params_t *, u64, u64, u64);

static void load_elf(const struct elf_header *header) {
	for_range(i, 0, 16) {
		const u32 *x = (u32*)((u64)header + 16*i);
		printf("%2x0: %08x %08x %08x %08x\n", i, x[0], x[1], x[2], x[3]);
	}
	for_array(i, elf_magic) {
		assert_msg(header->magic[i] == elf_magic[i], "value 0x%016zx at offset %u != 0x%016zx", header->magic[i], 8*i, elf_magic[i]);
	}
	printf("Loading ELF: entry address %zx, %u program headers at %zx\n", header->entry, header->num_prog_h, header->prog_h_off);
	assert(header->prog_h_entry_size == 0x38);
	assert((header->prog_h_off & 7) == 0);
	for_range(i, 0, header->num_prog_h) {
		const struct program_header *ph = (const struct program_header*)((u64)header + header->prog_h_off + header->prog_h_entry_size * i);
		if (ph->type == 0x6474e551) {puts("ignoring GNU_STACK segment\n"); continue;}
		assert_msg(ph->type == 1, "found unexpected segment type %08x\n", ph->type);
		printf("LOAD %08zx…%08zx → %08zx\n", ph->offset, ph->offset + ph->file_size, ph->vaddr);
		assert(ph->vaddr == ph->paddr);
		assert(ph->flags == 7);
		assert(ph->offset % ph->alignment == 0);
		assert(ph->vaddr % ph->alignment == 0);
		u64 alignment = ph->alignment;
		assert(alignment % 16 == 0);
		const u64 *src = (const u64 *)((u64)header + ph->offset), *end = src + ((ph->file_size + alignment - 1) / alignment * (alignment / 8));
		u64 *dest = (u64*)ph->vaddr;
		while (src < end) {*dest++ = *src++;}
	}
}

#define DRAM_START 0
#define TZRAM_SIZE 0x00200000

static u32 dram_size() {return 0xf8000000;}
void transform_fdt(const struct fdt_header *header, void *dest, void *initcpio_start, void *initcpio_end, u64 dram_start, u64 dram_size);

static const u64 elf_addr = 0x04200000, fdt_addr = 0x00100000, fdt_out_addr = 0x00180000, payload_addr = 0x00280000;
#ifdef CONFIG_ELFLOADER_DECOMPRESSION
static const u64 blob_addr = 0x04400000;
_Alignas(16) u8 decomp_state[1 << 14];
#ifdef CONFIG_ELFLOADER_INITCPIO
static const u64 initcpio_addr = 0x08000000;
#endif
#endif

#ifdef CONFIG_ELFLOADER_DECOMPRESSION
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
	while ((pos = async->pos) <= old_pos) {
		debug("waiting for data …\n");
		__asm__("wfi");
	}
	return pos;
}

static size_t UNUSED decompress(struct async_transfer *async, size_t offset, u8 *out, u8 **out_end) {
	struct decompressor_state *state = (struct decompressor_state *)decomp_state;
	size_t size, xfer_pos = async->pos;
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
					assert_msg(res >= NUM_DECODE_STATUS, "decompression failed, status: %zu\n", res);
					offset += res - NUM_DECODE_STATUS;
				}
			}
			info("decompressed %zu bytes in %zu μs\n", state->out - out, (get_timestamp() - start) / CYCLES_PER_MICROSECOND);
			*out_end = state->out;
			return offset;
		}
	}
	die("couldn't probe");
}
#endif

_Noreturn u32 ENTRY main() {
	puts("elfloader\n");
	struct stage_store store;
	stage_setup(&store);
	mmu_setup(initial_mappings, critical_ranges);
	setup_pll(cru + CRU_CPLL_CON, 1000);
	/* aclk_gic = 200 MHz */
	cru[CRU_CLKSEL_CON + 56] = SET_BITS16(1, 0) << 15 | SET_BITS16(5, 4) << 8;
	/* aclk_cci = 500 MHz, DTS has 600 */
	cru[CRU_CLKSEL_CON + 5] = SET_BITS16(2, 0) << 6 | SET_BITS16(5, 1);
	/* aclk_perilp0 = hclk_perilp0 = 1 GHz, pclk_perilp = 500 MHz */
	cru[CRU_CLKSEL_CON + 23] = SET_BITS16(1, 0) << 7 | SET_BITS16(5, 0) | SET_BITS16(2, 0) << 8 | SET_BITS16(3, 1);
	/* hclk_perilp1 = pclk_perilp1 = 333 MHz, DTS has 400 */
	cru[CRU_CLKSEL_CON + 25] = SET_BITS16(1, 0) << 7 | SET_BITS16(5, 2) | SET_BITS16(3, 0) << 8;
#ifdef CONFIG_ELFLOADER_DECOMPRESSION
	struct async_transfer *async;
#if CONFIG_ELFLOADER_MEMORY
	struct async_transfer xfer = {
		.buf = (u8 *)blob_addr,
		.total_bytes = 60 << 20,
		.pos = 60 << 20,
	};
	async = &xfer;
#else
#if CONFIG_ELFLOADER_SPI
	async = &spi1_async;
	async->total_bytes = 16 << 20;
#else
#error No elfloader payload source specified
#endif
	async->buf = (u8 *)blob_addr;
	async->pos = 0;
#endif
#ifdef CONFIG_ELFLOADER_SPI
	gicv2_global_setup(gic500d);
	gicv3_per_cpu_setup(gic500r);
	u64 xfer_start = get_timestamp();
#ifdef SPI_POLL
	rkspi_read_flash_poll(spi1, blob, blob_end - blob, 0);
#else
	rkspi_start_irq_flash_read(0);
#ifdef SPI_SYNC_IRQ
	while (spi1_async.pos < spi1_async.total_bytes) {
		debug("idle pos=0x%zx rxlvl=%"PRIu32", rxthreshold=%"PRIu32"\n", spi1_state.pos, spi->rx_fifo_level, spi->rx_fifo_threshold);
		__asm__("wfi");
	}
#endif
#endif
#endif
	u8 *end = (u8 *)blob_addr;
	size_t offset = decompress(async, 0, (u8 *)elf_addr, &end);
	end = (u8 *)fdt_out_addr;
	offset = decompress(async, offset, (u8 *)fdt_addr, &end);
	end = __start__;
	offset = decompress(async, offset, (u8 *)payload_addr, &end);
#ifdef CONFIG_ELFLOADER_INITCPIO
	u8 *initcpio_end = (u8 *)(u64)(DRAM_START + dram_size());
	offset = decompress(async, offset, (u8 *)initcpio_addr, &initcpio_end);
#endif

#ifdef CONFIG_ELFLOADER_SPI
#ifndef SPI_POLL
	rkspi_end_irq_flash_read();
#endif
	u64 xfer_end = get_timestamp();
	printf("transfer finished after %zu μs\n", (xfer_end - xfer_start) / CYCLES_PER_MICROSECOND);
	gicv3_per_cpu_teardown(gic500r);
#endif
#endif
	const struct elf_header *header = (const struct elf_header*)elf_addr;
	load_elf(header);

	transform_fdt((const struct fdt_header *)fdt_addr, (void *)fdt_out_addr,
#ifdef CONFIG_ELFLOADER_INITCPIO
		(void *)initcpio_addr, initcpio_end,
#else
		0, 0,
#endif
	       DRAM_START + TZRAM_SIZE, dram_size() - TZRAM_SIZE
	);

	bl33_ep.pc = payload_addr;
	bl33_ep.spsr = 9; /* jump into EL2 with SPSel = 1 */
	bl33_ep.args.arg0 = fdt_out_addr;
	bl33_ep.args.arg1 = 0;
	bl33_ep.args.arg2 = 0;
	bl33_ep.args.arg3 = 0;
	setup_pll(cru + CRU_BPLL_CON, 600);
	/* aclkm_core_b = clk_core_b = BPLL */
	cru[CRU_CLKSEL_CON + 2] = SET_BITS16(5, 0) << 8 | SET_BITS16(2, 1) << 6 | SET_BITS16(5, 0);
	stage_teardown(&store);
	while (~uart->line_status & 0x60) {__asm__ volatile("yield");}
	uart->shadow_fifo_enable = 0;
	((bl31_entry)header->entry)(&bl_params, 0, 0, 0);
	puts("return\n");
	halt_and_catch_fire();
}
