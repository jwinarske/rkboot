/* SPDX-License-Identifier: CC0-1.0 */
#include <inttypes.h>
#include <main.h>
#include <uart.h>
#include <rk3399.h>
#include <stage.h>
#include <compression.h>
#include <rkspi.h>
#include <gic.h>
#include <dwmmc.h>
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
#endif
#endif

enum {
	SD_OCR_HIGH_CAPACITY = 1 << 30,
	SD_OCR_XPC = 1 << 28,
	SD_OCR_S18R = 1 << 24,
};
enum {
	SD_RESP_BUSY = 1 << 31,
};

static void UNUSED dwmmc_check_ok_status(volatile struct dwmmc_regs *dwmmc, enum dwmmc_status st, const char *context) {
	assert_msg(st == DWMMC_ST_OK, "error during %s: status=0x%08"PRIx32" rintsts=0x%08"PRIx32"\n", context, dwmmc->status, dwmmc->rintsts);
}

static void UNUSED sd_dump_cid(u32 cid0, u32 cid1, u32 cid2, u32 cid3) {
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
	setup_pll(cru + CRU_CPLL_CON, 1000);
	/* aclk_gic = 200 MHz */
	cru[CRU_CLKSEL_CON + 56] = SET_BITS16(1, 0) << 15 | SET_BITS16(5, 4) << 8;
	/* aclk_cci = 500 MHz, DTS has 600 */
	cru[CRU_CLKSEL_CON + 5] = SET_BITS16(2, 0) << 6 | SET_BITS16(5, 1);
	/* aclk_perilp0 = hclk_perilp0 = 1 GHz, pclk_perilp = 500 MHz */
	cru[CRU_CLKSEL_CON + 23] = SET_BITS16(1, 0) << 7 | SET_BITS16(5, 0) | SET_BITS16(2, 0) << 8 | SET_BITS16(3, 1);
	/* hclk_perilp1 = pclk_perilp1 = 333 MHz, DTS has 400 */
	cru[CRU_CLKSEL_CON + 25] = SET_BITS16(1, 0) << 7 | SET_BITS16(5, 2) | SET_BITS16(3, 0) << 8;
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
	volatile struct dwmmc_regs *sdmmc = (volatile struct dwmmc_regs*)0xfe320000;
	sdmmc->pwren = 1;
	udelay(1000);
	sdmmc->rintsts = ~(u32)0;
	sdmmc->intmask = 0;
	sdmmc->fifoth = 0x307f0080;
	sdmmc->tmout = 0xffffffff;
	sdmmc->ctrl = 3;
	{
		timestamp_t start = get_timestamp();
		while (sdmmc->ctrl & 3) {
			__asm__("yield");
			if (get_timestamp() - start > 100 * CYCLES_PER_MICROSECOND) {
				die("reset timeout, ctrl=%08"PRIx32"\n", sdmmc->ctrl);
			}
		}
	}
	info("SDMMC reset successful. HCON=%08"PRIx32"\n", sdmmc->hcon);
	sdmmc->rst_n = 1;
	udelay(5);
	sdmmc->rst_n = 0;
	sdmmc->ctype = 0;
	sdmmc->clkdiv[0] = 0;
	mmio_barrier();
	sdmmc->clkena = 1;
	dwmmc_wait_cmd(sdmmc, DWMMC_CMD_UPDATE_CLOCKS | DWMMC_CMD_SYNC_DATA);
	info("clock enabled\n");
	udelay(500);
	dwmmc_wait_cmd_done(sdmmc, 0 | DWMMC_CMD_SEND_INITIALIZATION | DWMMC_CMD_CHECK_RESPONSE_CRC, 0, 1000);
	udelay(10000);
	info("CMD0 resp=%08"PRIx32" rintsts=%"PRIx32"\n", sdmmc->resp[0], sdmmc->rintsts);
	info("status=%08"PRIx32"\n", sdmmc->status);
	enum dwmmc_status st = dwmmc_wait_cmd_done(sdmmc, 8 | DWMMC_R1, 0x1aa, 100000);
	assert_msg(st != DWMMC_ST_ERROR, "error on SET_IF_COND (CMD8)\n");
	assert_unimpl(st != DWMMC_ST_TIMEOUT, "SD <2.0 cards\n");
	info("CMD8 resp=%08"PRIx32" rintsts=%"PRIx32"\n", sdmmc->resp[0], sdmmc->rintsts);
	assert_msg((sdmmc->resp[0] & 0xff) == 0xaa, "CMD8 check pattern returned incorrectly\n");
	info("status=%08"PRIx32"\n", sdmmc->status);
	u32 resp;
	{
		timestamp_t start = get_timestamp();
		while (1) {
			st = dwmmc_wait_cmd_done(sdmmc, 55 | DWMMC_R1, 0, 1000);
			dwmmc_check_ok_status(sdmmc, st, "CMD55 (APP_CMD)");
			st = dwmmc_wait_cmd_done(sdmmc, 41 | DWMMC_R3, 0x00ff8000 | SD_OCR_HIGH_CAPACITY | SD_OCR_S18R, 1000);
			dwmmc_check_ok_status(sdmmc, st, "ACMD41 (OP_COND)");
			resp = sdmmc->resp[0];
			if (resp & SD_RESP_BUSY) {break;}
			if (get_timestamp() - start > 100000 * CYCLES_PER_MICROSECOND) {
				die("timeout in initialization\n");
			}
			udelay(100);
		}
	}
	info("resp0=0x%08"PRIx32" status=0x%08"PRIx32"\n", resp, sdmmc->status);
	assert_msg(resp & SD_OCR_HIGH_CAPACITY, "card does not support high capacity addressing\n");
	st = dwmmc_wait_cmd_done(sdmmc, 2 | DWMMC_R2, 0, 1000);
	dwmmc_check_ok_status(sdmmc, st, "CMD2 (ALL_SEND_CID)");
	sd_dump_cid(sdmmc->resp[0], sdmmc->resp[1], sdmmc->resp[2], sdmmc->resp[3]);
	st = dwmmc_wait_cmd_done(sdmmc, 3 | DWMMC_R6, 0, 1000);
	dwmmc_check_ok_status(sdmmc, st, "CMD3 (SEND_RELATIVE_ADDRESS)");
	u32 rca = sdmmc->resp[0];
	info("RCA: %08"PRIx32"\n", rca);
	rca &= 0xffff0000;
	st = dwmmc_wait_cmd_done(sdmmc, 10 | DWMMC_R2, rca, 1000);
	dwmmc_check_ok_status(sdmmc, st, "CMD10 (SEND_CID)");
	sd_dump_cid(sdmmc->resp[0], sdmmc->resp[1], sdmmc->resp[2], sdmmc->resp[3]);
	st = dwmmc_wait_cmd_done(sdmmc, 7 | DWMMC_R1, rca, 1000);
	dwmmc_check_ok_status(sdmmc, st, "CMD7 (SELECT_CARD)");
	dwmmc_wait_not_busy(sdmmc, 1000 * CYCLES_PER_MICROSECOND);
	sdmmc->clkena = 0;
	dwmmc_wait_cmd(sdmmc, DWMMC_CMD_UPDATE_CLOCKS | DWMMC_CMD_SYNC_DATA);
	/* clk_sdmmc = 24 MHz */
	cru[CRU_CLKSEL_CON + 16] = SET_BITS16(3, 5) << 8 | SET_BITS16(7, 0);
	sdmmc->clkena = 1;
	dwmmc_wait_cmd(sdmmc, DWMMC_CMD_UPDATE_CLOCKS | DWMMC_CMD_SYNC_DATA);
	st = dwmmc_wait_cmd_done(sdmmc, 16 | DWMMC_R1, 512, 1000);
	dwmmc_check_ok_status(sdmmc, st, "CMD16 (SET_BLOCKLEN)");
	info("resp0=0x%08"PRIx32" status=0x%08"PRIx32"\n", sdmmc->resp[0], sdmmc->status);
	st = dwmmc_wait_cmd_done(sdmmc, 55 | DWMMC_R1, rca, 1000);
	dwmmc_check_ok_status(sdmmc, st, "CMD55 (APP_CMD)");
	st = dwmmc_wait_cmd_done(sdmmc, 6 | DWMMC_R1, 2, 1000);
	dwmmc_check_ok_status(sdmmc, st, "ACMD6 (SET_BUS_WIDTH)");
	sdmmc->ctype = 1;
	st = dwmmc_wait_cmd_done(sdmmc, 55 | DWMMC_R1, rca, 1000);
	dwmmc_check_ok_status(sdmmc, st, "CMD55 (APP_CMD)");
	sdmmc->blksiz = 64;
	sdmmc->bytcnt = 64;
	st = dwmmc_wait_cmd_done(sdmmc, 13 | DWMMC_R1 | DWMMC_CMD_DATA_EXPECTED, 0, 1000);
	dwmmc_check_ok_status(sdmmc, st, "ACMD13 (SD_STATUS)");
	{
		u32 status;
		timestamp_t start = get_timestamp();
		while (~(status = sdmmc->rintsts) & DWMMC_INT_DATA_TRANSFER_OVER) {
			if (status & DWMMC_ERROR_INT_MASK) {
				die("error transferring SSR\n");
			}
			if (get_timestamp() - start > 1000 * CYCLES_PER_MICROSECOND) {
				die("SSR timeout\n");
			}
		}
	}
	dwmmc_print_status(sdmmc);
	sdmmc->rintsts = DWMMC_INT_DATA_TRANSFER_OVER;
	for_range(i, 0, 16) {
		info("SSR%"PRIu32": 0x%08"PRIx32"\n", i, *(u32*)0xfe320200);
	}
	dwmmc_print_status(sdmmc);
#else
#error No elfloader payload source specified
#endif
	async->buf = (u8 *)blob_addr;
	async->pos = 0;
#endif
#if CONFIG_ELFLOADER_SPI
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
#elif CONFIG_ELFLOADER_SD
	assert(async->total_bytes % 512 == 0);
	sdmmc->blksiz = 512;
	sdmmc->bytcnt = async->total_bytes;
	u32 pos = 0, sector = 64;
	st = dwmmc_wait_cmd_done(sdmmc, 18 | DWMMC_R1 | DWMMC_CMD_DATA_EXPECTED, sector, 1000);
	sector += 2;
	dwmmc_check_ok_status(sdmmc, st, "CMD17 (READ_SINGLE_BLOCK)");
	while (1) {
		u32 status = sdmmc->status, intstatus = sdmmc->rintsts;
		//dwmmc_print_status(sdmmc);
		assert((intstatus & DWMMC_ERROR_INT_MASK) == 0);
		u32 fifo_items = status >> 17 & 0x1fff;
		for_range(i, 0, fifo_items) {
			u32 val = *(volatile u32*)0xfe320200;
			spew("%3"PRIu32": 0x%08"PRIx32"\n", pos, val);
			*(u32 *)(async->buf + pos) = val;
			pos += 4;
		}
		async->pos = pos;
		u32 ack = 0;
		if (intstatus & DWMMC_INT_CMD_DONE) {
#ifdef SPEW_MSG
			dwmmc_print_status(sdmmc);
#endif
			ack |= DWMMC_INT_CMD_DONE;
		}
		if (intstatus & DWMMC_INT_DATA_TRANSFER_OVER) {
			ack |= DWMMC_INT_DATA_TRANSFER_OVER;
		}
		sdmmc->rintsts = ack;
		if (!fifo_items) {
			if (pos >= async->total_bytes) {break;}
			udelay(100);
			continue;
		}
	}
	info("SD transfer over at 0x%zx bytes\n", async->pos);
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
