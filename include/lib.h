/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>
#include <stdio.h>
#include <assert.h>
#include <log.h>

_Noreturn int PRINTF(1, 2) die(const char *fmt, ...);
_Noreturn void halt_and_catch_fire();

u64 get_timestamp();

#define log(fmt, ...) printf("[%zu] " fmt, get_timestamp(), __VA_ARGS__)
#define logs(str) printf("[%zu] %s", get_timestamp(), str)
