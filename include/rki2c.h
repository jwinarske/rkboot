/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

struct rki2c_config;
struct rki2c_config UNUSED rki2c_calc_config_v1(u32 ctrl_mhz, u32 max_hz, u32 rise_ns, u32 fall_ns);
