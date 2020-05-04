#pragma once
#include "defs.h"

_Noreturn int PRINTF(1, 2) die(const char *fmt, ...);
_Noreturn void halt_and_catch_fire();

#define assert_msg(expr, ...) do {if (unlikely(!(expr))) {die(__VA_ARGS__);}}while(0)
#define assert_eq(a, b, type, fmt) do {type __tmp_a = (a), __tmp_b = (b);if (unlikely(a != b)) {die("%s:%s:%u: ASSERTION FAILED: "#a" ["fmt"] != "#b" ["fmt"]\n", __FILE__, __func__, __LINE__, __tmp_a, __tmp_b);}}while(0)
