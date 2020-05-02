/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

enum {
	GICD_CTLR_RWP = 1 << 31,
	GICD_CTLR_ARE_NS = 32,
	GICD_CTLR_ARE_S = 16,
	GICD_CTLR_EnableGrp1S = 4,
	GICD_CTLR_EnableGrp1NS = 2,
	GICD_CTLR_EnableGrp0 = 1,
};

struct gic_distributor {
	u32 control;
	u32 type;
	u32 implementer_id;
	u32 reserved1;
	u32 status;
	u32 reserved2[3];
	u32 implementation_defined1[8];
	struct {
		u32 set;
		u32 reserved1;
		u32 clear;
		u32 reserved2;
	} clrsetspi[2];
	u32 reserved3[8];
	u32 group[32];
	u32 enable[32];
	u32 disable[32];
	u32 set_pending[32];
	u32 clear_pending[32];
	u32 activate[32];
	u32 deactivate[32];
	u8 priority[1020];
	u32 reserved4;
	u8 targets[1020];
	u32 reserved5;
	u32 configuration[64];
	u32 group_modifier[32];
	u32 ns_access[64];
	u32 generate_sgi;
};
CHECK_OFFSET(gic_distributor, clrsetspi, 0x40);
CHECK_OFFSET(gic_distributor, enable, 0x100);
CHECK_OFFSET(gic_distributor, priority, 0x400);
CHECK_OFFSET(gic_distributor, group_modifier, 0xd00);

struct gic_redistributor {
	u32 control;
	u32 implementer_id;
	u32 type;
	u32 status;
	u32 wake;
};

#define ICC_IAR0_EL1 "S3_0_C12_C8_0"
#define ICC_IAR1_EL1 "S3_0_C12_C12_0"
#define ICC_EOIR0_EL1 "S3_0_C12_C8_1"
