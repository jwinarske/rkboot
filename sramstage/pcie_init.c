/* SPDX-License-Identifier: CC0-1.0 */
#include <rk3399/sramstage.h>
#include <stdatomic.h>
#include <inttypes.h>

#include <rk3399.h>
#include <runqueue.h>
#include <timer.h>
#include <log.h>
#include <aarch64.h>

enum {
	RKPCIEPHY_PROBE_CLOCK_TEST = 0x10,
	RKPCIEPHY_PROBE_CLK_EN = 0x12,
};
enum {
	/* CLOCK_TEST */
	RKPCIEPHY_pll_test_clk = 8,
	RKPCIEPHY_CLK_PI_FB = 4,
	RKPCIEPHY_TXPLL_LOCK = 2,
	RKPCIEPHY_cdr_test_clk = 1,
};
enum {
	RKPCIEPHY_CFG_CLKSEL = 0x10,
	RKPCIEPHY_CFG_CLK_EN = 0x12,
};
enum {
	/* CLKSEL */
	RKPCIEPHY_separate_rate = 8,
	RKPCIEPHY_INVERT_PLL_CLK = 4,
	RKPCIEPHY_TEST_CLK_SEL = 3,	/* mask */
	/* CLK_EN, also obvervable */
	RKPCIEPHY_SEL_PLL_100M = 8,
	RKPCIEPHY_GATE_100M = 4,
	/* bits 0, 1 reserved */
};
#define SELECT(reg) (SET_BITS16(6,(reg)) << 1)
#define OBSERVE(val) ((val) >> 8 & 15)

static volatile u32 *const phyctrl = grf + GRF_SOC_CON0+8, *const physts = grf + GRF_SOC_STATUS+1;

static void write_reg(u32 reg, u32 val) {
	*phyctrl = SELECT(reg) | SET_BITS16(4, val) << 7;
	dsb_st();
	delay(NSECS(10));
	*phyctrl = SET_BITS16(1, 1);
	dsb_st();
	delay(NSECS(20));
	*phyctrl = SET_BITS16(1, 0);
	dsb_st();
	delay(NSECS(10));
}

static _Bool wait_pll_lock() {
	*phyctrl = SELECT(RKPCIEPHY_PROBE_CLOCK_TEST);
	timestamp_t start = get_timestamp();
	u32 sts;
	while (~OBSERVE(sts = *physts) & RKPCIEPHY_TXPLL_LOCK) {
		if (get_timestamp() - start > MSECS(20)) {
			infos("PCIe TX PLL lock timeout\n");
			return 0;
		}
		debug("not locked: %"PRIx32"\n", sts);
		sched_yield();
	}
	info("PCIe PHY PLL locked: %"PRIx32"\n", sts);
	return 1;
}

void pcie_init() {
	infos("initializing PCIe\n");
	cru[CRU_SOFTRST_CON+8] = SET_BITS16(8, 0xff);
	cru[CRU_CLKGATE_CON+12] = SET_BITS16(1, 0) << 6;	/* clk_pciephy_ref100m */
	cru[CRU_CLKGATE_CON+20] = SET_BITS16(2, 0) << 10	/* {a,p}clk_pcie */
		| SET_BITS16(1, 0) << 2;	/* aclk_perf_pcie */
	cru[CRU_CLKGATE_CON+6] = SET_BITS16(2, 0) << 2;	/* clk_pcie_{core,pm} */
	usleep(10);
	cru[CRU_SOFTRST_CON+8] = SET_BITS16(1, 0) << 7;	/* resetn_pciephy */
	dsb_st();
	grf[GRF_SOC_CON0+5] = SET_BITS16(4, 0) << 3;	/* enable all lanes */
	if (!wait_pll_lock()) {goto shut_down_phy;}
	write_reg(RKPCIEPHY_CFG_CLKSEL, RKPCIEPHY_separate_rate);
	write_reg(RKPCIEPHY_CFG_CLK_EN, RKPCIEPHY_SEL_PLL_100M);
	*phyctrl = SELECT(RKPCIEPHY_PROBE_CLK_EN);
	timestamp_t start = get_timestamp();
	u32 sts;
	while (OBSERVE(sts = *physts) & RKPCIEPHY_GATE_100M) {
		if (get_timestamp() - start > MSECS(20)) {
			infos("PCIe PHY clock output timeout\n");
			goto shut_down_phy;
		}
		info("waiting for clock output enable: %"PRIx32"\n", sts);
		sched_yield();
	}
	info("PCIe PHY clock active: %"PRIx32"\n", sts);
	if (!wait_pll_lock()) {goto shut_down_phy;}

	cru[CRU_SOFTRST_CON+8] = SET_BITS16(8, 0);
	rk3399_set_init_flags(RK3399_INIT_PCIE);
	return;
shut_down_phy:
	grf[GRF_SOC_CON0+5] = SET_BITS16(4, 15) << 3;	/* disable all lanes */
	dsb_st();
	cru[CRU_CLKGATE_CON+12] = SET_BITS16(1, 1) << 6;	/* clk_pciephy_ref100m */
	cru[CRU_CLKGATE_CON+20] = SET_BITS16(2, 3) << 10	/* {a,p}clk_pcie */
		| SET_BITS16(1, 1) << 2;	/* aclk_perf_pcie */
	rk3399_set_init_flags(RK3399_INIT_PCIE);
}
