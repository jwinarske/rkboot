/* SPDX-License-Identifier: CC0-1.0 */
#include <main.h>
#include <uart.h>
#include <rk3399.h>
#include <stage.h>

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

int32_t ENTRY NO_ASAN main() {
	setup_uart();
	setup_timer();
	u64 sctlr;
	__asm__ volatile("ic iallu;tlbi alle3;mrs %0, sctlr_el3" : "=r"(sctlr));
	debug("SCTLR_EL3: %016zx\n", sctlr);
	__asm__ volatile("msr sctlr_el3, %0" : : "r"(sctlr | SCTLR_I));
	stage_setup();
	setup_mmu();
	setup_pll(cru + CRU_LPLL_CON, 1200);
	ddrinit();
	set_sctlr_flush_dcache(sctlr);
	__asm__ volatile("ic iallu;tlbi alle3");
	puts("end\n");
	return 0;
}
