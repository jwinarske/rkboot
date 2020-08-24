/* SPDX-License-Identifier: CC0-1.0 */
#include <uart.h>
#include <stdio.h>
#include <stdarg.h>

#include <plat.h>
#include <die.h>

static u32 NO_ASAN wait_until_fifo_free(u32 space_needed) {
	u32 queue_space;
	while (1) {
		queue_space = UART_FIFO_DEPTH - uart->tx_level;
		if (queue_space >= space_needed) {return queue_space;}
		__asm__ volatile("yield");
	}
}

static u32 NO_ASAN fmt_str(const char *str, u32 queue_space) {
	while (1) {
		const u8 c = (u8)*str++;
		if (!c) {return queue_space;}
		if (c == '\n') {
			if (!queue_space) {
				queue_space = wait_until_fifo_free(1);
			}
			queue_space -= 1;
			uart->tx = '\r';
		}
		if (!queue_space) {
			queue_space = wait_until_fifo_free(1);
		}
		queue_space -= 1;
		uart->tx = c;
	}
}

int fflush(FILE UNUSED *f) {
	while (uart->tx_level) {asm("yield");}
	return 0;
}

int puts(const char *str) {
	u64 daif;
	__asm__ volatile("mrs %0, DAIF;msr DAIFSet, #15" : "=r"(daif));
	fmt_str(str, 0);
	__asm__ volatile("msr DAIF, %0" : : "r"(daif));
	return 0;
}

static u32 fmt_hex(u64 val, char pad, u8 width, u32 queue_space) {
	u8 shift = val ? 60 - (__builtin_clzll(val) & ~3) : 0;
	u8 digits = shift / 4 + 1;
	if (digits < width) {
		u8 padlength = width - digits;
		if (queue_space < width) {
			queue_space = wait_until_fifo_free(width);
		}
		queue_space -= width;
		while (padlength--) {uart->tx = pad;}
	} else if (queue_space < digits) {
		queue_space = wait_until_fifo_free(digits) - digits;
	} else {queue_space -= digits;}
	while (1) {
		u8 d = (val >> shift) & 0xf;
		u8 c = d >= 10 ? 'a' - 10 + d : '0' + d;
		uart->tx = c;
		if (!shift) {return queue_space;}
		shift -= 4;
	}
}

u32 fmt_dec(u64 val, char pad, u8 width, u32 queue_space) {
	char buf[24];
	buf[23] = 0;
	char *ptr = &buf[23];
	do {
		*--ptr = '0' + (val % 10);
		val /= 10;
	} while (val);
	u8 digits = 23 - (ptr - buf);
	if (digits < width) {
		u8 padlength = width - digits;
		if (queue_space < width) {
			queue_space = wait_until_fifo_free(width);
		}
		queue_space -= width;
		while (padlength--) {uart->tx = pad;}
	}
	return fmt_str(ptr, queue_space);
}

enum {
    FLAG_SIZE_T = 1,
};

static u32 fmt_format(const char *fmt, va_list *va, u32 queue_space) {
	u8 c;
	/*va_list va;
	va_copy(va, vargs);*/
	while ((c = (u8)*fmt++)) {
		if (c == '%') {
			u64 flags = 0;
			char pad = ' ';
			u8 width = 0;
			while (1) {
				c = *fmt++;
				if (c == 'z') {
					flags |= FLAG_SIZE_T;
				} else if (c == '0') {
					if (width != 0) {
						width *= 10;
					} else {
						pad = '0';
					}
				} else if (c >= '1' && c <= '9') {
					width = width * 10 + c - '0';
				} else {
					break;
				}
			}
			if (c == 's') {
				const char *str = va_arg(*va, const char *);
				queue_space = fmt_str(str, queue_space);
			} else if (c == 'c') {
				if (!queue_space) {
					queue_space = wait_until_fifo_free(1);
				}
				queue_space -= 1;
				uart->tx = va_arg(*va, int);
			} else {
				u64 val = flags & FLAG_SIZE_T ? va_arg(*va, u64) : va_arg(*va, u32);
				if (c == 'u') {
					queue_space = fmt_dec(val, pad, width, queue_space);
				} else if (c == 'x') {
					queue_space = fmt_hex(val, pad, width, queue_space);
				} else {
					queue_space = fmt_str("ERROR: unknown format specifier", queue_space);
					halt_and_catch_fire();
				}
			}
		} else {
			if (c == '\n') {
				if (!queue_space) {
					queue_space = wait_until_fifo_free(1);
				}
				queue_space -= 1;
				uart->tx = '\r';
			}
			if (!queue_space) {
				queue_space = wait_until_fifo_free(1);
			}
			queue_space -= 1;
			uart->tx = c;
		}
	}
	/*va_end(va);*/
	return queue_space;
}

int printf(const char *fmt, ...) {
	va_list va;
	va_start(va, fmt);
	u64 daif;
	__asm__ volatile("mrs %0, DAIF;msr DAIFSet, #15" : "=r"(daif));
	fmt_format(fmt, &va, 0);
	__asm__ volatile("msr DAIF, %0" : : "r"(daif));
	va_end(va);
	return 0;
}

int die(const char *fmt, ...) {
	va_list va;
	va_start(va, fmt);
	fmt_format(fmt, &va, 0);
	halt_and_catch_fire();
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
