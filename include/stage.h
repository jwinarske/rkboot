/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>
#include <aarch64.h>
#include <lib.h>

extern u8 __bss_start__, __bss_end__, __exc_base__;
extern u8 __start__, __ro_end__, __data_end__;
static inline u32 compute_crc32c(u64 *start, u64 *end, u32 seed) {
    while (start < end) {
	__asm__("crc32cx %w0, %w0, %1" : "+r"(seed) : "r"(*start++));
    }
    return seed;
}

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
#ifdef CONFIG_CRC
	u64 *crc_start = (u64*)&__start__, *crc_mid = (u64*)(((u64)&__ro_end__ + 0xfff) & ~(u64)0xfff), *crc_end = (u64*)(((u64)&__data_end__ + 0xfff) & ~(u64)0xfff);
	u32 crc_ro =  compute_crc32c(crc_start, crc_mid, ~(u32)0);
	printf("CRC32C(%08zx, %08zx): %08x\n", (u64)crc_start, (u64)crc_mid, crc_ro);
	u32 crc_all = compute_crc32c(crc_mid, crc_end, crc_ro);
	printf("CRC32C(%08zx, %08zx): %08x\n", (u64)crc_start, (u64)crc_end, crc_all);
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
	u64 sctlr = store->sctlr;
	set_sctlr_flush_dcache(sctlr | SCTLR_I);
	logs("end\n");
	__asm__ volatile("msr sctlr_el3, %0;isb;ic iallu;tlbi alle3" : : "r"(sctlr));
}
