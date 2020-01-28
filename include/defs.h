#pragma once
#include <stdint.h>
#include <stddef.h>

typedef unsigned char u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define unlikely(x) __builtin_expect((x), 0)

#define NO_ASAN __attribute__((no_sanitize_address))
#define UNUSED __attribute__((unused))
#define PRINTF(str_idx, start) __attribute__((format(printf, str_idx, start)))

#define CHECK_OFFSET(strct, member, offs) _Static_assert(offsetof(struct strct, member) == offs, "wrong offset for " #member)

