/* SPDX-License-Identifier: CC0-1.0 */
#include "elfloader.h"
#include <inttypes.h>
#include <main.h>
#include <uart.h>
#include <rk3399.h>
#include <rkpll.h>
#include <stage.h>
#include <compression.h>
#include <async.h>
#include <rki2c.h>
#include <rki2c_regs.h>
#include <exc_handler.h>
#include <rkgpio_regs.h>
#include <dump_mem.h>
#if CONFIG_ELFLOADER_SPI
#include <rkspi.h>
#include <rkspi_regs.h>
#include "rk3399_spi.h"
#endif
#include <gic.h>
#include <gic_regs.h>
#if CONFIG_ELFLOADER_SD
#include <dwmmc.h>
#endif
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
	MAPPING_BINARY,
	{.first = 0, .last = (u64)&__start__ - 1, .flags = MEM_TYPE_NORMAL},
	{.first = 0x4100000, .last = 0xf7ffffff, .flags = MEM_TYPE_NORMAL},
	{.first = (u64)uart, .last = (u64)uart + 0xfff, .flags = MEM_TYPE_DEV_nGnRnE},
	{.first = 0, .last = 0, .flags = 0}
};

static const struct address_range critical_ranges[] = {
	{.first = __start__, .last = __end__ - 1},
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
		const u64 words_copied = (ph->file_size + 7) >> 3;
		const u64 words_clear = ((ph->mem_size + 7) >> 3) - words_copied;
		const u64 *src = (const u64 *)((u64)header + ph->offset);
		const u64 *end = (const u64 *)ph->vaddr + words_copied;
		u64 *dest = (u64*)ph->vaddr;
		debug("copying to %"PRIx64"–%"PRIx64"\n", dest, (u64)end);
		while (dest < end) {*dest++ = *src++;}
		end += words_clear;
		debug("clearing to %"PRIx64"\n", (u64)end);
		while (dest < end) {*dest++ = 0;}
	}
}

#define DRAM_START ((u64)0)
#define TZRAM_SIZE 0x00200000

static u32 dram_size() {return 0xf8000000;}
void transform_fdt(const struct fdt_header *header, void *input_end, void *dest, void *initcpio_start, void *initcpio_end, u64 dram_start, u64 dram_size);

static const u64 elf_addr = 0x04200000, fdt_addr = 0x00100000, fdt_out_addr = 0x00180000, payload_addr = 0x00280000;
static const u64 blob_addr = 0x04400000;
#ifdef CONFIG_ELFLOADER_DECOMPRESSION
static _Alignas(16) u8 decomp_state[1 << 14];
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

static void UNUSED async_wait(struct async_transfer *async) {
	while (async->pos < async->total_bytes) {
#if CONFIG_ELFLOADER_SPI
		spew("idle pos=0x%zx rxlvl=%"PRIu32", rxthreshold=%"PRIu32"\n", spi1_state.pos, spi->rx_fifo_level, spi->rx_fifo_threshold);
#elif CONFIG_ELFLOADER_SD
#ifdef SPEW_MSG
		spew("idle ");dwmmc_print_status(sdmmc);
#endif
#endif
		__asm__("wfi");
	}
}

static const char *const decode_status_msg[NUM_DECODE_STATUS] = {
#define X(name, msg) msg,
	DEFINE_DECODE_STATUS
#undef X
};

static size_t UNUSED decompress(struct async_transfer *async, size_t offset, u8 *out, u8 **out_end) {
#ifdef ASYNC_WAIT
	async_wait(async);
#endif
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
					assert_msg(res >= NUM_DECODE_STATUS, "decompression failed, status: %zu (%s)\n", res, decode_status_msg[res]);
					offset += res - NUM_DECODE_STATUS;
				}
			}
			info("decompressed %zu bytes in %zu μs\n", state->out - out, (get_timestamp() - start) / CYCLES_PER_MICROSECOND);
			*out_end = state->out;
			return offset;
		}
	}
#if DEBUG_MSG
	dump_mem(buf + offset, xfer_pos - offset < 1024 ? xfer_pos - offset : 1024);
#endif
	die("couldn't probe");
}
#endif

#if CONFIG_EXC_STACK
void sync_exc_handler(struct exc_state_save UNUSED *save) {
	u64 elr, esr, far;
	__asm__("mrs %0, esr_el3; mrs %1, far_el3; mrs %2, elr_el3" : "=r"(esr), "=r"(far), "=r"(elr));
	die("sync exc@0x%"PRIx64": ESR_EL3=0x%"PRIx64", FAR_EL3=0x%"PRIx64"\n", elr, esr, far);
}
#endif

