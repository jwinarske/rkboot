/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef unsigned char u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef u64 ureg_t;
typedef u64 timestamp_t;
#define TICKS_PER_MICROSECOND 24

#define unlikely(x) __builtin_expect((x), 0)
#define likely(x) __builtin_expect((x), 1)

#define FALLTHROUGH __attribute__((fallthrough))
#define NO_ASAN __attribute__((no_sanitize_address))
#define UNUSED __attribute__((unused))
#define PRINTF(str_idx, start) __attribute__((format(printf, str_idx, start)))
#define UNINITIALIZED __attribute__((section(".bss.noinit")))
#define NORETURN_ATTR __attribute__((__noreturn__))
#define HEADER_FUNC static inline UNUSED

#define CHECK_OFFSET(strct, member, offs) _Static_assert(offsetof(struct strct, member) == offs, "wrong offset for " #member)

#define STRINGIFY(x) #x
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#define for_range(i, a, b) for (u32 i = a; i < b; ++i)
#define for_array(i, arr) for (u32 i = 0; i < ARRAY_SIZE(arr); ++i)

#define SET_BITS16(number, value) (((((u32)1 << number) - 1) << 16) | ((u32)(u16)(value) & (((u32)1 << number) - 1)))
#define SET_BITS32(number, value) (((((u64)1 << number) - 1) << 32) | (u64)((u32)(value) & (((u32)1 << number) - 1)))

enum {UNREACHABLE = 0};

static inline UNUSED u32 div_round_up_u32(u32 a, u32 b) {
	return (b - 1 + a) / b;
}
