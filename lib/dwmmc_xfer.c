/* SPDX-License-Identifier: CC0-1.0 */
#include <dwmmc.h>
#include <dwmmc_helpers.h>
#include <dwmmc_regs.h>
#include <assert.h>
#include <stdatomic.h>
#include <aarch64.h>

enum {
	DWMMC_CREATING = NUM_IOST,
	DWMMC_SUBMITTED,
};

enum iost dwmmc_start_request(struct dwmmc_xfer *xfer, u32 block_addr) {
	u8 status = atomic_load_explicit(&xfer->status, memory_order_acquire);
	do {
		if (status == DWMMC_SUBMITTED) {return IOST_TRANSIENT;}
	} while (atomic_compare_exchange_weak_explicit(&xfer->status, &status, DWMMC_CREATING, memory_order_acquire, memory_order_acquire));
	xfer->block_addr = block_addr;
	xfer->desc_size = 0;
	xfer->xfer_bytes = 0;
	xfer->second_buf = 0;
	return IOST_OK;
}

_Bool dwmmc_add_phys_buffer(struct dwmmc_xfer *xfer, phys_addr_t start, phys_addr_t end) {
	assert(atomic_load_explicit(&xfer->status, memory_order_relaxed) == DWMMC_CREATING);
	if (start & 3 || end & 3) {return 0;}
	while (1) {
		if (start >= end) {return 1;}
		if (xfer->desc_size >= xfer->desc_cap) {return 0;}
		debug("%s: %"PRIxPHYS": ", __FUNCTION__, start);
		u32 size = 4096;
		if (end - start < 4096) {size = end - start;}
		_Bool first_buf = xfer->second_buf ^ 1;
		size_t desc_idx = xfer->desc_size;
		struct dwmmc_idmac_desc *desc = xfer->desc + desc_idx;
		u32 flags = DWMMC_DES_DISABLE_INTERRUPT | (desc_idx ? DWMMC_DES_OWN : DWMMC_DES_FIRST);
		if (!first_buf) {
			debugs("second buffer\n");
			atomic_store_explicit(&desc->control, flags, memory_order_relaxed);
			desc->ptr2 = start;
			desc->sizes |= size << 13;
			xfer->desc_size = desc_idx + 1;
			xfer->second_buf = 0;
		} else if (plat_is_page_aligned(xfer->desc + xfer->desc_size + 1) || xfer->always_use_chaining) {
			debugs("chain\n");
			desc->ptr1 = start;
			desc->sizes = size;
			desc->ptr2 = plat_virt_to_phys(xfer->desc + xfer->desc_size);
			atomic_store_explicit(&desc->control, flags | DWMMC_DES_CHAIN_PTR, memory_order_relaxed);
			xfer->desc_size = desc_idx + 1;
			xfer->second_buf = 0;
		} else {
			debugs("first buffer\n");
			atomic_store_explicit(&desc->control, flags, memory_order_relaxed);
			desc->sizes = size;
			desc->ptr1 = start;
			xfer->second_buf = 1;
		}
		start += size;
		xfer->xfer_bytes += size;
	}
}

enum iost dwmmc_start_xfer(struct dwmmc_state *state, struct dwmmc_xfer *xfer) {
	debug("dwmmc_start_xfer: %zu bytes\n", xfer->xfer_bytes);
	if (xfer->xfer_bytes == 0 || (xfer->desc_size == 0 && !xfer->second_buf)) {return IOST_INVALID;}
	size_t last = xfer->desc_size + xfer->second_buf - 1;
	u32 ctrl = atomic_load_explicit(&xfer->desc[last].control, memory_order_relaxed);
	atomic_store_explicit(&xfer->desc[last].control, (ctrl & ~DWMMC_DES_DISABLE_INTERRUPT) | DWMMC_DES_LAST | DWMMC_DES_END_OF_RING, memory_order_relaxed);
	atomic_store_explicit(&xfer->desc[0].control, atomic_load_explicit(&xfer->desc[0].control, memory_order_relaxed) | DWMMC_DES_OWN, memory_order_release);
	atomic_thread_fence(memory_order_release);
	u8 status = DWMMC_CREATING;
	_Bool success = atomic_compare_exchange_strong_explicit(&xfer->status, &status, DWMMC_SUBMITTED, memory_order_relaxed, memory_order_relaxed);
    (void)success;
	assert(success);
	struct dwmmc_xfer *prev_xfer = 0;
	success = atomic_compare_exchange_strong_explicit(&state->active_xfer, &prev_xfer, xfer, memory_order_relaxed, memory_order_relaxed);
	assert(success);
	atomic_thread_fence(memory_order_seq_cst);
	dsb_sy();
	volatile struct dwmmc_regs *dwmmc = state->regs;
	dwmmc->bmod = DWMMC_BMOD_SOFT_RESET;
	dwmmc->ctrl = DWMMC_CTRL_INT_ENABLE | DWMMC_CTRL_DMA_RESET;
	dsb_sy();
	while (dwmmc->ctrl & DWMMC_CTRL_DMA_RESET) {__asm__("yield");}
	while (dwmmc->bmod & DWMMC_BMOD_SOFT_RESET) {__asm__("yield");}
	dwmmc->fifoth = 0x307f0080;
	dwmmc->ctrl = DWMMC_CTRL_INT_ENABLE | DWMMC_CTRL_USE_IDMAC;
	dwmmc->bmod = DWMMC_BMOD_IDMAC_ENABLE;
	dwmmc->desc_list_base = (u32)xfer->desc_addr;
	dwmmc->idmac_int_enable = DWMMC_IDMAC_INTMASK_ALL;
	dwmmc->intmask = DWMMC_INTMASK_DMA;
	assert(xfer->xfer_bytes % 512 == 0);
	dwmmc->blksiz = 512;
	dwmmc->bytcnt = xfer->xfer_bytes;
	dwmmc->cmdarg = xfer->block_addr;
	enum iost res = dwmmc_wait_cmd(state,
		18 | DWMMC_R1 | DWMMC_CMD_AUTO_STOP | DWMMC_CMD_DATA_EXPECTED | state->cmd_template,
		get_timestamp()
	);
	if (res != IOST_OK) {return res;}
	return IOST_OK;
}

enum iost dwmmc_wait_xfer(struct dwmmc_state *state, struct dwmmc_xfer *xfer) {
	u8 status;
	while ((status = atomic_load_explicit(&xfer->status, memory_order_acquire)) >= NUM_IOST) {
		assert(status == DWMMC_SUBMITTED);
		call_cc_ptr2_int2(sched_finish_u8, &xfer->status, &state->interrupt_waiters, 0xff, status);
		spews("dwmmc_wait_xfer: woke up");
	}
	return status;
}
