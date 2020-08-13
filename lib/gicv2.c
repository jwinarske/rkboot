/* SPDX-License-Identifier: CC0-1.0 */
#include <gic.h>
#include <gic_regs.h>
#include <assert.h>

void gicv2_wait_disabled(volatile struct gic_distributor *dist) {
	while (dist->control & GICD_CTLR_RWP) {__asm__("yield");}
}

void gicv2_global_setup(volatile struct gic_distributor *dist) {
	u32 ctlr = dist->control;
	assert((ctlr & (GICD_CTLR_ARE_NS | GICD_CTLR_ARE_S)) == 0);
	dist->control = GICD_CTLR_EnableGrp0 | GICD_CTLR_EnableGrp1NS;
	for_range(i, 1, 32) {
		dist->disable[i] = ~(u32)0;
		dist->clear_pending[i] = ~(u32)0;
		dist->deactivate[i] = ~(u32)0;
	}
}

void gicv2_setup_spi(volatile struct gic_distributor *dist, u16 intr, u8 priority, u8 targets, u32 flags) {
	assert(intr >= 32 && intr < 1020);
	u32 bit = 1 << (intr % 32);
	dist->targets[intr] = targets;
	dist->priority[intr] = priority;
	if (flags & 1) {
		dist->group[intr / 32] |= bit;
	} else {
		dist->group[intr / 32] &= ~bit;
	}
	if (flags & 2) {
		dist->group_modifier[intr / 32] |= bit;
	} else {
		dist->group_modifier[intr / 32] &= ~bit;
	}
	u32 tmp = dist->configuration[intr / 16];
	u16 pos =intr % 16 * 2;
	dist->configuration[intr / 16] = (tmp & ~((u32)32 << pos)) | (flags >> 2 & 3) << pos;
	dist->enable[intr / 32] = bit;
}

void gicv2_disable_spi(volatile struct gic_distributor *dist, u16 intid) {
	dist->disable[intid / 32] = 1 << (intid % 32);
}
