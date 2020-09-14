/* SPDX-License-Identifier: CC0-1.0 */
#include <inttypes.h>
#include <stdatomic.h>
#include <assert.h>

#include <sdhci.h>
#include <sdhci_regs.h>
#include <sdhci_helpers.h>
#include <log.h>
#include <timer.h>
#include <iost.h>

void sdhci_irq(struct sdhci_state *st) {
	volatile struct sdhci_regs *sdhci = st->regs;
	debugs("?");
	u32 int_st = sdhci->int_st;
	spew("SDHCI IRQ 0x%08"PRIx32"\n", int_st);
	sdhci->int_st = int_st;
	u32 int_st_old = atomic_fetch_or_explicit(&st->int_st, int_st, memory_order_release);
	if ((int_st & SDHCI_INT_XFER_COMPLETE) || int_st >> 16) {
		struct sdhci_xfer *xfer = atomic_load_explicit(&st->active_xfer, memory_order_acquire);
		if (xfer) {
			debugs("ending xfer\n");
			atomic_thread_fence(memory_order_release);
			enum iost status = (int_st | int_st_old) >> 16 ? IOST_GLOBAL : IOST_OK;
			atomic_store_explicit(&xfer->status, status, memory_order_relaxed);
			atomic_store_explicit(&st->active_xfer, 0, memory_order_relaxed);
		}
	}
	sched_queue_list(CURRENT_RUNQUEUE, &st->interrupt_waiters);
}

enum iost sdhci_wait_state(struct sdhci_state *st, u32 mask, u32 expected, timestamp_t timeout, const char *name) {
	volatile struct sdhci_regs *sdhci = st->regs;
	timestamp_t start = get_timestamp();
	u32 prests, int_st;
	while (1) {
		if ((int_st = atomic_load_explicit(&st->int_st, memory_order_relaxed)) >> 16) {
			info("%s error: intsts0x%08"PRIx32" prests0x%08"PRIx32" cmd %"PRIx16" %08"PRIx32"\n", name, int_st, prests, sdhci->cmd, sdhci->arg);
			return IOST_GLOBAL;
		} else if (((prests = sdhci->present_state) & mask) == expected) {
			break;
		} else if (get_timestamp() - start > timeout) {
			info("%s timeout: reg0x%08"PRIx32" mask0x%08"PRIx32" expected0x%08"PRIx32"\n", name, prests, mask, expected);
			return IOST_TRANSIENT;
		}
		printf("sleeping on PRESTS=0x%08"PRIx32"\n", prests);
		call_cc_ptr2_int2(sched_finish_u32, &sdhci->present_state, &st->interrupt_waiters, mask, mask & prests);
	}
	return IOST_OK;
}

enum iost sdhci_submit_cmd(struct sdhci_state *st, u32 cmd, u32 arg) {
	volatile struct sdhci_regs *sdhci = st->regs;
	u32 state_mask = SDHCI_PRESTS_CMD_INHIBIT | SDHCI_PRESTS_DAT_INHIBIT;
	enum iost res;
	if (IOST_OK != (res = sdhci_wait_state(st, state_mask, 0, USECS(1000), "inhibit"))) {return res;}
	info("[%"PRIuTS"] CMD %04"PRIx32" %08"PRIx32"\n", get_timestamp(), cmd, arg);
	sdhci->arg = arg;
	sdhci->cmd = (u16)cmd;
	return IOST_OK;
}
