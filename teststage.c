/* SPDX-License-Identifier: CC0-1.0 */
#include <main.h>
#include <fdt.h>

__asm__(".section .text.entry, \"ax\", %progbits;adr x5, #0x10000;add sp, x5, #0;b main");

void dump_fdt(const struct fdt_header *);

_Noreturn void main(u64 x0) {
	puts("test stage\n");
	u64 sctlr;
	__asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));
	debug("SCTLR_EL2: %016zx\n", sctlr);
	__asm__ volatile("msr sctlr_el2, %0" : : "r"(sctlr | SCTLR_I));
	const struct fdt_header *fdt = (const struct fdt_header*)x0;
	dump_fdt(fdt);
	halt_and_catch_fire();
}
