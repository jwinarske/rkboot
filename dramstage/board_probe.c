#include <rk3399/dramstage.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>

#include <log.h>
#include <runqueue.h>

#include <mmu.h>

#include <rki2c_regs.h>
#include <rki2c.h>

#include <rk3399.h>

_Atomic(u32) rk3399_detected_board = BOARD_UNKNOWN;
struct sched_runnable_list rk3399_board_detection_waiters = {};

void rk3399_probe_board() {
	/* clk_i2c4 = PPLL/4 = 169 MHz, DTS has 200 */
	regmap_pmucru[PMUCRU_CLKSEL_CON + 3] = SET_BITS16(7, 0);

	volatile u32 *const pmugrf = regmap_pmugrf;
	volatile struct rki2c_regs *const i2c4 = regmap_i2c4;
	debug("RKI2C4_CON: %"PRIx32"\n", i2c4->control);
	struct rki2c_config i2c_cfg = rki2c_calc_config_v1(169, 1000000, 600, 20);
	debug("setup %"PRIx32" %"PRIx32"\n", i2c_cfg.control, i2c_cfg.clkdiv);
	i2c4->clkdiv = i2c_cfg.clkdiv;
	i2c4->control = i2c_cfg.control;
	pmugrf[PMUGRF_GPIO1B_IOMUX] = SET_BITS16(2, 1) << 6 | SET_BITS16(2, 1) << 8;
	i2c4->control = i2c_cfg.control | RKI2C_CON_ENABLE | RKI2C_CON_START | RKI2C_CON_MODE_REGISTER_READ | RKI2C_CON_ACK;
	i2c4->slave_addr = 1 << 24 | 0x62 << 1;
	i2c4->reg_addr = 1 << 24 | 0;
	i2c4->rx_count = 1;
	u32 val;
	while (!((val = i2c4->int_pending) & RKI2C_INTMASK_XACT_END)) {}
	debug("RKI2C4_CON: %"PRIx32", _IPD: %"PRIx32"\n", i2c4->control, i2c4->int_pending);
	_Bool is_pbp = !(val & 1 << RKI2C_INT_NAK);
	debug("%"PRIx32"\n", i2c4->rx_data[0]);
	i2c4->control = i2c_cfg.control | RKI2C_CON_ENABLE | RKI2C_CON_STOP;

	if (is_pbp) {
		mmu_map_mmio_identity(0xff420000, 0xff420fff);
		mmu_flush();
		info("ACK from i2c4-62, this seems to be a Pinebook Pro\n");
		*(volatile u32*)0xff420024 = 0x96e;
		*(volatile u32*)0xff420028 = 0x25c;
		*(volatile u32*)0xff42002c = 0x13;
		pmugrf[PMUGRF_GPIO1C_IOMUX] = SET_BITS16(2, 1) << 6;
		atomic_store_explicit(&rk3399_detected_board, BOARD_PINEBOOK_PRO, memory_order_release);
	} else {
		info("not running on a Pinebook Pro ⇒ not setting up regulators\n");
		atomic_store_explicit(&rk3399_detected_board, BOARD_ROCKPRO64, memory_order_release);
	}
	sched_queue_list(CURRENT_RUNQUEUE, &rk3399_board_detection_waiters);
}
