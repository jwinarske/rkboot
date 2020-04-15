/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>
#define FREESTANDING_STDIO

void puts(const char *);
void PRINTF(1, 2) printf(const char *fmt, ...);
