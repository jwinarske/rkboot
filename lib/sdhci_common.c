/* SPDX-License-Identifier: CC0-1.0 */
#include <inttypes.h>
#include <stdatomic.h>

#include <sdhci.h>
#include <sdhci_regs.h>
#include <log.h>
#include <timer.h>


void sdhci_irq(volatile struct sdhci_regs *sdhci, struct sdhci_state *st) {
	u32 status = sdhci->int_st, status_remaining = status;
	debug("SDHCI IRQ 0x%08"PRIx32"\n", status);
	if (status & (SDHCI_INT_CMD_COMPLETE | SDHCI_INT_XFER_COMPLETE)) {
		/* completion interrupt: just ack, thread will know what to do next */
		sdhci->normal_int_st = status & (SDHCI_INT_CMD_COMPLETE | SDHCI_INT_XFER_COMPLETE);
		status_remaining &= ~(SDHCI_INT_CMD_COMPLETE | SDHCI_INT_XFER_COMPLETE);
	}
	if (status_remaining) {
		/* unexpected interrupt: signal-disable, let thread handle it */
		sdhci->int_signal_enable &= ~status_remaining;
	}
	atomic_fetch_or_explicit(&st->int_st, status, memory_order_release);
	sched_queue_list(CURRENT_RUNQUEUE, &st->interrupt_waiters);
}

_Bool sdhci_wait_state_clear(volatile struct sdhci_regs *sdhci, struct sdhci_state *st, u32 mask, timestamp_t timeout, const char *name) {
	timestamp_t start = get_timestamp();
	u32 prests, int_st;
	while ((prests = sdhci->present_state) & mask) {
		if (get_timestamp() - start > timeout) {
			info("%s timeout: reg0x%08"PRIx32" mask0x%08"PRIx32" expected0x%08"PRIx32"\n", name, prests, mask);
			return 0;
		} else if ((int_st = atomic_load_explicit(&st->int_st, memory_order_relaxed)) >> 16) {
			info("%s error: intsts0x%08"PRIx32" prests0x%08"PRIx32"\n", name, int_st, prests);
			return 0;
		}
		printf("sleeping on PRESTS=0x%08"PRIx32"\n", prests);
		call_cc_ptr2_int2(sched_finish_u32, &sdhci->present_state, &st->interrupt_waiters, mask, mask & prests);
	}
	return 1;
}

_Bool sdhci_submit_cmd(volatile struct sdhci_regs *sdhci, struct sdhci_state *st, u32 cmd, u32 arg) {
	u32 state_mask = SDHCI_PRESTS_CMD_INHIBIT | SDHCI_PRESTS_DAT_INHIBIT;
	if (!sdhci_wait_state_clear(sdhci, st, state_mask, USECS(1000), "inhibit")) {return 0;}
	info("[%"PRIuTS"] CMD %04"PRIx32" %08"PRIx32"\n", get_timestamp(), cmd, arg);
	sdhci->arg = arg;
	sdhci->cmd = (u16)cmd;
	return 1;
}
