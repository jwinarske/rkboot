/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

typedef u64 timestamp_t;
#define TICKS_PER_MICROSECOND 24
#define NSECS(n) ((u64)(n) * TICKS_PER_MICROSECOND / 1000)
#define USECS(n) ((u64)(n) * TICKS_PER_MICROSECOND)
#define MSECS(n) ((u64)(n) * 1024 * TICKS_PER_MICROSECOND)
#define PRIuTS PRIu64
