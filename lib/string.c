/* SPDX-License-Identifier: CC0-1.0 */
#include <defs.h>
#include <string.h>

#undef strnlen
size_t strnlen(const char *s, size_t maxlen) {
	const char *start = s, *end = s + maxlen;
	while (s < end && *s) {s += 1;}
	return s - start;
}
