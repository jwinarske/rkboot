/* SPDX-License-Identifier: CC0-1.0 */
#include <defs.h>

size_t strnlen(const char *s, size_t maxlen) {
	const char *start = s, *end = s + maxlen;
	while (s < end && *s) {s += 1;}
	return s - start;
}

void *memset(void *s, int c, size_t n) {
	u8 *p = s;
	while (n--) {*p++ = (u8)c;}
	return s;
}
