/* SPDX-License-Identifier: CC0-1.0 */
#include <stage.h>
#include <uart.h>
#include <inttypes.h>
#include <dump_mem.h>
#include <mmu.h>
#include <exc_handler.h>

static struct stage_store store;

void xfer_handler(u64 *buf, size_t len, u16 cmd, struct exc_state_save *save) {
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
}

static const struct mapping initial_mappings[] = {
	{.first = 0, .last = 0xf7ffffff, .flags = MEM_TYPE_NORMAL},
	{.first = 0xf8000000, 0xff8bffff, .flags = MEM_TYPE_DEV_nGnRnE},
	{.first = 0xff8c0000, .last = 0xff8effff, .flags = MEM_TYPE_UNCACHED},
	{.first = 0xff8f0000, .last = 0xfffeffff, .flags = MEM_TYPE_DEV_nGnRnE},
	{.first = 0xffff0000, .last = 0xffffffff, .flags = MEM_TYPE_NORMAL},
	{.first = 0, .last = 0, .flags = 0}
};

static const struct address_range critical_ranges[] = {
	{.first = __start__, .last = __end__ - 1},
	{.first = uart, .last = uart},
	ADDRESS_RANGE_INVALID
};

void patch_brom();

void ENTRY main() {
	puts("brompatch\n");
	stage_setup(&store);
	mmu_setup(initial_mappings, critical_ranges);
	u32 crc;__asm__("crc32cx %w0, %w1, %2" : "=r"(crc) : "r"(~(u32)0), "r"(0x0706050403020100));
	printf("CRC32C: %08"PRIx32"\n", crc);
	patch_brom();
	puts("patch applied\n");
	/* do *not* tear down the stage */
}
