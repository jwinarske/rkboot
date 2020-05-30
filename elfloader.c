/* SPDX-License-Identifier: CC0-1.0 */
#include <inttypes.h>
#include <main.h>
#include <uart.h>
#include <rk3399.h>
#include <stage.h>
#include <compression.h>
#include <async.h>
#include <rki2c.h>
#include <rki2c_regs.h>
#if CONFIG_ELFLOADER_SPI
#include <rkspi.h>
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

#if CONFIG_ELFLOADER_SD
struct async_transfer sdmmc_async;
static volatile struct dwmmc_regs *const sdmmc = (volatile struct dwmmc_regs*)0xfe320000;

static const u32 sdmmc_intid = 97, sdmmc_irq_threshold = 128;

static void handle_sdmmc_interrupt(volatile struct dwmmc_regs *sdmmc, struct async_transfer *async) {
	u32 items_to_read = 0, rintsts = sdmmc->rintsts, ack = 0;
	assert((rintsts & DWMMC_ERROR_INT_MASK) == 0);
	if (rintsts & DWMMC_INT_DATA_TRANSFER_OVER) {
		ack |= DWMMC_INT_DATA_TRANSFER_OVER;
		items_to_read = sdmmc->status >> 17 & 0x1fff;
	}
	if (rintsts & DWMMC_INT_RX_FIFO_DATA_REQ) {
		ack |= DWMMC_INT_RX_FIFO_DATA_REQ;
		if (items_to_read < sdmmc_irq_threshold) {
			items_to_read = sdmmc_irq_threshold;
		}
	}
	if (items_to_read) {
		u32 *buf = (u32*)async->buf;
		size_t pos = async->pos;
		assert(pos % 4 == 0);
		for_range(i, 0, items_to_read) {
			buf[pos/4] = *(volatile u32 *)0xfe320200;
			pos += 4;
		}
		async->pos = pos;
	}
#ifdef DEBUG_MSG
	if (unlikely(!ack)) {
		dwmmc_print_status(sdmmc);
		debug("don't know what to do with interrupt\n");
	} else if (ack == 0x20) {
		debugs(".");
	} else {
		debug("ack %"PRIx32"\n", ack);
	}
#endif
	sdmmc->rintsts = ack;
}

static void irq_handler() {
	u64 grp0_intid;
	__asm__ volatile("mrs %0, "ICC_IAR0_EL1 : "=r"(grp0_intid));
	u64 sp;
	__asm__("add %0, SP, #0" : "=r"(sp));
	spew("SP=%"PRIx64"\n", sp);
	if (grp0_intid >= 1020 && grp0_intid < 1023) {
		if (grp0_intid == 1020) {
			die("intid1020");
		} else if (grp0_intid == 1021) {
			die("intid1021");
		} else if (grp0_intid == 1022) {
			die("intid1022");
		}
	} else {
		__asm__("msr DAIFClr, #0xf");
		if (grp0_intid == sdmmc_intid) {
			spew("SDMMC interrupt, buf=0x%zx\n", (size_t)sdmmc_async.buf);
			handle_sdmmc_interrupt(sdmmc, &sdmmc_async);
		} else if (grp0_intid == 1023) {
			debugs("spurious interrupt\n");
		} else {
			die("unexpected group 0 interrupt");
		}
	}
	__asm__ volatile("msr DAIFSet, #0xf;msr "ICC_EOIR0_EL1", %0" : : "r"(grp0_intid));
}

extern void (*volatile fiq_handler_spx)();
extern void (*volatile irq_handler_spx)();

void dwmmc_start_irq_read(volatile struct dwmmc_regs *dwmmc, u32 sector) {
	fiq_handler_spx = irq_handler_spx = irq_handler;
	gicv2_setup_spi(gic500d, sdmmc_intid, 0x80, 1, IGROUP_0 | INTR_LEVEL);
	dwmmc->intmask = DWMMC_ERROR_INT_MASK | DWMMC_INT_DATA_TRANSFER_OVER | DWMMC_INT_RX_FIFO_DATA_REQ | DWMMC_INT_TX_FIFO_DATA_REQ;
	dwmmc->ctrl |= DWMMC_CTRL_INT_ENABLE;
	assert(sdmmc_async.total_bytes % 512 == 0);
	dwmmc->blksiz = 512;
	dwmmc->bytcnt = sdmmc_async.total_bytes;
	enum dwmmc_status st = dwmmc_wait_cmd_done(dwmmc, 18 | DWMMC_R1 | DWMMC_CMD_DATA_EXPECTED, sector, 1000);
	dwmmc_check_ok_status(dwmmc, st, "CMD18 (READ_MULTIPLE_BLOCK)");
}
#endif
#endif

