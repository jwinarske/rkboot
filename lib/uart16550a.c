/* SPDX-License-Identifier: CC0-1.0 */
#include <uart.h>
#include <stdio.h>
#include <arch.h>
#include <irq.h>

HEADER_FUNC uint32_t mmio_r32(volatile uint32_t *reg) {return *reg;}
HEADER_FUNC void mmio_w32(volatile uint32_t *reg, uint32_t val) {*reg = val;}

struct uart *const console_uart = (struct uart *)CONFIG_CONSOLE_UART_ADDR;
//static irq_lock_t console_lock = IRQ_LOCK_INIT;

void plat_write_console(const char *str, size_t len) {
	irq_save_t irq = irq_save_mask();//irq_lock(&console_lock);
	static const size_t depth = CONFIG_CONSOLE_FIFO_DEPTH;
	while (len) {
		size_t this_round = len <= depth ? len : depth;
		while (mmio_r32(&console_uart->tx_level) > depth - this_round) {
			arch_relax_cpu();
		}
		len -= this_round;
		while (this_round--) {
			mmio_w32(&console_uart->tx, *str++);
		}
	}
	irq_restore(irq);
	//irq_unlock(&console_lock, irq);
}

int fflush(FILE UNUSED *f) {
	irq_save_t irq = irq_save_mask();//irq_lock(&console_lock);
	while (mmio_r32(&console_uart->tx_level)) {
		arch_relax_cpu();
	}
	irq_restore(irq);
	//irq_unlock(&console_lock, irq);
	return 0;
}

