/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <runqueue.h>
#include <plat.h>
#include <iost.h>

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
	_Atomic(struct sdhci_xfer *) active_xfer;
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

struct sdhci_adma2_desc8 {
	_Atomic(u32) cmd;
	u32 addr;
};

enum {
	SDHCI_CREATING = NUM_IOST,
	SDHCI_SUBMITTED,
};

struct sdhci_xfer {
	struct sdhci_xfer *next;
	_Atomic(u8) status;
	phys_addr_t desc_addr;
	struct sdhci_adma2_desc8 *desc8;
	size_t desc_cap, desc_size, xfer_bytes;
};

_Bool sdhci_reset_xfer(struct sdhci_xfer *xfer);
_Bool sdhci_add_phys_buffer(struct sdhci_xfer *xfer, phys_addr_t buf, phys_addr_t buf_end);
enum iost sdhci_start_xfer(struct sdhci_state *st, struct sdhci_xfer *xfer, u32 addr);
enum iost sdhci_wait_xfer(struct sdhci_state *st, struct sdhci_xfer *xfer);
