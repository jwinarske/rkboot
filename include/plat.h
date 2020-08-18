/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

typedef u64 timestamp_t;
#define TICKS_PER_MICROSECOND 24
#define USECS(n) ((n) * TICKS_PER_MICROSECOND)
#define PRIuTS PRIu64
