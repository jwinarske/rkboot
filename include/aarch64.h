#pragma once

enum {
	SCTLR_M = 1,
	SCTLR_C = 4,
	SCTLR_SA = 8,
	SCTLR_I = 0x1000,
	SCTLR_EL3_RES1 = 0x30c50830
};
enum {
	SCR_EA = 8,
	SCR_FIQ = 4,
	SCR_IRQ = 2,
	SCR_EL3_RES1 = 0x30,
};
enum {
	MEM_TYPE_DEV_nGnRnE = 0,
	MEM_TYPE_DEV_nGnRE = 1,
	MEM_TYPE_DEV_nGRE = 2,
	MEM_TYPE_DEV_GRE = 3,
	MEM_TYPE_NORMAL = 4
};
