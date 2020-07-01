/* SPDX-License-Identifier: CC0-1.0 */
#include <stage.h>
#include <mmu.h>
#include <inttypes.h>
#include <die.h>
#include <exc_handler.h>
#include <dump_mem.h>

static _Alignas(4096) u32 brom_patch_page[1024];

void xfer_handler(u64 *buf, size_t len, u16 cmd, struct exc_state_save *save);

void exc_handler(struct exc_state_save *save) {
	u64 elr, esr;
	__asm__("mrs %0, esr_el3;mrs %1, elr_el3" : "=r"(esr), "=r"(elr));
	info("ELR_EL3=0x%08"PRIx64" ESR_EL3=0x%08"PRIx64"\n", elr, esr);
	if (esr == 0x5e << 24) {
		if (elr == 0xffff3b64) {
			debug("X0=0x%"PRIx64" X1=0x%"PRIx64"\n", save->locals[0], save->locals[1]);
		} else if (elr == 0xffff3bd0) {
			debug("X0=0x%"PRIx64" X1=0x%"PRIx64" X2=0x%"PRIx64"\n", save->locals[0], save->locals[1], save->locals[2]);
			u16 UNUSED cmd = save->locals[0];
			assert(save->locals[1] % 8 == 0);
			u64 *buf = (u64 *)save->locals[1];
			assert(save->locals[2] % 4096 == 4);
			size_t len = save->locals[2] / 8;
			u32 crc32c = *(u32*)(buf + len);
#ifdef DEBUG_MSG
			debug("end: 0x%"PRIx64"\n", (u64)(buf + len));
			dump_mem(buf + len - 15, 128);
#endif
			assert_eq(crc32c, ~compute_crc32c(buf, buf + len, ~(u32)0), u32, "%08"PRIx32);
			xfer_handler(buf, len, cmd, save);
			return;
		}
	} else {
	}
	dump_mem((void*)elr, 64);
	die("unhandled\n");
}

void patch_brom() {
	sync_exc_handler_spx = exc_handler;
	for_array(i, brom_patch_page) {
		brom_patch_page[i] = *(u32 *)((u64)0xffff3000 + 4*i);
	}
	brom_patch_page[0xb60 / 4] = 0xd4000003; /* SMC */
	brom_patch_page[0xb64 / 4] = 0xd65f03c0; /* RET */
	brom_patch_page[0xbcc / 4] = 0xd4000003; /* SMC */
	brom_patch_page[0xbd0 / 4] = 0xd65f03c0; /* RET */
	mmu_unmap_range(0xffff3000, 0xffff3fff);
	__asm__("dsb ish;isb");
	flush_dcache();
	mmu_map_range(0xffff3000, 0xffff3fff, (u64)&brom_patch_page, MEM_TYPE_NORMAL);
	__asm__ volatile("dc civac, %0;dsb sy;isb" : : "r"((u64)0xffff3b60));
	/*assert_eq(*(u32*)(u64)0xffff3b60, brom_patch_page[0xb60 / 4], u32, "%"PRIx32);*/
	__asm__("ic iallu;tlbi alle3;isb");
	/*void (*test)(u64, u64) = 0xffff3b60;
	test(1, 2);*/
}
