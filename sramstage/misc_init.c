/* SPDX-License-Identifier: CC0-1.0 */
#include <rk3399.h>
#include <rk3399/sramstage.h>
#include <mmu.h>
#include <rktimer_regs.h>
#include <rkcrypto_v1_regs.h>
#include <log.h>

void misc_init() {
	mmu_map_mmio_identity((u64)stimer0, (u64)stimer0 + 0xfff);
	mmu_map_mmio_identity((u64)crypto1, (u64)crypto1 + 0xfff);
	mmu_flush();
	stimer0[0].control = 0;
	stimer0[0].load_count2 = stimer0[0].load_count3 = stimer0[0].load_count0 = 0;
	stimer0[0].load_count0 = 240000;
	stimer0[0].interrupt_status = 1;
	stimer0[0].control = RKTIMER_ENABLE | RKTIMER_INT_EN;

	crypto1->trng_control = RKCRYPTO_V1_TRNG_ENABLE(1000);
	crypto1->control = SET_BITS16(1, 1) << RKCRYPTO_V1_CTRL_TRNG_START_BIT;
	info("trng: %"PRIx32" %"PRIx32"\n", crypto1->control, crypto1->trng_control);
}
