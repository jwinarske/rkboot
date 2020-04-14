#pragma once
#include <defs.h>
#include <aarch64.h>
#include <output.h>

extern u8 __bss_start__, __bss_end__, __exc_base__;

struct stage_store {
	u64 sctlr;
#ifdef CONFIG_EXC_VEC
	u64 vbar;
	u64 scr;
#endif
};

static inline void UNUSED stage_setup(struct stage_store *store) {
	u64 sctlr;
	__asm__ volatile("ic iallu;tlbi alle3;mrs %0, sctlr_el3" : "=r"(sctlr));
	debug("SCTLR_EL3: %016zx\n", sctlr);
	__asm__ volatile("msr sctlr_el3, %0" : : "r"(sctlr | SCTLR_I));
	store->sctlr = sctlr;
	u8 *bss = &__bss_start__, *bss_end_ptr =  &__bss_end__;
	do {*bss++ = 0;} while(bss < bss_end_ptr);
#ifdef CONFIG_EXC_VEC
	u64 vbar,  scr;
	__asm__("mrs %0, vbar_el3; mrs %1,  scr_el3" : "=r"(vbar),  "=r"(scr));
	debug("VBAR=%08zx,  SCR=%zx\n",  vbar,  scr);
	store->vbar = vbar;
	store->scr = scr;
	__asm__ volatile("msr scr_el3, %0" : : "r"((u64)SCR_EL3_RES1 | SCR_EA | SCR_FIQ | SCR_IRQ));
	__asm__ volatile("msr vbar_el3, %0;isb;msr DAIFclr, #0xf;isb" : : "r"(&__exc_base__));
#endif
}

void set_sctlr_flush_dcache(u64);

static inline void UNUSED stage_teardown(struct stage_store *store) {
	set_sctlr_flush_dcache(store->sctlr);
#ifdef CONFIG_EXC_VEC
	__asm__ volatile("msr scr_el3, %0" : : "r"(store->scr));
	__asm__ volatile("msr vbar_el3, %0" : : "r"(store->vbar));
	__asm__ volatile("isb;msr DAIFset, #0xf;isb");
#endif
	__asm__ volatile("ic iallu;tlbi alle3");
	puts("end\n");
}
