/* SPDX-License-Identifier: CC0-1.0 */
	{
		.addr = (u64)&__start__,
		.desc = MMU_MAPPING(NORMAL, (u64)&__start__) + MEM_ACCESS_RO_PRIV
	},
	{
		.addr = (u64)&__ro_end__,
		.desc = MMU_MAPPING(NORMAL, (u64)&__ro_end__)
	},
	{
		.addr = (u64)&__bss_write_through__,
		.desc = MMU_MAPPING(WRITE_THROUGH, (u64)&__bss_write_through__)
	},
	{
		.addr = (u64)&__bss_uncached__,
		.desc = MMU_MAPPING(UNCACHED, (u64)&__bss_uncached__)
	},
	{.addr = (u64)&__end__, .desc = 0},
#define X(caps, name, baseaddr, type)  {\
	.addr = (u64)REGMAP64K_BASE(REGMAP64K_##caps),\
	.desc = MMU_MAPPING(DEV_nGnRnE, UINT64_C(baseaddr))\
},
	DEFINE_REGMAP64K
#undef X
#define MMIO(caps, name, baseaddr, type)  {\
	.addr = (u64)REGMAP_BASE(REGMAP_##caps),\
	.desc = MMU_MAPPING(DEV_nGnRnE, UINT64_C(baseaddr))\
},
	DEFINE_REGMAP
#undef MMIO
	{.addr = (u64)REGMAP_BASE(NUM_REGMAP), .desc = 0},
