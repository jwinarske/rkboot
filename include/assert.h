/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

#define assert_msg(expr, ...) do {if (unlikely(!(expr))) {die(__VA_ARGS__);}}while(0)
#ifndef NDEBUG
#define assert(expr) assert_msg(expr,  "%s:%s:%u: ASSERTION FAILED: %s\n", __FILE__, __FUNCTION__, __LINE__, #expr)
#else
#define assert(expr) ((void)0)
#endif
