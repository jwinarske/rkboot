/* SPDX-License-Identifier: CC0-1.0 */
#include <defs.h>
#include <string.h>

#undef strnlen
size_t strnlen(const char *s, size_t maxlen) {
	const char *start = s, *end = s + maxlen;
	while (s < end && *s) {s += 1;}
	return s - start;
}
#undef strncmp
int strncmp(const char *a, const char *b, size_t maxlen) {
	while (maxlen--) {
		unsigned char c0 = *a++, c1 = *b++;
		if (c0 != c1) {return (int)c0 - c1;}
		if (!c0) {return -(int)c1;}
		if (!c1) {return c0;}
	}
	return 0;
}
#undef memcmp
int memcmp(const char *a, const char *b, size_t maxlen);
int memcmp(const char *a, const char *b, size_t maxlen) {
	while (maxlen--) {
		unsigned char c0 = *a++, c1 = *b++;
		if (c0 != c1) {return (int)c0 - c1;}
	}
	return 0;
}

#undef strcmp
int strcmp(const char *a, const char *b);
int strcmp(const char *a, const char *b) {
	while (1) {
		unsigned char c0 = *a++, c1 = *b++;
		if (c0 != c1) {return (int)c0 - c1;}
		if (!c0 || !c1) {return 0;}
	}
}
