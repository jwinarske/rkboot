/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#define memcpy(d, s, n) __builtin_memcpy((d), (s), (n))
#define memset(d, v, n) __builtin_memset((d), (v), (n))
#define strnlen(s, n) __builtin_strnlen((s), (n))
