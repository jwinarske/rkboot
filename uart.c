#include <main.h>
#include <uart.h>
#include <stdarg.h>

static u32 wait_until_fifo_free(u32 space_needed) {
	u32 queue_space;
	while (1) {
		queue_space = UART_FIFO_DEPTH - uart->tx_level;
		if (queue_space >= space_needed) {return queue_space;}
		yield();
	}
}

static u32 fmt_str(const char *str, u32 queue_space) {
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

void puts(const char *str) {
	fmt_str(str, 0);
}

static u32 fmt_hex(u64 val, char pad, u8 width, u32 queue_space) {
	u8 shift = val ? 60 - (__builtin_clzll(val) & ~3) : 0;
	u8 digits = shift / 4 + 1;
	if (digits < width) {
		u8 padlength = width - digits;
		if (queue_space < width) {
			queue_space = wait_until_fifo_free(width) - width;
		}
		while (padlength--) {uart->tx = pad;}
	} else if (queue_space < digits) {
		queue_space = wait_until_fifo_free(digits) - digits;
	}
	while (1) {
		u8 d = (val >> shift) & 0xf;
		u8 c = d >= 10 ? 'a' - 10 + d : '0' + d;
		uart->tx = c;
		if (!shift) {return queue_space;}
		shift -= 4;
	}
}

static u32 fmt_dec(u64 val, u32 queue_space) {
	char buf[24];
	buf[23] = 0;
	char *ptr = &buf[23];
	do {
		*--ptr = '0' + (val % 10);
		val /= 10;
	} while (val);
	return fmt_str(ptr, queue_space);
}

enum {
    FLAG_SIZE_T = 1,
};

static u32 fmt_format(const char *fmt, va_list va, u32 queue_space) {
	u8 c;
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
			u64 val = flags & FLAG_SIZE_T ? va_arg(va, u64) : va_arg(va, u32);
			if (c == 'u') {
				queue_space = fmt_dec(val, queue_space);
			} else if (c == 'x') {
				queue_space = fmt_hex(val, pad, width, queue_space);
			} else if (c == 's') {
				queue_space = fmt_str(va_arg(va, const char *), queue_space);
			} else {
				queue_space = fmt_str("ERROR: unknown format specifier", queue_space);
				halt_and_catch_fire();
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
	return queue_space;
}

void printf(const char *fmt, ...) {
	va_list va;
	va_start(va, fmt);
	fmt_format(fmt, va, 0);
	va_end(va);
}

int die(const char *fmt, ...) {
	va_list va;
	va_start(va, fmt);
	fmt_format(fmt, va, 0);
	halt_and_catch_fire();
	va_end(va);
}
