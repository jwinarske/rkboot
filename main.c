/* SPDX-License-Identifier: CC0-1.0 */
#include <main.h>
#include <uart.h>
#include <rk3399.h>
#include <stage.h>
#include <compression.h>
#include <inttypes.h>
#include <exc_handler.h>
#include <rkpll.h>
#include <rksaradc_regs.h>

static const struct mapping initial_mappings[] = {
	MAPPING_BINARY_SRAM,
	{.first = (u64)uart, .last = (u64)uart + 0xfff, .flags = MEM_TYPE_DEV_nGnRnE},
	{.first = 0xff8c0000, .last = 0xff8c1fff, .flags = MEM_TYPE_NORMAL},
	{.first = 0, .last = 0, .flags = 0}
};

static const struct address_range critical_ranges[] = {
	{.first = __start__, .last = __end__ - 1},
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

#if CONFIG_EXC_STACK
void sync_exc_handler() {
	u64 esr, far;
	__asm__("mrs %0, esr_el3; mrs %1, far_el3" : "=r"(esr), "=r"(far));
	die("sync exc: ESR_EL3=0x%"PRIx64", FAR_EL3=0x%"PRIx64"\n", esr, far);
}
#endif

int32_t ENTRY NO_ASAN main() {
	setup_uart();
	setup_timer();
	cru[CRU_CLKSEL_CON+26] = SET_BITS16(8, 2) << 8;
	saradc->control = 0;
	dsb_st();
	saradc->control = 1 | RKSARADC_POWER_UP | RKSARADC_INT_ENABLE;
	mmio_barrier();
	while (~saradc->control & RKSARADC_INTERRUPT) {__asm__("yield");}
	mmio_barrier();
	u32 recovery_value = saradc->data;
	mmio_barrier();
	saradc->control = 0;
	if (recovery_value <= 255) {
		while (uart->tx_level) {__asm__("yield");}
		for (const char *text = "recovery\r\n"; *text; ++text) {uart->tx = *text;}
		__asm__("add sp, %0, #0x2000; br %1" : : "r"((u64)0xff8c0000), "r"((u64)0xffff0100));
	}
	struct stage_store store;
	stage_setup(&store);
#ifdef CONFIG_EXC_STACK
	sync_exc_handler_spx = sync_exc_handler;
#endif
	mmu_setup(initial_mappings, critical_ranges);
	/* map {PMU,}CRU, GRF */
	mmu_map_mmio_identity(0xff750000, 0xff77ffff);
	/* map PMU{,SGRF,GRF} */
	mmu_map_mmio_identity(0xff310000, 0xff33ffff);
	__asm__("dsb ishst");

	const u32 pd_busses = 0x20c00e79, pd_domains = 0x93cf833e;
	pmu[PMU_BUS_IDLE_REQ] = pd_busses;
	while (pmu[PMU_BUS_IDLE_ACK] != pd_busses) {__asm__("yield");}
	debugs("bus idle ack\n");
	while (pmu[PMU_BUS_IDLE_ST] != pd_busses) {__asm__("yield");}
	debug("PMU_BUS_IDLE_ACK: %"PRIx32" _ST: %"PRIx32"\n", pmu[PMU_BUS_IDLE_ACK], pmu[PMU_BUS_IDLE_ST]);
	pmu[PMU_PWRDN_CON] = pd_domains;
	while(pmu[PMU_PWRDN_ST] != pd_domains) {__asm__("yield");}
	debugs("domains powered down\n");

	pmucru[PMUCRU_CLKGATE_CON+0] = 0x0b630b63;
	pmucru[PMUCRU_CLKGATE_CON+1] = 0x0a800a80;
	static const u16 clk_gates[] = {
		0, 0xff, 0, 0xb,
		0x7ff, 0x3e0, 0x7f0f, 0x2e0,
		0xfff8, 0xf8cf, 0xffff, 0xcdfa,
		0x2f40, 0xeaf3, 0x6, 0,
		0x505, 0x505, 0, 0,
		0xce4, 0x24f, 0xd7eb, 0x3700,
		0x8060, 0x60, 0, 0x1f0,
		0xcc, 0xfc6, 0x500, 0,
		0x215, 0, 0x3f
	};
	for_array(i, clk_gates) {
		u32 gates = clk_gates[i];
		cru[CRU_CLKGATE_CON+i] = gates << 16 | gates;
	}

	/* aclk_cci = GPLL = 594 MHz, DTS has 600 */
	cru[CRU_CLKSEL_CON + 5] = SET_BITS16(2, 1) << 6 | SET_BITS16(5, 0);
	/* aclk_perihp = GPLL/4 = 148.5 MHz (DTS has 150), hclk_perihp = aclk_perihp / 2, pclk_perihp = aclk_perihp / 4 */
	cru[CRU_CLKSEL_CON + 14] = SET_BITS16(3, 3) << 12 | SET_BITS16(2, 1) << 8 | SET_BITS16(1, 1) << 7 | SET_BITS16(5, 7);
	/* aclk_perilp0 = hclk_perilp0 = CPLL/8 = 100 MHz, pclk_perilp0 = 50 MHz */
	cru[CRU_CLKSEL_CON + 23] = SET_BITS16(1, 0) << 7 | SET_BITS16(5, 7) | SET_BITS16(2, 0) << 8 | SET_BITS16(3, 1) << 12;
	/* hclk_perilp1 = CPLL/8 = 100 MHz pclk_perilp1 = hclk_perilp1/2 = 50 MHz */
	cru[CRU_CLKSEL_CON + 25] = SET_BITS16(1, 0) << 7 | SET_BITS16(5, 7) | SET_BITS16(3, 1) << 8;
	/* aclk_gic = CPLL/4 = 200 MHz */
	cru[CRU_CLKSEL_CON + 56] = SET_BITS16(1, 0) << 15 | SET_BITS16(5, 3) << 8;
	dsb_st();

	static const struct {volatile u32 *pll; u32 mhz;char name[8];} plls[] = {
		{cru + CRU_LPLL_CON, 1200, "LPLL"},
		{cru + CRU_CPLL_CON, 800, "CPLL"},
		{cru + CRU_GPLL_CON, 594, "GPLL"},
		{cru + CRU_BPLL_CON, 600, "BPLL"},
		{pmucru + PMUCRU_PPLL_CON, 676, "PPLL"},
		{cru + CRU_DPLL_CON, 50, "DPLL"},
	};
	for_array(i, plls) {
		rkpll_configure(plls[i].pll, plls[i].mhz);
	}
	const timestamp_t pll_start = get_timestamp();
	for_array(i, plls) {
		while (!rkpll_switch(plls[i].pll)) {
			if (get_timestamp() - pll_start > 1000 * TICKS_PER_MICROSECOND) {
				die("failed to lock-on %s\n", plls[i].name);
			}
			__asm__("yield");
		}
	}

	ddrinit();
#ifdef CONFIG_EMBED_ELFLOADER
	void *loadaddr = (void *)0x4000000;
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
