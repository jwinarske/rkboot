/* SPDX-License-Identifier: CC0-1.0 */
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdarg.h>

#include <plat.h>
#include <die.h>

int puts(const char *str) {
	return printf("%s", str);
}

char *fmt_hex(u64 val, char pad, size_t width, char *out, char *end) {
	u8 shift = val ? 60 - (__builtin_clzll(val) & ~3) : 0;
	size_t digits = shift / 4 + 1;
	if (digits < width) {
		size_t padlength = width - digits;
		if ((size_t)(end - out) < padlength) {return 0;}
		while (padlength--) {*out++ = pad;}
	}
	if ((size_t)(end - out) < digits) {return 0;}
	while (1) {
		u8 d = (val >> shift) & 0xf;
		u8 c = d >= 10 ? 'a' - 10 + d : '0' + d;
		*out++ = c;
		if (!shift) {return out;}
		shift -= 4;
	}
}

char *fmt_dec(u64 val, char pad, size_t width, char *out, char *end) {
	char buf[24];
	char *ptr = buf + sizeof(buf), *buf_end = ptr;
	do {
		*--ptr = '0' + (val % 10);
		val /= 10;
	} while (val);
	size_t digits = buf_end - ptr;
	if (digits < width) {
		size_t padlength = width - digits;
		if ((size_t)(end - out) < padlength) {return 0;}
		while (padlength--) {*out++ = pad;}
	}
	if ((size_t)(end - out) < digits) {return 0;}
	while (ptr < buf_end) {*out++ = *ptr++;}
	return out;
}

_Noreturn static void format_overflow() {
	static const char error[] = "ERROR: formatting overflow\r\n";
	plat_write_console(error, sizeof(error) - 1);
	plat_panic();
}

static void end_line(char *buf, char *out, char *end) {
	if (out == end - 2) {format_overflow();}
	*out++ = '\r';
	*out++ = '\n';
	plat_write_console(buf, out - buf);
}

int vprintf(const char *fmt, va_list va) {
	char buf[CONFIG_BUF_SIZE];
	char *end = buf + sizeof(buf), *out = buf;
	char c;
	size_t printed = 0;
	while ((c = *fmt++)) {
		if (c == '%') {
			c = *fmt++;
			if (c == '%') {
				if (end == out) {format_overflow();}
				*out++ = '%';
				continue;
			}
			char pad = ' ';
			if (c == '0') {
				pad = '0';
				c = *fmt++;
			}
			size_t width = 0;
			while (c >= '0' && c <= '9') {
				width = width * 10 + c - '0';
				if ((size_t)(end - out) < width) {
					static const char error[] = "BUG: field width too large for buffer\r\n";
					plat_write_console(error, sizeof(error) - 1);
					plat_panic();
				}
				c = *fmt++;
			}
			u64 value;
			if (c == 's') {
				const char *str = va_arg(va, const char *);
				while ((c = *str++) != 0) {
					if (c == '\n') {
						end_line(buf, out, end);
						out = buf;
						continue;
					}
					if (out == end) {format_overflow();}
					*out++ = c;
				}
				continue;
			} else if (c == 'z') {
				value = va_arg(va, size_t);
				c = *fmt++;
			} else if (c == 'l') {
				c = *fmt++;
				if (c == 'l') {
					value = va_arg(va, unsigned long long);
					c = *fmt++;
				} else {
					value = va_arg(va, unsigned long);
				}
			} else if (c == 'h') {
				value = va_arg(va, int);
				c = *fmt++;
				if (c == 'h') {c = *fmt++;}
			} else if (c == 'c') {
				if (out == end) {format_overflow();}
				*out++ = (char)va_arg(va, int);
				continue;
			} else {
				value = va_arg(va, unsigned int);
			}
			if (c == 'u') {
				out = fmt_dec(value, pad, width, out, end);
				if (!out) {format_overflow();}
			} else if (c == 'x') {
				out = fmt_hex(value, pad, width, out, end);
				if (!out) {format_overflow();}
			} else {
				plat_write_console(buf, out - buf);
				static const char error[] = "BUG: unknown conversion specification\r\n";
				plat_write_console(error, sizeof(error) - 1);
				plat_panic();
			}
		} else if (c == '\n') {
			end_line(buf, out, end);
			out = buf;
			printed += out - buf + 2;
		} else {
			if (out == end) {format_overflow();}
			*out++ = c;
		}
	}
	if (buf < out) {
		plat_write_console(buf, out - buf);
		printed += out - buf;
	}
	return printed <= INT_MAX ? (int)printed : INT_MAX;
}

