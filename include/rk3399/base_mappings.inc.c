/* SPDX-License-Identifier: CC0-1.0 */
	{(u64)&__start__, (u64)&__start__ + (PGTAB_PAGE(MEM_TYPE_NORMAL) | MEM_ACCESS_RO_PRIV)},\
	{(u64)&__ro_end__, (u64)&__ro_end__ + (PGTAB_PAGE(MEM_TYPE_NORMAL) | MEM_ACCESS_RW_PRIV)},\
	{(u64)&__end__, 0},
#define X(caps, name, baseaddr, type)  {\
	.addr = (u64)REGMAP64K_BASE(REGMAP64K_##caps),\
	.desc = PGTAB_PAGE(MEM_TYPE_DEV_nGnRnE) | MEM_ACCESS_RW_PRIV | UINT64_C(baseaddr)\
},
	DEFINE_REGMAP64K
#undef X
#define MMIO(caps, name, baseaddr, type)  {\
	.addr = (u64)REGMAP_BASE(REGMAP_##caps),\
	.desc = PGTAB_PAGE(MEM_TYPE_DEV_nGnRnE) | MEM_ACCESS_RW_PRIV | UINT64_C(baseaddr)\
},
	DEFINE_REGMAP
#undef MMIO
	{
		.addr = (u64)REGMAP_BASE(NUM_REGMAP),
		.desc = 0
	},
