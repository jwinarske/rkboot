/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <stdint.h>

void *memcpy(void *restrict dest, const void *restrict src, size_t n);
void *memset(void *dest, int c, size_t n);
#define memcpy(d, s, n) __builtin_memcpy((d), (s), (n))
#define memset(d, v, n) __builtin_memset((d), (v), (n))

size_t strnlen(const char *s, size_t maxlen);