int printf(const char *fmt, ...) {
	va_list va;
	va_start(va, fmt);
	int res = vprintf(fmt, va);
	va_end(va);
	return res;
}

int die(const char *fmt, ...) {
	va_list va;
	va_start(va, fmt);
	vprintf(fmt, va);
	plat_panic();
	va_end(va);
}

#ifdef CONFIG_ASAN
static void NO_ASAN asan_report(uintptr_t addr, size_t size, const char *type) {
	puts("ASAN REPORT: ");
	puts(type);
	fmt_dec((u64)size, 0, 0, 0);
	puts(" at 0x");
	fmt_hex((u64)addr, '0', 0, 0);
	puts("\n");
	halt_and_catch_fire();
}
#define asan(type, size) void NO_ASAN __asan_report_##type##size##_noabort(uintptr_t addr) {asan_report(addr, size, #type);} void NO_ASAN __asan_##type##_noabort(uintptr_t addr) {if (addr >= 0xff8c0000 && addr < 0xff8f0000) {if }}
asan(load, 1)
asan(store, 1)
asan(load, 2)
asan(store, 2)
asan(load, 4)
asan(store, 4)
asan(load, 8)
asan(store, 8)

void __asan_handle_no_return() {
	puts("ASAN no_return\n");
	halt_and_catch_fire();
}
#endif /* CONFIG_ASAN */

#ifdef UBSAN
enum {
	type_kind_int = 0,
	type_kind_float = 1,
	type_unknown = 0xffff
};

struct type_descriptor {
	u16 type_kind;
	u16 type_info;
	char type_name[1];
};

struct source_location {
	const char *file_name;
	union {
		unsigned long reported;
		struct {
			u32 line;
			u32 column;
		};
	};
};

struct overflow_data {
	struct source_location location;
	struct type_descriptor *type;
};

_Noreturn void __ubsan_handle_divrem_overflow(struct overflow_data UNUSED *data, void UNUSED *lhs, void UNUSED *rhs) {
	puts("UBSAN: % overflow\n");
	halt_and_catch_fire();
}

_Noreturn void __ubsan_handle_pointer_overflow(struct overflow_data UNUSED *data, void UNUSED *lhs, void UNUSED *rhs) {
	puts("UBSAN: pointer overflow\n");
	halt_and_catch_fire();
}

struct type_mismatch_data {
	struct source_location location;
	struct type_descriptor *type;
	unsigned long alignment;
	unsigned char type_check_kind;
};

struct type_mismatch_data_v1 {
	struct source_location location;
	struct type_descriptor *type;
	unsigned char log_alignment;
	unsigned char type_check_kind;
};

_Noreturn void __ubsan_handle_type_mismatch_v1(struct type_mismatch_data_v1 UNUSED *data, void UNUSED *ptr) {
	puts("UBSAN: type mismatch\n");
	halt_and_catch_fire();
}

struct type_mismatch_data_common {
	struct source_location *location;
	struct type_descriptor *type;
	unsigned long alignment;
	unsigned char type_check_kind;
};

struct nonnull_arg_data {
	struct source_location location;
	struct source_location attr_location;
	int arg_index;
};

struct out_of_bounds_data {
	struct source_location location;
	struct type_descriptor *array_type;
	struct type_descriptor *index_type;
};

_Noreturn void __ubsan_handle_out_of_bounds(struct out_of_bounds_data UNUSED *data, void UNUSED *index) {
	puts("UBSAN: index out of bounds\n");
	halt_and_catch_fire();
}

struct shift_out_of_bounds_data {
	struct source_location location;
	struct type_descriptor *lhs_type;
	struct type_descriptor *rhs_type;
};

_Noreturn void __ubsan_handle_shift_out_of_bounds(struct shift_out_of_bounds_data UNUSED *data, void UNUSED *lhs, void UNUSED *rhs) {
	puts("UBSAN: shift out of bounds\n");
	halt_and_catch_fire();
}

struct unreachable_data {
	struct source_location location;
};

_Noreturn void __ubsan_handle_builtin_unreachable(struct unreachable_data UNUSED *data) {
	puts("UBSAN: shift out of bounds\n");
	halt_and_catch_fire();
}

struct invalid_value_data {
	struct source_location location;
	struct type_descriptor *type;
};

_Noreturn void __ubsan_handle_load_invalid_value(struct invalid_value_data UNUSED *data, void UNUSED *value) {
	puts("UBSAN: shift out of bounds\n");
	halt_and_catch_fire();
}
#endif
