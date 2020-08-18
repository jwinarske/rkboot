/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <inttypes.h>

#include <runqueue.h>
#include <timer.h>
#include <log.h>

#define DEFINE_WAITFUNC(type) HEADER_FUNC _Bool wait_##type(volatile type *reg, type mask, type expected, timestamp_t timeout, const char UNUSED *name) {\
	timestamp_t start = get_timestamp();\
	type val;\
	while (((val = *reg) & mask) != expected) {\
		if (get_timestamp() - start > timeout) {\
			info("%s timeout: reg0x%08"PRIx32" mask0x%08"PRIx32" expected0x%08"PRIx32"\n", name, val, mask, expected);\
			return 0;\
		}\
		sched_yield();\
	}\
	return 1;\
}\
HEADER_FUNC _Bool wait_##type##_set(volatile type *reg, type mask, timestamp_t timeout, const char *name) {return wait_##type(reg, mask, mask, timeout, name);}\
HEADER_FUNC _Bool wait_##type##_unset(volatile type *reg, type mask, timestamp_t timeout, const char *name) {return wait_##type(reg, mask, 0, timeout, name);}
DEFINE_WAITFUNC(u32)
DEFINE_WAITFUNC(u16)
DEFINE_WAITFUNC(u8)
