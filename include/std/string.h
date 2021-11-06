/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <stdint.h>

void *memcpy(void *restrict dest, const void *restrict src, size_t n);
void *memset(void *dest, int c, size_t n);
int strncmp(const char *s1, const char *s2, size_t n);

#define memcmp(a, b, n) __builtin_memcmp((a), (b), (n))
#define memcpy(d, s, n) __builtin_memcpy((d), (s), (n))
#define memset(d, v, n) __builtin_memset((d), (v), (n))
#define strncmp(a, b, n) __builtin_strncmp((a), (b), (n))
#define strcmp(a, b) __builtin_strcmp((a), (b))

size_t strnlen(const char *s, size_t maxlen);
