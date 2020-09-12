/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <runqueue.h>

enum sdhci_phy_setup_action {
	SDHCI_PHY_START = 1,
	SDHCI_PHY_STOP = 2,
	SDHCI_PHY_MODIFY = 3,
};

struct sdhci_phy {
	_Bool (*setup)(struct sdhci_phy *, enum sdhci_phy_setup_action action);
	_Bool (*lock_freq)(struct sdhci_phy *phy, u32 khz);
};

struct sdhci_state {
	volatile struct sdhci_regs *regs;
	struct sdhci_phy *phy;
	u64 caps;
	u8 version;

	_Atomic(u32) int_st;
	_Atomic(u8 *) sdma_buf;
	u8 *buf_end;
	u32 sdma_buffer_size;
	struct sched_runnable_list interrupt_waiters;
};

struct sdhci_regs;
void sdhci_irq(struct sdhci_state *st);
_Bool sdhci_try_abort(struct sdhci_state *st);

HEADER_FUNC void sdhci_wake_threads(struct sdhci_state *st) {
	sched_queue_list(CURRENT_RUNQUEUE, &st->interrupt_waiters);
}

struct mmc_cardinfo;
enum iost sdhci_init_late(struct sdhci_state *st, struct mmc_cardinfo *card);
