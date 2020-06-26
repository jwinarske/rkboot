/* SPDX-License-Identifier: CC0-1.0 */
#include <main.h>
#include <stage.h>
#include <mmu.h>
#include <uart.h>
#include <inttypes.h>
#include <die.h>
#include <exc_handler.h>

static const struct mapping initial_mappings[] = {
	{.first = 0, .last = 0xf7ffffff, .type = MEM_TYPE_NORMAL},
	{.first = 0xf8000000, 0xff8bffff, .type = MEM_TYPE_DEV_nGnRnE},
	{.first = 0xff8c0000, .last = 0xff8effff, .type = MEM_TYPE_UNCACHED},
	{.first = 0xff8f0000, .last = 0xfffeffff, .type = MEM_TYPE_DEV_nGnRnE},
	{.first = 0xffff0000, .last = 0xffffffff, .type = MEM_TYPE_NORMAL},
	{.first = 0, .last = 0, .type = 0}
};

static const struct address_range critical_ranges[] = {
	{.first = __start__, .last = __end__},
	{.first = uart, .last = uart},
	ADDRESS_RANGE_INVALID
};

_Alignas(4096) u32 brom_patch_page[1024];

uint16_t crc16(uint8_t *buf, size_t len, uint16_t crc) {
	const uint16_t poly = 0x1021;
	for (size_t p = 0; p < len; ++p) {
		uint8_t bit = 0x80, byte = buf[p];
		do {
			crc = (crc << 1) ^ (((crc & 0x8000) != 0) ? poly : 0);
			crc ^= (bit & byte) != 0 ? poly : 0;
		} while(bit >>= 1);
	}
	return crc;
}

static void UNUSED dump_mem(void *mem, size_t size) {
	for_range(i, 0, size / 8) {
		const u8 *start = (const u8 *)(mem + 8*i);
		printf("%08"PRIx64":", (u64)start);
		for_range(j, 0, 8) {
			printf(" %02"PRIx8, start[j]);
		}
		puts("  ");
		for_range(j, 0, 8) {
			printf("%c", start[j] < 0x7f && start[j] >= 32 ? start[j] : '.');
		}
		puts("\n");
	}
}

struct stage_store store;

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
			u64 load_addr = buf[0], end_addr = buf[1];
			assert(end_addr >= load_addr && end_addr - load_addr <= len * 8 - 16);
			assert(load_addr % 8 == 0);
			u64 *dest = (u64 *)load_addr, *end = (u64 *)((end_addr + 7) & ~(u64)7);
			printf("loading to 0x%08"PRIx64"\n", load_addr);
			buf += 2;
			while (dest < end) {*dest++ = *buf++;}
			if (cmd == 0x0472) {
				for_array(i, save->locals) {save->locals[i] = 0;}
				__asm__ volatile("msr elr_el3, %0;msr SPSel, #1;add sp, %1, #0;msr SPSel, #0" : : "r"(load_addr), "r"(exc_stack + sizeof(exc_stack)));
				dump_mem((void*)load_addr, 64);
				stage_teardown(&store);
				puts("jumping\n");
			}
			return;
		}
	} else {
	}
	dump_mem((void*)elr, 64);
	die("unhandled\n");
}

void ENTRY main() {
	puts("brompatch\n");
	stage_setup(&store);
	sync_exc_handler_spx = exc_handler;
	mmu_setup(initial_mappings, critical_ranges);
	u32 crc;__asm__("crc32cx %w0, %w1, %2" : "=r"(crc) : "r"(~(u32)0), "r"(0x0706050403020100));
	printf("CRC32C: %08"PRIx32"\n", crc);
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
	void (*test)(u64, u64) = 0xffff3b60;
	test(1, 2);
	puts("patch applied\n");
	/* do *not* tear down the stage */
}
