/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>
#include <stdarg.h>
#define FREESTANDING_STDIO

int putchar(int);
int puts(const char *);
int PRINTF(1, 2) printf(const char *fmt, ...);
typedef void FILE;
int fflush(FILE *file);
static FILE *const stdout = 0;
int vprintf(const char *fmt, va_list va);
