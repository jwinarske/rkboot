/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

struct gic_distributor;
struct gic_redistributor;


enum {
	IGROUP_0 = 0,
	IGROUP_NS1 = 1,
	IGROUP_S1 = 2,
	INTR_LEVEL = 0,
	INTR_EDGE = 2 << 2,
};

void gicv3_per_cpu_setup(volatile struct gic_redistributor *redist);
void gicv3_per_cpu_teardown(volatile struct gic_redistributor *redist);

void gicv2_global_setup(volatile struct gic_distributor *dist);
void gicv2_setup_spi(volatile struct gic_distributor *dist, u16 intid, u8 priority, u8 targets, u32 flags);

void gicv2_disable_spi(volatile struct gic_distributor *dist, u16 intid);
void gicv2_enable_spi(volatile struct gic_distributor *dist, u16 intid);
void gicv2_wait_disabled(volatile struct gic_distributor *dist);
