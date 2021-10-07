// SPDX-License-Identifier: CC0-1.0
#pragma once
#include <defs.h>

char *fmt_hex(u64 val, char pad, size_t width, char *out, char *end);
char *fmt_dec(u64 val, char pad, size_t width, char *out, char *end);
