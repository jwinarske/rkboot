/* SPDX-License-Identifier: CC0-1.0 */
#include <inttypes.h>

#include <log.h>
#include <timer.h>
#include <die.h>
#include <aarch64.h>
#include <rkpll.h>
#include <rk3399.h>

void pmu_cru_setup() {
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
		0x2f40, 0xeaf3, 0, 0,

		0x505, 0x505, 0, 0,
		0xce4, 0x24f, 0xd7eb, 0x3700,
		0x60, 0x60, 0, 0x1f0,
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
	debugs("PLLs configured\n");
	const timestamp_t pll_start = get_timestamp();
	for_array(i, plls) {
		while (!rkpll_switch(plls[i].pll)) {
			if (get_timestamp() - pll_start > 1000 * TICKS_PER_MICROSECOND) {
				die("failed to lock-on %s\n", plls[i].name);
			}
			__asm__("yield");
		}
	}
	debugs("PLLs enabled\n");
}
