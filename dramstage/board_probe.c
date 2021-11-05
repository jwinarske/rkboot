#include <rk3399/dramstage.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include <log.h>
#include <memaccess.h>
#include <runqueue.h>

#include <mmu.h>

#include <rki2c_regs.h>
#include <rki2c.h>

#include <rk3399.h>

static const enum rk3399_board init_board =
#if !CONFIG_SINGLE_BOARD
	BOARD_UNKNOWN
#elif CONFIG_BOARD_RP64
	BOARD_ROCKPRO64
#elif CONFIG_BOARD_PBP
	BOARD_PINEBOOK_PRO
#else
#error no board selected
#endif
;

_Atomic(u32) rk3399_detected_board = init_board;
struct sched_runnable_list rk3399_board_detection_waiters = {};

static bool ping_cw2015() {
	/* clk_i2c4 = PPLL/4 = 169 MHz, DTS has 200 */
	regmap_pmucru[PMUCRU_CLKSEL_CON + 3] = SET_BITS16(7, 0);

	volatile struct rki2c_regs *const i2c4 = regmap_i2c4;
	debug("RKI2C4_CON: %"PRIx32"\n", i2c4->control);
	struct rki2c_config i2c_cfg = rki2c_calc_config_v1(169, 1000000, 600, 20);
	debug("setup %"PRIx32" %"PRIx32"\n", i2c_cfg.control, i2c_cfg.clkdiv);
	i2c4->clkdiv = i2c_cfg.clkdiv;
	i2c4->control = i2c_cfg.control;
	regmap_pmugrf[PMUGRF_GPIO1B_IOMUX] = SET_BITS16(2, 1) << 6 | SET_BITS16(2, 1) << 8;
	i2c4->control = i2c_cfg.control | RKI2C_CON_ENABLE | RKI2C_CON_START | RKI2C_CON_MODE_REGISTER_READ | RKI2C_CON_ACK;
	i2c4->slave_addr = 1 << 24 | 0x62 << 1;
	i2c4->reg_addr = 1 << 24 | 0;
	i2c4->rx_count = 1;
	u32 val;
	while (!((val = i2c4->int_pending) & RKI2C_INTMASK_XACT_END)) {
		sched_yield();
	}
	debug("RKI2C4_CON: %"PRIx32", _IPD: %"PRIx32"\n", i2c4->control, i2c4->int_pending);
	bool ack = !(val & 1 << RKI2C_INT_NAK);
	debug("%"PRIx32"\n", i2c4->rx_data[0]);
	i2c4->control = i2c_cfg.control | RKI2C_CON_ENABLE | RKI2C_CON_STOP;
	return ack;
}

void rk3399_probe_board() {
	enum rk3399_board board = init_board;
	if (CONFIG_BOARD_RP64 && CONFIG_BOARD_PBP) {
		if (ping_cw2015()) {
			info("ACK from i2c4-62, this seems to be a Pinebook Pro\n");
			board = BOARD_PINEBOOK_PRO;
		} else {
			info("not running on a Pinebook Pro ⇒ not setting up regulators\n");
			board = BOARD_ROCKPRO64;
		}
	}

	if (init_board == BOARD_UNKNOWN) {
		release32(&rk3399_detected_board, board);
		sched_queue_list(CURRENT_RUNQUEUE, &rk3399_board_detection_waiters);
	}

	if (board == BOARD_PINEBOOK_PRO) {
		mmu_map_mmio_identity(0xff420000, 0xff420fff);
		mmu_flush();
		*(volatile u32*)0xff420024 = 0x96e;
		*(volatile u32*)0xff420028 = 0x25c;
		*(volatile u32*)0xff42002c = 0x13;
		regmap_pmugrf[PMUGRF_GPIO1C_IOMUX] = SET_BITS16(2, 1) << 6;
	}
}
