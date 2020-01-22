#pragma once
#include <stdint.h>
#include <stddef.h>

typedef unsigned char u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define unlikely(x) __builtin_expect((x), 0)
#define STRINGIFY(x) #x
#define assert(expr) if (unlikely(!(expr))) {die( __FILE__":"STRINGIFY(__LINE__)": ASSERTION FAILED: "#expr);}
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#define CHECK_OFFSET(strct, member, offs) _Static_assert(offsetof(struct strct, member) == offs, "wrong offset for " #member)

#define bounds_checked(arr, i) ((unlikely ((i) >= ARRAY_SIZE(arr)) ? die(__FILE__":"STRINGIFY(__LINE__)": ERROR: "#arr "[" #i "] out of bounds", __FILE__, __LINE__) : 0), arr[i])

#define for_range(i, a, b) for (u32 i = a; i < b; ++i)
#define for_array(i, arr) for (u32 i = 0; i < ARRAY_SIZE(arr); ++i)
