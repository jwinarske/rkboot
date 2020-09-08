/* SPDX-License-Identifier: CC0-1.0 */
#include <dwmmc.h>
#include <dwmmc_helpers.h>
#include <inttypes.h>
#include <assert.h>
#include <stdatomic.h>

#include <die.h>
#include <log.h>
#include <timer.h>
#include <runqueue.h>
#include <iost.h>

void dwmmc_irq(struct dwmmc_state *state) {
	volatile struct dwmmc_regs *dwmmc = state->regs;
	u32 rintsts = dwmmc->rintsts, idmac_st = dwmmc->idmac_status, status = dwmmc->status;
	debugs("?");
	spew("[%"PRIuTS"] DWMMC IRQ %08"PRIx32" %08"PRIx32" %08"PRIx32"\n", get_timestamp(), rintsts, idmac_st, status);
	if (rintsts) {
		dwmmc->rintsts = rintsts;
	}
	if (idmac_st & DWMMC_IDMAC_INTMASK_ALL) {
		dwmmc->idmac_status = idmac_st & DWMMC_IDMAC_INTMASK_ALL;
	}
	if ((status & (DWMMC_STATUS_DATA_BUSY | DWMMC_STATUS_DATA_SM_BUSY)) == 0) {
		rintsts |= DWMMC_INT_DATA_NO_BUSY;
	}
	atomic_thread_fence(memory_order_release);
	rintsts |= atomic_fetch_or_explicit(&state->int_st, rintsts, memory_order_relaxed);
	u32 idmac_st_val = atomic_load_explicit(&state->idmac_st, memory_order_relaxed);
	u32 idmac_st_acc;
	do {
		idmac_st_acc = idmac_st_val & ~DWMMC_IDMAC_FSM_MASK;
		idmac_st_acc |= idmac_st;
	} while (!atomic_compare_exchange_weak_explicit(&state->idmac_st, &idmac_st_val, idmac_st_acc, memory_order_release, memory_order_relaxed));
	if ((idmac_st_acc >> 13 & 15) < 2 && (~status & DWMMC_STATUS_DATA_SM_BUSY)) {
		struct dwmmc_xfer *xfer = atomic_load_explicit(&state->active_xfer, memory_order_acquire);
		if (xfer) {
			spews("ending transfer\n");
			enum iost st = rintsts & DWMMC_ERROR_INT_MASK || idmac_st_acc & DWMMC_IDMAC_INTMASK_ABNORMAL ? IOST_GLOBAL : IOST_OK;
			atomic_store_explicit(&state->active_xfer, 0, memory_order_relaxed);
			atomic_store_explicit(&xfer->status, st, memory_order_release);
		}
	}
	sched_queue_list(CURRENT_RUNQUEUE, &state->interrupt_waiters);
}

enum iost dwmmc_wait_cmd(struct dwmmc_state *st, u32 cmd, timestamp_t start) {
	u32 int_st, cleared_int_st, mask = DWMMC_INT_CMD_DONE;
	if (cmd & DWMMC_CMD_SYNC_DATA) {mask |= DWMMC_INT_DATA_NO_BUSY | DWMMC_INT_DATA_TRANSFER_OVER;}
	int_st = atomic_load_explicit(&st->int_st, memory_order_acquire);
	do {
		spew("[%"PRIuTS"] dwmmc: cmd %08"PRIx32" int_st0x%08"PRIx32"\n", get_timestamp(), cmd, int_st);
		if (int_st & DWMMC_ERROR_INT_MASK) {
			info("dwmmc: tried submitting command %08"PRIx32" in error state %08"PRIx32"\n", cmd, int_st);
			return IOST_LOCAL;
		}
		if ((int_st & mask) != mask) {
			info("command %08"PRIx32" not allowed in state %08"PRIx32"\n", cmd, int_st);
			return IOST_TRANSIENT;
		}
		cleared_int_st = int_st & ~(DWMMC_INT_DATA_NO_BUSY | DWMMC_INT_CMD_DONE);
		if (cmd & DWMMC_CMD_DATA_EXPECTED) {cleared_int_st &= ~DWMMC_INT_DATA_TRANSFER_OVER;}
	} while(!atomic_compare_exchange_weak_explicit(&st->int_st, &int_st, cleared_int_st, memory_order_acquire, memory_order_acquire));
	volatile _Atomic u32 *reg = &st->regs->cmd;
#ifdef SPEW_MSG
	timestamp_t issue_time = get_timestamp();
#endif
	atomic_store_explicit(reg, cmd, memory_order_release);
	while (atomic_load_explicit(reg, memory_order_acquire) & DWMMC_CMD_START) {
		if (get_timestamp() - start > USECS(10000)) {
			dwmmc_print_status(st->regs, "cmd ack timeout ");
			return IOST_GLOBAL;
		}
		sched_yield();
	}
	spew("hardware lock released after %"PRIuTS"\n", get_timestamp() - issue_time);
	return IOST_OK;
}
enum iost dwmmc_wait_cmd_done_postissue(struct dwmmc_state *st, timestamp_t start, timestamp_t raw_timeout) {
	u32 int_st;
	while (((int_st = atomic_load_explicit(&st->int_st, memory_order_acquire)) & (DWMMC_INT_CMD_DONE | DWMMC_ERROR_INT_MASK)) == 0) {
		if (get_timestamp() - start > raw_timeout) {
			info("timed out waiting for command completion, rintsts=0x%"PRIx32" status=0x%"PRIx32"\n", int_st, st->regs->status);
			return IOST_GLOBAL;
		}
		call_cc_ptr2_int2(sched_finish_u32, &st->int_st, &st->interrupt_waiters, 0xffffffff, 0);
		spew("woke up\n");
	}
	if (int_st & DWMMC_ERROR_INT_MASK) {return IOST_LOCAL;}
	return IOST_OK;
}

_Bool dwmmc_wait_data_idle(struct dwmmc_state *st, timestamp_t start, timestamp_t raw_timeout) {
	u32 int_st;
	while (~(int_st = atomic_load_explicit(&st->int_st, memory_order_acquire)) & DWMMC_INT_DATA_NO_BUSY) {
		if (get_timestamp() - start > raw_timeout) {
			info("timed out waiting for data idle, rintsts=0x%"PRIx32" status=0x%"PRIx32"\n", int_st, st->regs->status);
			return 0;
		}
		call_cc_ptr2_int2(sched_finish_u32, &st->int_st, &st->interrupt_waiters, DWMMC_INT_DATA_NO_BUSY, 0);
	}
	return 1;
}

void dwmmc_print_status(volatile struct dwmmc_regs *dwmmc, const char *context) {
	info("[%"PRIuTS"] %sresp0=0x%08"PRIx32" status=0x%08"PRIx32" rintsts=0x%04"PRIx32"\n", get_timestamp(), context, dwmmc->resp[0], dwmmc->status, dwmmc->rintsts);
}
