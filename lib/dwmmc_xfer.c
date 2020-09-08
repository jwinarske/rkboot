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

const u8 iost_u8[NUM_IOST];

enum iost dwmmc_start_request(struct dwmmc_xfer *xfer, u32 block_addr) {
	u8 status = atomic_load_explicit(&xfer->status, memory_order_acquire);
	do {
		if (status == DWMMC_SUBMITTED) {return IOST_TRANSIENT;}
	} while (atomic_compare_exchange_weak_explicit(&xfer->status, &status, DWMMC_CREATING, memory_order_acquire, memory_order_acquire));
	xfer->block_addr = block_addr;
	return IOST_OK;
}

_Bool dwmmc_add_phys_buffer(struct dwmmc_xfer *xfer, phys_addr_t start, phys_addr_t end) {
	assert(atomic_load_explicit(&xfer->status, memory_order_relaxed) == DWMMC_CREATING);
	if (start & 3 || end & 3) {return 0;}
	while (1) {
		if (start >= end) {return 1;}
		if (xfer->desc_size >= xfer->desc_cap) {return 0;}
		info("%s: %"PRIxPHYS": ", __FUNCTION__, start);
		u32 size = 4096;
		if (end - start < 4096) {size = end - start;}
		_Bool first_buf = xfer->second_buf ^ 1;
		size_t desc_idx = xfer->desc_size;
		struct dwmmc_idmac_desc *desc = xfer->desc + desc_idx;
		u32 flags = DWMMC_DES_DISABLE_INTERRUPT | (desc_idx ? DWMMC_DES_OWN : DWMMC_DES_FIRST);
		if (!first_buf) {
			infos("second buffer\n");
			atomic_store_explicit(&desc->control, flags, memory_order_relaxed);
			desc->ptr2 = start;
			desc->sizes |= size << 13;
			xfer->desc_size = desc_idx + 1;
			xfer->second_buf = 0;
		} else if (plat_is_page_aligned(xfer->desc + xfer->desc_size + 1) || xfer->always_use_chaining) {
			infos("chain\n");
			desc->ptr1 = start;
			desc->sizes = size;
			desc->ptr2 = plat_virt_to_phys(xfer->desc + xfer->desc_size);
			atomic_store_explicit(&desc->control, flags | DWMMC_DES_CHAIN_PTR, memory_order_relaxed);
			xfer->desc_size = desc_idx + 1;
			xfer->second_buf = 0;
		} else {
			infos("first buffer\n");
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
	if (xfer->xfer_bytes == 0 || (xfer->desc_size == 0 && !xfer->second_buf)) {return IOST_INVALID;}
	size_t last = xfer->desc_size + xfer->second_buf - 1;
	atomic_store_explicit(&xfer->desc[last].control, atomic_load_explicit(&xfer->desc[last].control, memory_order_relaxed) | DWMMC_DES_LAST | DWMMC_DES_END_OF_RING, memory_order_relaxed);
	u32 ctrl = atomic_load_explicit(&xfer->desc[0].control, memory_order_relaxed);
	atomic_store_explicit(&xfer->desc[0].control, (ctrl & ~DWMMC_DES_DISABLE_INTERRUPT) | DWMMC_DES_OWN, memory_order_release);
	atomic_thread_fence(memory_order_release);
	u8 status = DWMMC_CREATING;
	_Bool success = atomic_compare_exchange_strong_explicit(&xfer->status, &status, DWMMC_SUBMITTED, memory_order_relaxed, memory_order_relaxed);
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
	info("%zu bytes\n", xfer->xfer_bytes);
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

#if 0
void dwmmc_write_descriptors(struct dwmmc_state *state, u8 *buf, u32 bytes_to_submit, u32 first_flag) {
	u16 tail = state->desc_tail, head = state->desc_head;
	u16 desc_mask = ~(0xfffe << state->descriptor_shift);
	u16 next;
	while (bytes_to_submit && (next = (head + 1) & desc_mask) != tail) {
		u32 dma_size = bytes_to_submit < 4096 ? bytes_to_submit : 4096;
		struct dwmmc_idmac_desc *desc = state->desc + head;
		desc->sizes = dma_size;
		desc->ptr1 = (u32)(uintptr_t)buf;
		desc->ptr2 = 0;
		u32 last_flag = bytes_to_submit == 0 ? DWMMC_DES_LAST : 0;
		u32 ring_end_flag = head == desc_mask ? DWMMC_DES_END_OF_RING : 0;
		u32 control = first_flag | last_flag | ring_end_flag | DWMMC_DES_OWN;
		debugs("=");
		spew("writing descriptor@%"PRIx64": %08"PRIx32" %08"PRIx32"\n", (u64)desc, control, buf);
		atomic_store_explicit(&desc->control, control, memory_order_release);
		first_flag = 0;
		buf += dma_size;
		bytes_to_submit -= dma_size;
		head = next;
	}
	state->desc_tail = tail;
	state->bytes_to_submit = bytes_to_submit;
}

/* may only be called if this_xfer_bytes == 0 and starting_xfer was acquired */
void dwmmc_start_transfer(struct dwmmc_state *state, u8 *buf, size_t window_size) {
	u32 bytes_to_submit = window_size & ~(0xffffffff << state->address_shift);
	if (window_size > )
	state->bytes_to_submit = bytes_to_submit;
	volatile struct dwmmc_regs *dwmmc = state->regs;
	dwmmc->bytcnt = bytes_to_submit;
	dwmmc->cmdarg = state->next_lba << state->address_shift;
	dwmmc_write_descriptors(state, buf, bytes_to_submit, DWMMC_DES_FIRST);
	atomic_store_explicit(&state->this_xfer_bytes, bytes_to_submit, memory_order_release);
	atomic_store_explicit(&dwmmc->cmd, state->cmd_template | 18 | DWMMC_R1 | DWMMC_CMD_DATA_EXPECTED | DWMMC_CMD_AUTO_STOP, memory_order_release);
}

void dwmmc_handle_dma_interrupt(struct dwmmc_state *state, u32 status) {
	volatile struct dwmmc_regs *dwmmc = state->regs;
	u16 tail = state->desc_tail, head = state->desc_head;
	u16 desc_mask = ~(0xfffe << state->descriptor_shift);
	u32 bytes_left = atomic_load_explicit(&state->this_xfer_bytes, memory_order_acquire);
	if (!bytes_left) {die("unexpected DMA interrupt\n");}
	u8 *buf = atomic_load_explicit(&state->xfer->window_start, memory_order_relaxed), *buf_end = atomic_load_explicit(&state->xfer->window_end, memory_order_relaxed);
	atomic_thread_fence(memory_order_acquire);
	spew("idmac %"PRIu32"/%"PRIu32" %"PRIx32"/%"PRIx32" status=0x%"PRIx32" cur_desc=0x%08"PRIx32" cur_buf=0x%08"PRIx32"\n", desc_completed, desc_written, finished, buf, status, dwmmc->cur_desc_addr, dwmmc->cur_buf_addr);
	assert(state->desc);
	if (status & DWMMC_IDMAC_INT_RECEIVE) {
		dwmmc->idmac_status = DWMMC_IDMAC_INT_NORMAL | DWMMC_IDMAC_INT_RECEIVE;
	}
	u32 bytes_to_submit = state->bytes_to_submit;
	assert(buf_end > buf && buf_end - buf >= bytes_to_submit);
	while (tail != head) {
		struct dwmmc_idmac_desc *desc = state->desc + tail;
		if (desc->control & DWMMC_DES_OWN) {break;}
		u32 sizes = desc->sizes;
		u32 size = (sizes & 0x1fff) + (sizes >> 13 & 0x1fff);
		assert(size <= bytes_left - bytes_to_submit);
		bytes_left -= size;
		buf += size;
		debugs(".");
		for (u64 addr = desc->ptr1, end = addr + (sizes & 0x1fff); addr < end; addr += 64) {
			__asm__ volatile("dc ivac, %0" : : "r"(addr));
		}
		tail = (tail + 1) & desc_mask;
	}
	assert(tail != head || bytes_to_submit == bytes_left);
	state->desc_tail = tail;
	atomic_store_explicit(&state->xfer->window_start, buf, memory_order_release);

	if (status & (DWMMC_IDMAC_INTMASK_ABNORMAL & ~DWMMC_IDMAC_INT_DESC_UNAVAILABLE)) {
		assert_msg((status >> 13 & 15) <= 1, "IDMAC not quiescent after error\n");
		bytes_left = 0;
		dwmmc->idmac_status = status & DWMMC_IDMAC_INTMASK_ABNORMAL;
	}

	if (!bytes_left) {
		assert(!bytes_to_submit);
		if (buf_end - buf < 1 << state->address_shift) {
			atomic_store_explicit(&state->this_xfer_bytes, 0, memory_order_release);
			/* order the store above before the loads below to prevent deadlock */
			atomic_thread_fence(memory_order_seq_cst);
			buf = atomic_load_explicit(&state->xfer->window_start, memory_order_relaxed);
			buf_end = atomic_load_explicit(&state->xfer->window_end, memory_order_relaxed);
			atomic_thread_fence(memory_order_acquire);
			dwmmc_kick_transfer(state, buf, buf_end);
		} else {
			
		}
	} else {
		atomic_store_explicit(&state->this_xfer_bytes, bytes_left, memory_order_release);
		dwmmc_write_descriptors(state, buf + bytes_left - bytes_to_submit, bytes_to_submit,0);

		if (status & DWMMC_IDMAC_INT_DESC_UNAVAILABLE) {
			if (bytes_to_submit) {
				atomic_signal_fence(memory_order_release);
				dwmmc->poll_demand = 1;
			} else {
				debugs("end of transfer reached\n");
			}
		}
	}
}
#endif
