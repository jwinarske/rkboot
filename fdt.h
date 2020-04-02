#pragma once
#include <defs.h>

typedef struct {u32 v;} be32;
typedef struct {u64 v;} be64;
struct fdt_header {
	be32 magic;
	be32 totalsize;
	be32 struct_offset;
	be32 string_offset;
	be32 reserved_offset;
	be32 version;
	be32 last_compatible_version;
	be32 boot_cpu;
	be32 string_size;
	be32 struct_size; /* since v17 */
};

static inline u32 read_be32(const be32 *x) {
	return __builtin_bswap32(x->v);
}

static inline u64 read_be64(const be64 *x) {
	return __builtin_bswap64(x->v);
}

static inline _Bool has_zero_byte(u32 v) {
	v = ~v;
	v = (v & 0x0f0f0f0f) & (v >> 4);
	v &= v >> 2;
	v &= v >> 1;
	return v != 0;
}

static inline char strncmp(const char *a, const char *b, size_t len) {
	while (len--) {
		char x = *a++, y = *b++;
		if (x != y || !x || !y) {return x - y;}
	}
	return 0;
}

static inline char strcmp(const char *a, const char *b) {
	while (1) {
		char x = *a++, y = *b++;
		if (x != y || !x || !y) {return x - y;}
	}
}
