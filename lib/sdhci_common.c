/* SPDX-License-Identifier: CC0-1.0 */
#include <inttypes.h>
#include <stdatomic.h>
#include <assert.h>

#include <sdhci.h>
#include <sdhci_regs.h>
#include <log.h>
#include <timer.h>
#include <iost.h>

static _Bool handle_sdma(volatile struct sdhci_regs *sdhci, struct sdhci_state *st) {
	u8 *buf = atomic_load_explicit(&st->sdma_buf, memory_order_acquire);
	spew("SDMA interrupt: %"PRIx64"\n", (u64)buf);
	if (!buf) {return 0;}
	assert((uintptr_t)buf <= 0xffffffff && ((uintptr_t)buf & (st->sdma_buffer_size - 1)) == 0 && ((uintptr_t)st->buf_end & (st->sdma_buffer_size - 1)) == 0);
	buf += st->sdma_buffer_size;
	if (buf >= st->buf_end) {return 0;}
	sdhci->normal_int_st = SDHCI_INT_DMA;
	sdhci->system_addr = (u32)(uintptr_t)buf;
	atomic_store_explicit(&st->sdma_buf, buf, memory_order_release);
	return 1;
}

void sdhci_irq(struct sdhci_state *st) {
	volatile struct sdhci_regs *sdhci = st->regs;
	debugs("?");
	fflush(stdout);
	u32 status = sdhci->int_st, status_remaining = status;
	spew("SDHCI IRQ 0x%08"PRIx32"\n", status);
	u16 insta_ack = SDHCI_INT_CMD_COMPLETE | SDHCI_INT_XFER_COMPLETE | SDHCI_INT_BUFFER_READ_READY | SDHCI_INT_BUFFER_WRITE_READY;
	if (status & insta_ack) {
		sdhci->normal_int_st = status & insta_ack;
		if (status & SDHCI_INT_XFER_COMPLETE) {
			atomic_store_explicit(&st->sdma_buf, 0, memory_order_release);
		}
		status_remaining &= ~(SDHCI_INT_CMD_COMPLETE | SDHCI_INT_XFER_COMPLETE);
	}
	if (status & SDHCI_INT_DMA) {
		if (handle_sdma(sdhci, st)) {status_remaining &= ~SDHCI_INT_DMA;}
	}
	if (status_remaining) {
		/* unexpected interrupt: signal-disable, let thread handle it */
		sdhci->int_signal_enable &= ~status_remaining;
	}
	atomic_fetch_or_explicit(&st->int_st, status, memory_order_release);
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
