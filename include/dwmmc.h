/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <stdatomic.h>
#include <runqueue.h>
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

struct dwmmc_xfer {
	_Atomic(u8) status;
	unsigned always_use_chaining : 1;
	unsigned second_buf : 1;
	u32 block_addr;
	size_t desc_cap, desc_size, xfer_bytes;
	phys_addr_t desc_addr;
	struct dwmmc_idmac_desc *desc;
};

struct dwmmc_regs;
struct dwmmc_state {
	volatile struct dwmmc_regs *regs;
	struct dwmmc_signal_services *svc;
	_Atomic(u32) int_st, idmac_st;
	struct sched_runnable_list interrupt_waiters;
	u32 cmd_template;

	_Atomic(struct dwmmc_xfer *) active_xfer;
};

struct sd_cardinfo;
_Bool dwmmc_init_early(struct dwmmc_state *state);
void dwmmc_irq(struct dwmmc_state *state);

_Bool dwmmc_init_late(struct dwmmc_state *state, struct sd_cardinfo *card);

HEADER_FUNC void dwmmc_wake_waiters(struct dwmmc_state *st) {
	sched_queue_list(CURRENT_RUNQUEUE, &st->interrupt_waiters);
}

enum iost dwmmc_start_request(struct dwmmc_xfer *xfer, u32 block_addr);
_Bool dwmmc_add_phys_buffer(struct dwmmc_xfer *xfer, phys_addr_t start, phys_addr_t end);
enum iost dwmmc_start_xfer(struct dwmmc_state *state, struct dwmmc_xfer *xfer);
enum iost dwmmc_wait_xfer(struct dwmmc_state *state, struct dwmmc_xfer *xfer);
