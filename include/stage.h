#pragma once
#include <defs.h>
#include <aarch64.h>

extern u8 __bss_start__, __bss_end__, __exc_base__;

static inline void UNUSED stage_setup() {
	u8 *bss = &__bss_start__, *bss_end_ptr =  &__bss_end__;
	do {*bss++ = 0;} while(bss < bss_end_ptr);
#ifdef CONFIG_EXC_VEC
	__asm__ volatile("msr scr_el3, %0" : : "r"((u64)SCR_EL3_RES1 | SCR_EA | SCR_FIQ | SCR_IRQ));
	__asm__ volatile("msr vbar_el3, %0;isb;msr DAIFclr, #0xf;isb" : : "r"(&__exc_base__));
#endif
}