void sd_dump_cid(u32 cid0, u32 cid1, u32 cid2, u32 cid3) {
	u32 cid[4] = {cid0, cid1, cid2, cid3};
	for_range(i, 0, 4) {
		info("CID%"PRIu32": %08"PRIx32"\n", i, cid[i]);
	}
	info("card month: %04"PRIu32"-%02"PRIu32", serial 0x%08"PRIx32"\n", (cid[0] >> 12 & 0xfff) + 2000, cid[0] >> 8 & 0xf, cid[0] >> 24 | cid[1] << 8);
	info("mfg 0x%02"PRIx32" oem 0x%04"PRIx32" hwrev %"PRIu32" fwrev %"PRIu32"\n", cid[3] >> 24, cid[3] >> 8 & 0xffff, cid[1] >> 28 & 0xf, cid[1] >> 24 & 0xf);
	char prod_name[6] = {cid[3] & 0xff, cid[2] >> 24 & 0xff, cid[2] >> 16 & 0xff, cid[2] >> 8 & 0xff, cid[2] & 0xff, 0};
	info("product name: %s\n", prod_name);
}

_Noreturn u32 ENTRY main() {
	puts("elfloader\n");
	struct stage_store store;
	stage_setup(&store);
	mmu_setup(initial_mappings, critical_ranges);

	/* clk_i2c4 = PPLL (= 24 MHz) */
	pmucru[PMUCRU_CLKSEL_CON + 3] = SET_BITS16(7, 0);
	printf("RKI2C4_CON: %"PRIx32"\n", i2c4->control);
	struct rki2c_config i2c_cfg = rki2c_calc_config_v1(24, 1000000, 600, 20);
	printf("setup %"PRIx32" %"PRIx32"\n", i2c_cfg.control, i2c_cfg.clkdiv);
	i2c4->clkdiv = i2c_cfg.clkdiv;
	i2c4->control = i2c_cfg.control;
	pmugrf[PMUGRF_GPIO1B_IOMUX] = SET_BITS16(2, 1) << 6 | SET_BITS16(2, 1) << 8;
	i2c4->control = i2c_cfg.control | RKI2C_CON_ENABLE | RKI2C_CON_START | RKI2C_CON_MODE_REGISTER_READ;
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
		info("ACK from i2c4-62, this seems to be a Pinebook Pro\n");
		/* set up PWM2 without muxing it out, just so the kernel will find a value */
		*(volatile u32*)0xff420024 = 1240;
		*(volatile u32*)0xff420028 = 310;
		*(volatile u32*)0xff42002c = 0x13;
		pmucru[0x104/4] = SET_BITS16(1, 1) << 10;
	} else {
		info("not running on a Pinebook Pro ⇒ not setting up regulators or LEDs\n");
	}

	setup_pll(cru + CRU_CPLL_CON, 800);
	/* aclk_gic = 200 MHz */
	cru[CRU_CLKSEL_CON + 56] = SET_BITS16(1, 0) << 15 | SET_BITS16(5, 3) << 8;
	/* aclk_cci = 400 MHz, DTS has 600 */
	cru[CRU_CLKSEL_CON + 5] = SET_BITS16(2, 0) << 6 | SET_BITS16(5, 1);
	/* aclk_perilp0 = hclk_perilp0 = 100 MHz, pclk_perilp = 50 MHz */
	cru[CRU_CLKSEL_CON + 23] = SET_BITS16(1, 0) << 7 | SET_BITS16(5, 7) | SET_BITS16(2, 0) << 8 | SET_BITS16(3, 1) << 12;
	/* hclk_perilp1 = pclk_perilp1 = 400 MHz */
	cru[CRU_CLKSEL_CON + 25] = SET_BITS16(1, 0) << 7 | SET_BITS16(5, 3) | SET_BITS16(3, 1) << 8;