void rk3399_spi_setup();

#ifdef CONFIG_ELFLOADER_DECOMPRESSION
void decompress_payload(struct async_transfer *async, struct payload_desc *payload) {
	payload->elf_end -= LZCOMMON_BLOCK;
	size_t offset = decompress(async, 0, payload->elf_start, &payload->elf_end);
	payload->fdt_end -= LZCOMMON_BLOCK;
	offset = decompress(async, offset, (u8 *)fdt_addr, &payload->fdt_end);
	payload->kernel_end -= LZCOMMON_BLOCK;
	offset = decompress(async, offset, (u8 *)payload_addr, &payload->kernel_end);
#ifdef CONFIG_ELFLOADER_INITCPIO
	payload->initcpio_end -= LZCOMMON_BLOCK;
	offset = decompress(async, offset, (u8 *)initcpio_addr, &payload->initcpio_end);
#endif
}
#endif

#if CONFIG_ELFLOADER_MEMORY
static void load_from_memory(struct payload_desc *payload, u8 UNUSED *buf, size_t buf_size) {
	struct async_transfer async = {
		.buf = (u8 *)blob_addr,
		.total_bytes = buf_size,
		.pos = buf_size,
	};
	decompress_payload(&async, payload);
}
#endif

void load_from_spi(struct payload_desc *payload, u8 *buf, size_t buf_size);
_Bool load_from_sd(struct payload_desc *payload, u8 *buf, size_t buf_size);

static void init_payload_desc(struct payload_desc *payload) {
	payload->elf_start = (u8 *)elf_addr;
	payload->elf_end =  (u8 *)blob_addr;
	payload->fdt_start = (u8 *)fdt_addr;
	payload->fdt_end = (u8 *)fdt_out_addr;
	payload->kernel_start = (u8 *)payload_addr;
	payload->kernel_end = __start__;

#if CONFIG_ELFLOADER_INITCPIO
	payload->initcpio_start = (u8 *)initcpio_addr;
	payload->initcpio_end = (u8 *)(DRAM_START + dram_size());
#endif
}

