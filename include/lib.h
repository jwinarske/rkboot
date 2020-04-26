/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>
#include <stdio.h>
#include <assert.h>
#include <log.h>
#include <die.h>

u64 get_timestamp();

#define log(fmt, ...) printf("[%zu] " fmt, get_timestamp(), __VA_ARGS__)
#define logs(str) printf("[%zu] %s", get_timestamp(), str)
