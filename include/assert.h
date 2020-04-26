/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <die.h>

#ifndef NDEBUG
#define assert(expr) assert_msg(expr,  "%s:%s:%u: ASSERTION FAILED: %s\n", __FILE__, __FUNCTION__, __LINE__, #expr)
#else
#define assert(expr) ((void)0)
#endif