_Noreturn u32 main(u64 sctlr) {
	puts("elfloader\n");
	struct stage_store store;
	store.sctlr = sctlr;
	stage_setup(&store);
	mmu_setup(initial_mappings, critical_ranges);
#ifdef CONFIG_EXC_STACK
	sync_exc_handler_spx = sync_exc_handler_sp0 = sync_exc_handler;
#endif
	mmu_map_mmio_identity(0xff750000, 0xff77ffff);	/* {PMU,}CRU, GRF */
	mmu_map_mmio_identity(0xff310000, 0xff33ffff);	/* PMU{,SGRF,GRF} */
	mmu_map_range(0xff8c0000, 0xff8dffff, 0xff8c0000, MEM_TYPE_NORMAL);	/* SRAM */
	mmu_map_range(0xff3b0000, 0xff3b1fff, 0xff3b0000, MEM_TYPE_NORMAL);	/* PMUSRAM */
	mmu_map_mmio_identity(0xff3d0000, 0xff3dffff);	/* i2c4 */
	mmu_map_mmio_identity((u64)gpio0, (u64)gpio0 + 0xfff);
	dsb_ishst();

	assert_msg(rkpll_switch(pmucru + PMUCRU_PPLL_CON), "PPLL did not lock-on\n");
	/* clk_i2c4 = PPLL/4 = 169 MHz, DTS has 200 */
	pmucru[PMUCRU_CLKSEL_CON + 3] = SET_BITS16(7, 0);
	printf("RKI2C4_CON: %"PRIx32"\n", i2c4->control);
	struct rki2c_config i2c_cfg = rki2c_calc_config_v1(169, 1000000, 600, 20);
	printf("setup %"PRIx32" %"PRIx32"\n", i2c_cfg.control, i2c_cfg.clkdiv);
	i2c4->clkdiv = i2c_cfg.clkdiv;
	i2c4->control = i2c_cfg.control;
	pmugrf[PMUGRF_GPIO1B_IOMUX] = SET_BITS16(2, 1) << 6 | SET_BITS16(2, 1) << 8;
	i2c4->control = i2c_cfg.control | RKI2C_CON_ENABLE | RKI2C_CON_START | RKI2C_CON_MODE_REGISTER_READ | RKI2C_CON_ACK;
	i2c4->slave_addr = 1 << 24 | 0x62 << 1;
	i2c4->reg_addr = 1 << 24 | 0;
	i2c4->rx_count = 1;
	u32 val;
	while (!((val = i2c4->int_pending) & RKI2C_INTMASK_XACT_END)) {}
	printf("RKI2C4_CON: %"PRIx32", _IPD: %"PRIx32"\n", i2c4->control, i2c4->int_pending);
	_Bool is_pbp = !(val & 1 << RKI2C_INT_NAK);
	printf("%"PRIx32"\n", i2c4->rx_data[0]);
	i2c4->control = i2c_cfg.control | RKI2C_CON_ENABLE | RKI2C_CON_STOP;

	if (is_pbp) {
		mmu_map_mmio_identity(0xff420000, 0xff420fff);
		dsb_ishst();
		info("ACK from i2c4-62, this seems to be a Pinebook Pro\n");
		*(volatile u32*)0xff420024 = 0x96e;
		*(volatile u32*)0xff420028 = 0x25c;
		*(volatile u32*)0xff42002c = 0x13;
        pmugrf[PMUGRF_GPIO1C_IOMUX] = SET_BITS16(2, 1) << 6;
	} else {
		info("not running on a Pinebook Pro ⇒ not setting up regulators\n");
	}

	struct payload_desc payload;
	init_payload_desc(&payload);

#ifdef CONFIG_ELFLOADER_DECOMPRESSION
#if CONFIG_ELFLOADER_IRQ
	mmu_map_mmio_identity(0xfee00000, 0xfeffffff);
	dsb_ishst();
	gicv2_global_setup(gic500d);
	gicv3_per_cpu_setup(gic500r);
	u64 xfer_start = get_timestamp();
#endif

#if CONFIG_ELFLOADER_MEMORY
	load_from_memory(&payload, (u8 *)blob_addr, 60 << 20);
#elif CONFIG_ELFLOADER_SD && CONFIG_ELFLOADER_SPI
	_Bool sd_success = load_from_sd(&payload, (u8 *)blob_addr, 60 << 20);
	printf("GPIO0: %08"PRIx32"\n", gpio0->read);
	if (!sd_success || ~gpio0->read & 32) {
		init_payload_desc(&payload);
		load_from_spi(&payload, (u8 *)blob_addr, 60 << 20);
	}
#elif CONFIG_ELFLOADER_SPI
	load_from_spi(&payload, (u8 *)blob_addr, 60 << 20);
#elif CONFIG_ELFLOADER_SD
	assert_msg(load_from_sd(&payload, (u8 *)blob_addr, 60 << 20), "loading the payload failed");
#else
#error No elfloader payload source specified
#endif

#if CONFIG_ELFLOADER_IRQ
	u64 xfer_end = get_timestamp();
	printf("transfer finished after %zu μs\n", (xfer_end - xfer_start) / CYCLES_PER_MICROSECOND);
	gicv3_per_cpu_teardown(gic500r);
#endif
#endif

	/* GPIO0B3: White and green LED on the RockPro64 and Pinebook Pro respectively, not connected on the Rock Pi 4 */
	gpio0->port |= 1 << 11;
	gpio0->direction |= 1 << 11;

	const struct elf_header *header = (const struct elf_header*)payload.elf_start;
	load_elf(header);

	transform_fdt((const struct fdt_header *)payload.fdt_start, payload.fdt_end, (void *)fdt_out_addr,
#ifdef CONFIG_ELFLOADER_INITCPIO
		payload.initcpio_start, payload.initcpio_end,
#else
		0, 0,
#endif
	       DRAM_START + TZRAM_SIZE, dram_size() - TZRAM_SIZE
	);

	bl33_ep.pc = (uintptr_t)payload.kernel_start;
	bl33_ep.spsr = 9; /* jump into EL2 with SPSel = 1 */
	bl33_ep.args.arg0 = fdt_out_addr;
	bl33_ep.args.arg1 = 0;
	bl33_ep.args.arg2 = 0;
	bl33_ep.args.arg3 = 0;
	assert_msg(rkpll_switch(cru + CRU_BPLL_CON), "BPLL did not lock-on\n");
	/* aclkm_core_b = clk_core_b = BPLL */
	cru[CRU_CLKSEL_CON + 2] = SET_BITS16(5, 0) << 8 | SET_BITS16(2, 1) << 6 | SET_BITS16(5, 0);
	cru[CRU_CLKGATE_CON+1] = SET_BITS16(8, 0);
	stage_teardown(&store);
	while (~uart->line_status & 0x60) {__asm__ volatile("yield");}
	uart->shadow_fifo_enable = 0;
	((bl31_entry)header->entry)(&bl_params, 0, 0, 0);
	puts("return\n");
	halt_and_catch_fire();
}
