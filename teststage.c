#include <main.h>

__asm__(".section .entry, \"ax\", %progbits;adr x5, #0x10000;add sp, x5, #0;b main");

void main() {
	puts("test stage\n");
	halt_and_catch_fire();
}
