/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include "../include/defs.h"

enum {LZCOMMON_BLOCK = 8};
void lzcommon_literal_copy(u8 *dest, const u8 *src, u32 length);
void lzcommon_match_copy(u8 *dest, u32 dist, u32 length);

#define DEFINE_COMPR_PROBE_STATUS\
	X(SIZE_UNKNOWN, "probe successful, size unknown")\
	X(SIZE_KNOWN, "probe successful, size known")\
	X(NOT_ENOUGH_DATA, "not enough data to probe")\
	X(WRONG_MAGIC, "wrong magic number")\
	X(UNIMPLEMENTED_FEATURE,  "unimplemented feature used")\
	X(RESERVED_FEATURE, "reserved feature used")

enum compr_probe_status {
#define X(name, msg) COMPR_PROBE_##name,
	DEFINE_COMPR_PROBE_STATUS
#undef X
	NUM_COMPR_PROBE_STATUS,
	COMPR_PROBE_LAST_SUCCESS = COMPR_PROBE_SIZE_KNOWN
};