#if CONFIG_ELFLOADER_SD
	/* aclk_perihp = 125 MHz, hclk_perihp = aclk_perihp / 2, pclk_perihp = aclk_perihp / 4 */
	cru[CRU_CLKSEL_CON + 14] = SET_BITS16(3, 3) << 12 | SET_BITS16(2, 1) << 8 | SET_BITS16(1, 0) << 7 | SET_BITS16(5, 7);
	/* hclk_sd = 200 MHz */
	cru[CRU_CLKSEL_CON + 13] = SET_BITS16(1, 0) << 15 | SET_BITS16(5, 4) << 8;
	/* clk_sdmmc = 24 MHz / 30 = 800 kHz */
	cru[CRU_CLKSEL_CON + 16] = SET_BITS16(3, 5) << 8 | SET_BITS16(7, 29);
	/* drive phase 180° */
	cru[CRU_SDMMC_CON] = SET_BITS16(1, 1);
	cru[CRU_SDMMC_CON + 0] = SET_BITS16(2, 1) << 1 | SET_BITS16(8, 0) << 3 | SET_BITS16(1, 0) << 11;
	cru[CRU_SDMMC_CON + 1] = SET_BITS16(2, 0) << 1 | SET_BITS16(8, 0) << 3 | SET_BITS16(1, 0) << 11;
	cru[CRU_SDMMC_CON] = SET_BITS16(1, 0);
	/* mux out the SD lines */
	grf[GRF_GPIO4B_IOMUX] = SET_BITS16(2, 1) | SET_BITS16(2, 1) << 2 | SET_BITS16(2, 1) << 4 | SET_BITS16(2, 1) << 6 | SET_BITS16(2, 1) << 8 | SET_BITS16(2, 1) << 10;
	/* mux out card detect */
	pmugrf[PMUGRF_GPIO0A_IOMUX] = SET_BITS16(2, 1) << 14;
	/* reset SDMMC */
	cru[CRU_SOFTRST_CON + 7] = SET_BITS16(1, 1) << 10;
	udelay(100);
	cru[CRU_SOFTRST_CON + 7] = SET_BITS16(1, 0) << 10;
	udelay(2000);
#endif

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
#elif CONFIG_ELFLOADER_SD
	async = &sdmmc_async;
	async->total_bytes = 60 << 20;
	info("starting SDMMC\n");
	dwmmc_init(sdmmc);
	static const u32 sd_start_sector = 4 << 11; /* offset 4 MiB */
#else
#error No elfloader payload source specified
#endif
	async->buf = (u8 *)blob_addr;
	async->pos = 0;
#endif

#if CONFIG_EXC_STACK
	gicv2_global_setup(gic500d);
	gicv3_per_cpu_setup(gic500r);
	u64 xfer_start = get_timestamp();
#endif

#if CONFIG_ELFLOADER_SPI

#ifdef SPI_POLL
	rkspi_read_flash_poll(spi1, blob, blob_end - blob, 0);
#else
	rkspi_start_irq_flash_read(0);
#endif

#elif CONFIG_ELFLOADER_SD

#ifdef SD_POLL
	dwmmc_read_poll(sdmmc, sd_start_sector, async->buf, async->total_bytes);
	async->pos = async->total_bytes;
#else
	dwmmc_start_irq_read(sdmmc, sd_start_sector);
#endif

#endif

#ifdef ASYNC_WAIT
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
#endif

	u8 *end = (u8 *)blob_addr - LZCOMMON_BLOCK;
	size_t offset = decompress(async, 0, (u8 *)elf_addr, &end);
	end = (u8 *)fdt_out_addr - LZCOMMON_BLOCK;
	offset = decompress(async, offset, (u8 *)fdt_addr, &end);
	end = __start__ - LZCOMMON_BLOCK;
	offset = decompress(async, offset, (u8 *)payload_addr, &end);
#ifdef CONFIG_ELFLOADER_INITCPIO
	u8 *initcpio_end = (u8 *)(u64)(DRAM_START + dram_size() - LZCOMMON_BLOCK);
	offset = decompress(async, offset, (u8 *)initcpio_addr, &initcpio_end);
#endif

#if CONFIG_ELFLOADER_SPI

#ifndef SPI_POLL
	rkspi_end_irq_flash_read();
#endif

#elif CONFIG_ELFLOADER_SD

#ifndef SD_POLL
	gicv2_disable_spi(gic500d, sdmmc_intid);
	fiq_handler_spx = irq_handler_spx = 0;
#endif

#endif
#endif

#if CONFIG_EXC_STACK
	u64 xfer_end = get_timestamp();
	printf("transfer finished after %zu μs\n", (xfer_end - xfer_start) / CYCLES_PER_MICROSECOND);
	gicv3_per_cpu_teardown(gic500r);
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
