/* SPDX-License-Identifier: CC0-1.0 */
#include <main.h>
#include <uart.h>

FORCE_USED uintptr_t __stack_chk_guard = 0x595e9fbd94fda766;

FORCE_USED  _Noreturn void NO_ASAN __stack_chk_fail() {
	const char *text = "STACK CORRUPTION\r\n";
	for (char c; (c = *text) ; ++text) {
		while (uart->tx_level) {__asm__ volatile("yield");}
		uart->tx = *text;
	}
	halt_and_catch_fire();
}

_Noreturn void halt_and_catch_fire() {
	while (1) {
		__asm__ volatile("wfi");
	}
}

_Noreturn void abort() {die("abort() called\n");}
