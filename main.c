/* SPDX-License-Identifier: CC0-1.0 */
#include <main.h>
#include <uart.h>
#include <rk3399.h>
#include <stage.h>
#include <rk-spi.h>
#include <compression.h>

const struct mapping initial_mappings[] = {
	{.first = 0, .last = 0xffffffff, .type = MEM_TYPE_DEV_nGnRnE},
	{.first = 0xff8c0000, .last = 0xff8effff, .type = MEM_TYPE_NORMAL},
	{.first = 0, .last = 0, .type = 0}
};

static void NO_ASAN setup_uart() {
	uart->line_control = UART_LCR_8_DATA_BITS | UART_LCR_DIVISOR_ACCESS;
	uart->divisor_low = CONFIG_UART_CLOCK_DIV;
	uart->line_control = UART_LCR_8_DATA_BITS;
	uart->shadow_fifo_enable = 1;
	grf[GRF_GPIO4C_IOMUX] = 0x03c00140;
	grf[GRF_SOC_CON7] = SET_BITS16(2, 2) << 10;
	const char *text = CONFIG_GREETING;
	for (char c; (c = *text) ; ++text) {uart->tx = *text;}
}

static void dump_pll(const char *name, volatile u32 *base) {
	u32 con3 = base[3], con1 = base[1], con2 = base[2];
	u32 refdiv = ubfx32(con1, 0, 6), fbdiv = base[0];
	u32 postdiv1 = ubfx32(con1, 8, 3), postdiv2 = ubfx32(con1, 12, 3);
	printf(
		"%s@%zx: fbdiv=%u refdiv=%u postdiv=%u,%u",
		name, (u64)base,
		fbdiv,
		refdiv,
		postdiv1, postdiv2
	);
	if (con3 & 8) {
		puts(" integer");
	} else {
		printf(" fracdiv=%u", con2);
	}
	u32 mode = ubfx32(con3, 8, 2);
	puts(mode == 0 ? " slow" : mode == 1 ? " normal" : " deep slow");
	if (con3 & 0x40) {puts(" 4phasepd");}
	if (con3 & 0x20) {puts(" vcopd");}
	if (con3 & 0x10) {puts(" postdivpd");}
	if (con3 & 4) {puts(" dacpd");}
	if (con3 & 2) {puts(" bypass");}
	if (con3 & 1) {puts(" globalpd");}
	printf(" ssmod=%x", base[4] << 16 | base[5]);
	if (mode == 0) {
		puts("→ 24 MHz\n");
	} else if (mode == 2) {
		puts("→ 32.768 KHz");
	} else if (con3 & 8) {
		printf("→ %u MHz\n", 24 * fbdiv / refdiv / postdiv1 / postdiv2);
	} else {
		printf("→ %u MHz\n", 24 * (fbdiv * 224 + con2) / 224 / refdiv / postdiv1 / postdiv2);
	}
}

static void UNUSED dump_clocks() {
	static const struct {
		const char *name;
		volatile u32 *addr;
	} plls[] = {
		{"PPLL", pmucru + 0},
		{"LPLL", cru + CRU_LPLL_CON},
		{"BPLL", cru + CRU_BPLL_CON},
		{"DPLL", cru + (0x40/4)},
		{"CPLL", cru + (0x60/4)},
		{"GPLL", cru + (0x80/4)},
		{"NPLL", cru + (0xa0/4)},
		{"VPLL", cru + (0xc0/4)},
	};
	for_array(i, plls) {
		dump_pll(plls[i].name, plls[i].addr);
	}
	for_range(i, 0, 4) {
		printf("clksel%u: %x\n", i, cru[0x100/4 + i]);
	}
}

#ifdef CONFIG_EMBED_ELFLOADER
extern const u8 _binary_elfloader_bin_start[], _binary_elfloader_bin_end;

__asm__("jump: add sp, x5, #0; br x4");
_Noreturn void jump(u64 x0, u64 x1, u64 x2, u64 x3, void *entry, void *stack);
#endif

int32_t ENTRY NO_ASAN main() {
	setup_uart();
	setup_timer();
	struct stage_store store;
	stage_setup(&store);
	setup_mmu();
	setup_pll(cru + CRU_LPLL_CON, 1200);
	ddrinit();
	cru[CRU_CLKSEL_CON+59] = SET_BITS16(1, 1) << 15 | SET_BITS16(7, 3) << 8;
	const u32 spi_mode_base = SPI_MASTER | SPI_CSM_KEEP_LOW | SPI_SSD_FULL_CYCLE | SPI_LITTLE_ENDIAN | SPI_MSB_FIRST | SPI_POLARITY(1) | SPI_PHASE(1) | SPI_DFS_8BIT;
	assert((spi_mode_base | SPI_BHT_APB_8BIT) == 0x24c1);
	spi->ctrl0 = spi_mode_base | SPI_BHT_APB_8BIT;
	spi->enable = 1; mmio_barrier();
	spi->slave_enable = 1; mmio_barrier();
	spi->tx = 0x9f;
	for_range(i, 0, 3) {spi->tx = 0xff;}
	u32 val = 0;
	for_range(i, 0, 4) {
		while (!spi->rx_fifo_level) {__asm__ volatile("yield");}
		val = val << 8 | spi->rx;
	}
	spi->slave_enable = 0; mmio_barrier();
	spi->enable = 0;
	printf("SPI read %x\n", val);
	assert(val != ~(u32)0);
#ifdef CONFIG_EMBED_ELFLOADER
	void *loadaddr = (void *)0x100000;
	lzcommon_literal_copy((u8 *)loadaddr, _binary_elfloader_bin_start, &_binary_elfloader_bin_end - _binary_elfloader_bin_start);
	stage_teardown(&store);
	jump(0, 0, 0, 0, loadaddr, (void *)0x1000);
#else
	stage_teardown(&store);
	return 0;
#endif
}
