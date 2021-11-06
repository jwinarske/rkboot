/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <stdint.h>

struct rkpwm_regs {
	struct {
		uint32_t counter;
		uint32_t period;
		uint32_t duty;
		uint32_t control;
#define RKPWM_EN 1
#define RKPWM_ONE_SHOT 0
#define RKPWM_CONTINUOUS 2
#define RKPWM_CAPTURE 4
#define RKPWM_DUTY_POL(i) ((i) << 3 & 8)
#define RKPWM_INACTIVE_POL(i) ((i) << 4 & 0x10)
#define RKPWM_CENTER 0x20
#define RKPWM_LOW_POWER 0x100
#define RKPWM_SCALE(i) (((i) << 15 & 0x00ff0000) | 0x200)  // i must be even
#define RKPWM_PRESCALE(i) ((i) << 12 & 0x7000)
#define RKPWM_REPEAT(i) ((i) << 24 & 0xff000000)
	} channels[4];
	uint32_t int_st;
	uint32_t int_en;
	uint32_t padding[2];
	uint32_t fifo_control;
	uint32_t fifo_int_st;
	uint32_t fifo_timout_thresh;
	uint32_t fifo[8];
};
