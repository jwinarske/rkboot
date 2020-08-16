/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>
#include <plat.h>

enum dwmmc_clock {
	DWMMC_CLOCK_400K = 0,
	DWMMC_CLOCK_25M,
	DWMMC_CLOCK_50M,
	DWMMC_CLOCK_100M,
	DWMMC_CLOCK_208M,
};
enum dwmmc_signal_voltage {
	DWMMC_SIGNAL_3V3 = 0,
	DWMMC_SIGNAL_1V8,
};

struct dwmmc_signal_services {
	_Bool (*set_clock)(struct dwmmc_signal_services *, enum dwmmc_clock);
	_Bool (*set_signal_voltage)(struct dwmmc_signal_services *, enum dwmmc_clock);
	u8 frequencies_supported;
	u8 voltages_supported;
};

struct dwmmc_regs;
_Bool dwmmc_init(volatile struct dwmmc_regs *dwmmc, struct dwmmc_signal_services *svc);
void dwmmc_read_poll(volatile struct dwmmc_regs *dwmmc, u32 sector, void *buf, size_t total_bytes);
