/* SPDX-License-Identifier: CC0-1.0 */
#include <nvme_regs.h>
#include <nvme.h>
#include <stdatomic.h>
#include <assert.h>
#include <string.h>

#include <iost.h>
#include <log.h>
#include <timer.h>
#include <byteorder.h>

enum iost nvme_reset_xfer(struct nvme_xfer *xfer) {
	u16 status = atomic_load_explicit(&xfer->req.status, memory_order_acquire);
	do {
		if (status == NVME_SUBMITTED) {return IOST_TRANSIENT;}
	} while (atomic_compare_exchange_weak_explicit(&xfer->req.status, &status, NVME_CREATING, memory_order_acquire, memory_order_acquire));
	xfer->prp_size = 0;
	xfer->first_prp_entry = xfer->last_prp_entry = 0;
	return IOST_OK;
}

_Bool nvme_add_phys_buffer(struct nvme_xfer *xfer, phys_addr_t start, phys_addr_t end) {
	assert(atomic_load_explicit(&xfer->req.status, memory_order_relaxed) == NVME_CREATING);
	assert(!(start & 3 || end & 3 || end < start));
	const phys_addr_t page_mask = (1 << PLAT_PAGE_SHIFT) - 1;
	if (!xfer->prp_size) {
		xfer->first_prp_entry = start;
		xfer->prp_size = 1;
		if (start >> PLAT_PAGE_SHIFT >= end >> PLAT_PAGE_SHIFT) {
			debugs("first page\n");
			xfer->xfer_bytes = end - start;
			return 1;
		} else {
			/* run to the end of the page */
			xfer->xfer_bytes = (1 << PLAT_PAGE_SHIFT) - (start & page_mask);
			start = (start & ~page_mask) + (1 << PLAT_PAGE_SHIFT);
		}
	} else {
		/* make sure the last buffer went to the end of the page */
		assert((xfer->xfer_bytes & page_mask) == (1 << PLAT_PAGE_SHIFT) - (xfer->first_prp_entry & page_mask));
	}
	/* all but the first buffer must be page-aligned */
	assert(!(start & page_mask));
	if (start >= end) {return 1;}
	if (xfer->prp_size < 2) {
		debugs("second page\n");
		assert(xfer->prp_size == 1);
		xfer->last_prp_entry = start;
		xfer->prp_size = 2;
		if (end - start <= 1 << PLAT_PAGE_SHIFT) {
			xfer->xfer_bytes += end - start;
			return 1;
		}
		start += 1 << PLAT_PAGE_SHIFT;
		xfer->xfer_bytes += 1 << PLAT_PAGE_SHIFT;
	}
	assert(xfer->prp_size >= 2);
	assert(start < end);
	while (1) {
		size_t idx = xfer->prp_size - 2;
		if (idx >= xfer->prp_cap - 1) {return 0;}
		if (plat_is_page_aligned(xfer->prp_list + idx + 1)) {
			xfer->prp_list[idx] = to_le64(plat_virt_to_phys(xfer->prp_list + idx + 1));
			idx += 1;
			if (idx >= xfer->prp_cap - 1) {return 0;}
		}
		xfer->prp_list[idx] = to_le64(xfer->last_prp_entry);
		xfer->prp_size = idx + 3;
		xfer->last_prp_entry = start;
		if (end - start <= (1 << PLAT_PAGE_SHIFT)) {
			xfer->xfer_bytes += end - start;
			return 1;
		}
		start += 1 << PLAT_PAGE_SHIFT;
		xfer->xfer_bytes += 1 << PLAT_PAGE_SHIFT;
	}
}

_Bool nvme_emit_read(struct nvme_state *st, struct nvme_sq *sq, struct nvme_xfer *xfer, u32 nsid, u64 lba) {
	if (xfer->xfer_bytes & ((1 << st->lba_shift) - 1)) {return 0;}
	struct nvme_cmd *cmd = sq->buf + sq->tail;
	memset(cmd, 0, sizeof(*cmd));
	cmd->opc = NVME_NVM_READ;
	cmd->fuse_psdt = NVME_PSDT_PRP;
	cmd->nsid = to_le32(nsid);
	cmd->dptr[0] = to_le64(xfer->first_prp_entry);
	if (xfer->prp_size == 2) {
		cmd->dptr[1] = to_le32(xfer->last_prp_entry);
	} else if (xfer->prp_size > 2) {
		cmd->dptr[1] = to_le32(xfer->prp_list_addr);
		/* add last entry at the end, even if it is the last on the page */
		xfer->prp_list[xfer->prp_size - 2] = to_le64(xfer->last_prp_entry);
	}
	cmd->cmd_spec[0] = to_le64(lba);
	cmd->dw12 = (xfer->xfer_bytes >> st->lba_shift) - 1;
	return 1;
}

enum iost nvme_read_wait(struct nvme_state *st, u16 sqid, struct nvme_xfer *xfer, u32 nsid, u64 lba) {
	if (!nvme_emit_read(st, st->sq + sqid, xfer, nsid, lba)) {return IOST_INVALID;}

	timestamp_t t_submit = get_timestamp();
	enum iost res = wait_single_command(st, sqid);
	if (res != IOST_OK) {return res;}
	info("read finished after %"PRIuTS" μs\n", (get_timestamp() - t_submit) / TICKS_PER_MICROSECOND);
	return IOST_OK;
}
