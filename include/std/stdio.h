/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>
#define FREESTANDING_STDIO

int puts(const char *);
int PRINTF(1, 2) printf(const char *fmt, ...);
typedef void FILE;
int fflush(FILE *file);
static FILE *const stdout = 0;
