#include <main.h>
#include <uart.h>
#include <rk3399.h>

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

int32_t ENTRY NO_ASAN main() {
	setup_uart();
	setup_timer();
	u64 sctlr;
	__asm__ volatile("ic iallu;tlbi alle3;mrs %0, sctlr_el3" : "=r"(sctlr));
	debug("SCTLR_EL3: %016zx\n", sctlr);
	__asm__ volatile("msr sctlr_el3, %0" : : "r"(sctlr | SCTLR_I));
	setup_mmu();
	ddrinit();
	set_sctlr_flush_dcache(sctlr);
	__asm__ volatile("ic iallu;tlbi alle3");
	puts("end\n");
	return 0;
}
