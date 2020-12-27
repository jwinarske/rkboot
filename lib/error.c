/* SPDX-License-Identifier: CC0-1.0 */
#include <stdlib.h>
#include <die.h>
#include <plat.h>
#include <uart.h>

FORCE_USED uintptr_t __stack_chk_guard = 0x595e9fbd94fda766;

_Noreturn void __stack_chk_fail();
FORCE_USED  _Noreturn void NO_ASAN __stack_chk_fail() {
	const char *text = "STACK CORRUPTION\r\n";
	for (char c; (c = *text) ; ++text) {
		while (uart->tx_level) {__asm__ volatile("yield");}
		uart->tx = *text;
	}
	halt_and_catch_fire();
}

_Noreturn void plat_panic() {halt_and_catch_fire();}

_Noreturn void halt_and_catch_fire() {
	while (1) {
		__asm__ volatile("wfi");
	}
}

_Noreturn void abort() {die("abort() called\n");}
