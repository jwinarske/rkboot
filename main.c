/* SPDX-License-Identifier: CC0-1.0 */
#include <main.h>
#include <uart.h>
#include <rk3399.h>
#include <stage.h>
#include <compression.h>

static const struct mapping initial_mappings[] = {
	{.first = 0, .last = 0xff8bffff, .type = MEM_TYPE_DEV_nGnRnE},
	{.first = 0xff8c0000, .last = 0xff8effff, .type = MEM_TYPE_NORMAL},
	{.first = 0xff8f0000, .last = 0xffffffff, .type = MEM_TYPE_DEV_nGnRnE},
	{.first = 0, .last = 0, .type = 0}
};

static const struct address_range critical_ranges[] = {
	{.first = __start__, .last = __end__},
	{.first = uart, .last = uart},
	ADDRESS_RANGE_INVALID
};

static void NO_ASAN setup_uart() {
	uart->line_control = UART_LCR_8_DATA_BITS | UART_LCR_DIVISOR_ACCESS;
	uart->divisor_low = CONFIG_UART_CLOCK_DIV;
	uart->line_control = UART_LCR_8_DATA_BITS;
	uart->shadow_fifo_enable = 1;
	grf[GRF_GPIO4C_IOMUX] = 0x03c00140;
	grf[GRF_SOC_CON7] = SET_BITS16(2, 2) << 10;
	const char *text = CONFIG_GREETING;
	for (char c; (c = *text) ; ++text) {uart->tx = *text;}
}

#ifdef CONFIG_EMBED_ELFLOADER
extern const u8 _binary_elfloader_lz4_start[], _binary_elfloader_lz4_end[];

extern const struct decompressor lz4_decompressor;

__asm__("jump: add sp, x5, #0; br x4");
_Noreturn void jump(u64 x0, u64 x1, u64 x2, u64 x3, void *entry, void *stack);
#endif

int32_t ENTRY NO_ASAN main() {
	setup_uart();
	setup_timer();
	struct stage_store store;
	stage_setup(&store);
	mmu_setup(initial_mappings, critical_ranges);
	setup_pll(cru + CRU_LPLL_CON, 1200);
	ddrinit();
	cru[CRU_CLKSEL_CON+59] = SET_BITS16(1, 1) << 15 | SET_BITS16(7, 3) << 8;
#ifdef CONFIG_EMBED_ELFLOADER
	void *loadaddr = (void *)0x4000000;
	mmu_unmap_range(0, 0xf7ffffff);
	mmu_map_range(0, 0xf7ffffff, 0, MEM_TYPE_NORMAL);
	__asm__ volatile("dsb sy");
	size_t size;
	assert_msg(lz4_decompressor.probe(_binary_elfloader_lz4_start, _binary_elfloader_lz4_end, &size) == COMPR_PROBE_SIZE_KNOWN, "could not probe elfloader compression frame\n");
	struct decompressor_state *state = (struct decompressor_state *)(loadaddr + (size + LZCOMMON_BLOCK + sizeof(max_align_t) - 1)/sizeof(max_align_t)*sizeof(max_align_t));
	const u8 *ptr = lz4_decompressor.init(state, _binary_elfloader_lz4_start, _binary_elfloader_lz4_end);
	assert(ptr);
	state->window_start = state->out = loadaddr;
	state->out_end = loadaddr + size;
	while (state->decode) {
		size_t res = state->decode(state, ptr, _binary_elfloader_lz4_end);
		assert(res >= NUM_DECODE_STATUS);
		ptr += res - NUM_DECODE_STATUS;
	}
	stage_teardown(&store);
	jump(0, 0, 0, 0, loadaddr, (void *)0x1000);
#else
	stage_teardown(&store);
	return 0;
#endif
}
