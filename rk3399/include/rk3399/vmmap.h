/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

enum vstack {
#define X(name) VSTACK_##name,
	DEFINE_VSTACK(X)
#undef X
	NUM_VSTACK
};

#define VSTACK_BASE(vstack) (UINT64_C(0xffe00000) + (VSTACK_DEPTH + 0x1000) * ((vstack) + 1))

enum {vstacks_end = VSTACK_BASE(NUM_VSTACK - 1)};

enum regmap_id {
#define MMIO(name, snake, addr, type) REGMAP_##name,
	DEFINE_REGMAP(MMIO)
#undef MMIO
	NUM_REGMAP
};

#define VSTACK_MULTIMAP(name)\
	{.addr = VSTACK_BASE(VSTACK_##name) - VSTACK_DEPTH, .desc =  MMU_MAPPING(NORMAL, (u64)&vstack_frames[VSTACK_##name])},\
	{.addr = VSTACK_BASE(VSTACK_##name), .desc = 0}

enum regmap64k_id {
#define X(caps, snake, addr, type) REGMAP64K_##caps,
	DEFINE_REGMAP64K(X)
#undef X
	NUM_REGMAP64K
};
_Static_assert(0x1000 * NUM_REGMAP + 0x10000 * NUM_REGMAP64K <= 0x200000, "regmaps don't fit into 2 MiB");

enum {
	/* 'static const's may not be "compile-time constants" but enums sure are … take that, compiler! :) */
	regmap4k_base = (u64)((0x100000 - NUM_REGMAP) & 0xffff0) << 12,
	regmap64k_base = regmap4k_base - 0x10000 * NUM_REGMAP64K
};

#define REGMAP_BASE(map) (uintptr_t)(regmap4k_base + 0x1000 * (map))
#define REGMAP64K_BASE(map) (uintptr_t)(regmap64k_base + 0x10000 * (map))

_Static_assert((u64)REGMAP64K_BASE(NUM_REGMAP64K) == regmap4k_base, "");

#define MMIO(name, snake, addr, type) static volatile type UNUSED *const regmap_##snake = (type *)REGMAP_BASE(REGMAP_##name);
	DEFINE_REGMAP(MMIO)
#undef MMIO
#define X(name, snake, addr, type) static volatile type UNUSED *const regmap_##snake = (type *)REGMAP64K_BASE(REGMAP64K_##name);
	DEFINE_REGMAP64K(X)
#undef X

_Static_assert((int)vstacks_end <= (int)regmap64k_base, "VStacks and regmaps don't fit into 2 MiB");
