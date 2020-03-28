#include <main.h>

_Bool test_mirror(u32 addr, u32 bit) {
	volatile u32 *base = (volatile u32*)(uintptr_t)addr, *mirror = (volatile u32*)(uintptr_t)(addr ^ (1 << bit));
	static const u32 pattern0 = 0, pattern1 = 0xf00f55aa;
	debug("test_mirror(0x%08x,%u)\n", addr, bit);
	*base = pattern0;
	*mirror = pattern1;
	__asm__ volatile("dsb sy" : : : "memory", "cc");
	u32 a = *base;
	u32 b = *mirror;
	if (b == pattern1) {
		if (a == pattern0) {
			return 0;
		} else if (a == pattern1) {
			return 1;
		}
	}
	die("\nmirror test (0x%08x,%u) corrupted: wrote %08x, %08x, read %08x, %08x\n", addr, bit, pattern0, pattern1, a, b);
}
