#pragma once
#include "defs.h"

_Noreturn int PRINTF(1, 2) die(const char *fmt, ...);
_Noreturn void halt_and_catch_fire();

#define assert_msg(expr, ...) do {if (unlikely(!(expr))) {die(__VA_ARGS__);}}while(0)
