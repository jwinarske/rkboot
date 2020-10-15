/* SPDX-License-Identifier: CC0-1.0 */
#include <rk3399.h>
#include <rk3399/sramstage.h>
#include <mmu.h>
#include <rktimer_regs.h>
#include <rkcrypto_v1_regs.h>
#include <log.h>

void misc_init() {
	/* mux out GPIO0A5 as GPIO, not emmc_pwren: on supported boards, this pin is used for the power button */
	pmugrf[PMUGRF_GPIO0A_IOMUX] = SET_BITS16(2, 0) << 10;

	volatile struct rktimer_regs *timer = regmap_stimer0 + 0;
	timer->control = 0;
	timer->load_count2 = timer->load_count3 = timer->load_count0 = 0;
	timer->load_count0 = 240000;
	timer->interrupt_status = 1;
	timer->control = RKTIMER_ENABLE | RKTIMER_INT_EN;

	volatile struct rkcrypto_v1_regs *crypto1 = regmap_crypto1;
	crypto1->trng_control = RKCRYPTO_V1_TRNG_ENABLE(1000);
	crypto1->control = SET_BITS16(1, 1) << RKCRYPTO_V1_CTRL_TRNG_START_BIT;
	info("trng: %"PRIx32" %"PRIx32"\n", crypto1->control, crypto1->trng_control);
}
