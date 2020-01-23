#include <main.h>
#include <uart.h>
#include <rk3399.h>

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

enum {SCTLR_I = 0x1000};

_Noreturn void ENTRY NO_ASAN main() {
	setup_uart();
	setup_timer();
	puts("test\n");
	u64 sctlr;
	__asm__ volatile("mrs %0, sctlr_el3" : "=r"(sctlr));
	printf("SCTLR_EL3: %016zx\n", sctlr);
	__asm__ volatile("msr sctlr_el3, %0" : : "r"(sctlr | SCTLR_I));
	ddrinit();
	die("end\n");
}

void yield() {
	__asm__ volatile("yield");
}

uintptr_t __stack_chk_guard = 0x595e9fbd94fda766;

_Noreturn void NO_ASAN __stack_chk_fail() {
	const char *text = "STACK CORRUPTION\r\n";
	for (char c; (c = *text) ; ++text) {
		while (uart->tx_level) {__asm__ volatile("yield");}
		uart->tx = *text;
	}
	halt_and_catch_fire();
}

_Noreturn void halt_and_catch_fire() {
	while (1) {
		__asm__ volatile("yield");
	}
}
