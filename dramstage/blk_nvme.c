/* SPDX-License-Identifier: CC0-1.0 */
#include <rk3399.h>
#include <rk3399/dramstage.h>
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include <pci_regs.h>
#include <rkpcie_regs.h>
#include <nvme_regs.h>
#include <log.h>
#include <runqueue.h>
#include <timer.h>
#include <aarch64.h>
#include <mmu.h>
#include <dump_mem.h>
#include <iost.h>
#include <plat.h>
#include <byteorder.h>

struct rkpcie_ob_desc {
	u32 addr[2];
	u32 desc[4];
	u32 padding[2];
};

struct rkpcie_addr_xlation {
	struct rkpcie_ob_desc ob[33];
	u8 padding[0x800 - 0x420];
	u32 rc_bar_addr[3][2];
	u32 padding1[4];
	u32 link_down_indication;
	u32 ep_bar_addr[7][2];
};
CHECK_OFFSET(rkpcie_addr_xlation, link_down_indication, 0x828);

enum {
	WTBUF_ASQ,
	WTBUF_IOSQ,
	WTBUF_PRP,
	NUM_WTBUF
};

enum {
	UBUF_ACQ,
	UBUF_IOCQ,
	UBUF_IDCTL,
	UBUF_IDNS,
	UBUF_DATA,
	NUM_UBUF
};

static UNINITIALIZED _Alignas(1 << PLAT_PAGE_SHIFT) u8 wt_buf[NUM_WTBUF][1 << PLAT_PAGE_SHIFT];
static UNINITIALIZED _Alignas(1 << PLAT_PAGE_SHIFT) u8 uncached_buf[NUM_UBUF][1 << PLAT_PAGE_SHIFT];

static _Bool finish_pcie_link() {
	timestamp_t start = get_timestamp(), gen1_trained, retrained;
	volatile u32 *pcie_client = regmap_base(REGMAP_PCIE_CLIENT);
	while (1) {
		u32 basic_status1 = pcie_client[RKPCIE_CLIENT_BASIC_STATUS+1];
		info("PCIe link status %08"PRIx32" %08"PRIx32"\n", pcie_client[RKPCIE_CLIENT_DEBUG_OUT+0], basic_status1);
		gen1_trained = get_timestamp();
		if ((basic_status1 >> 20 & 3) == 3) {break;}
		if (gen1_trained - start > MSECS(500)) {
			info("timed out waiting for PCIe link\n");
			return 0;
		}
		usleep(1000);
	}
	volatile u32 *pcie_mgmt = regmap_base(REGMAP_PCIE_MGMT);
	u32 plc0 = pcie_mgmt[RKPCIE_MGMT_PLC0];
	if (~plc0 & RKPCIE_MGMT_PLC0_LINK_TRAINED) {return 0;}
	info("trained %"PRIu32"x link (waited %"PRIuTS" μs)\n", 1 << (plc0 >> 1 & 3), (gen1_trained - start) / TICKS_PER_MICROSECOND);
	volatile u32 *pcie_rcconf = regmap_base(REGMAP_PCIE_RCCONF);
	volatile u32 *lcs = pcie_rcconf + RKPCIE_RCCONF_PCIECAP + PCIECAP_LCS;
	for_range(i, 0, 0x138 >> 2) {
		spew("%03"PRIx32": %08"PRIx32"\n", i*4, pcie_rcconf[i]);
	}
	info("LCS: %08"PRIx32"\n", *lcs);
	*lcs |= PCIECAP_LCS_RL;
	u32 lcs_val;
	while (1) {
		lcs_val = *lcs;
		info("PCIe link status %08"PRIx32" %08"PRIx32"\n", pcie_client[RKPCIE_CLIENT_DEBUG_OUT+0], lcs_val);
		retrained = get_timestamp();
		if (lcs_val & PCIECAP_LCS_LBMS) {break;}
		if (retrained - gen1_trained > MSECS(500)) {
			info("timed out waiting for Gen2 retrain\n");
			return 1;	/* fallback is not an error */
		}
		usleep(1000);
	}
	info("link speed after retrain: %"PRIu32" (waited %"PRIuTS" μs)\n", lcs_val >> 16 & 15, (retrained - gen1_trained) / TICKS_PER_MICROSECOND);
	/* could power down unused lanes here; we assume full width, so we don't waste code size implementing it */
	return 1;
}

enum {
	NVME_CREATING = 0,
	NVME_SUBMITTED = 2,
};

struct nvme_req {
	u32 data[2];
	u16 aux;
	_Atomic(u16) status;
};

struct nvme_sq {
	volatile _Atomic u32 *doorbell;
	struct nvme_cmd *buf;
	u16 size, tail, cq;
	_Atomic(u16) head;
	u16 max_cid;
	_Atomic(struct nvme_req *) *cmd;
};

struct nvme_cq {
	volatile _Atomic u32 *doorbell;
	struct nvme_completion *buf;
	unsigned phase : 1;
	u16 size, pos;
};

/* info and state for a single controller with a single namespace … we don't handle more enterprisey setups here */
struct nvme_state {
	volatile struct nvme_regs *regs;
	u64 cap;
	u32 cfg;
	u32 rtd3e;
	u8 lba_shift;
	u16 num_iosq, num_iocq;
	struct nvme_sq *sq;
	struct nvme_cq *cq;
};

enum iost nvme_init(struct nvme_state *st) {
	volatile struct nvme_regs *nvme = st->regs;
	u64 nvme_cap = st->cap = nvme->capabilities;
	u32 cmbsz = nvme->cmb_size;
	info("NVMe CAP: %016"PRIx64" CMBSZ: %08"PRIx32"\n", nvme_cap, cmbsz);
	if (~nvme_extr_cap_css(nvme_cap) & 1 << NVME_CMDSET_NVM) {
		infos("Controller does not support NVM command set\n");
		return IOST_INVALID;
	}
	if (nvme_cap & NVME_CAP_MPSMIN_MASK) {
		infos("Controller does not support 4k pages\n");
		return IOST_INVALID;
	}
	if (nvme_extr_cap_mqes(nvme_cap) < 3) {
		infos("Controller does not support 4+ entry queues\n");
		return IOST_INVALID;
	}
	timestamp_t timeout = nvme_extr_cap_to(nvme_cap) * MSECS(500), t_wait_idle = get_timestamp(), t_idle;
	u32 cfg = 0;
	atomic_store_explicit(&nvme->config, 0, memory_order_relaxed);
	atomic_thread_fence(memory_order_seq_cst);
	while (1) {
		u32 status = atomic_load_explicit(&nvme->status, memory_order_acquire);
		info("CSTS: %08"PRIx32"\n", status);
		t_idle = get_timestamp();
		if (~status & NVME_CSTS_RDY) {break;}
		if (t_idle - t_wait_idle > timeout) {
			infos("Controller idle timeout\n");
			return IOST_GLOBAL;
		}
		usleep(1000);
	}
	u32 dstrd = 4 << nvme_extr_cap_dstrd(st->cap);
	st->num_iocq = 0;
	st->num_iosq = 0;
	st->cq[0].doorbell = (_Atomic u32 *)((uintptr_t)nvme + 0x1000 + dstrd);
	st->cq[0].phase = 1;
	st->cq[0].pos = 0;
	memset(st->cq[0].buf, 0, sizeof(struct nvme_completion) * ((size_t)st->cq[0].size + 1));
	st->sq[0].doorbell = (_Atomic u32 *)((uintptr_t)nvme + 0x1000);
	st->sq[0].tail = 0;
	st->sq[0].cq = 0;
	atomic_store_explicit(&st->sq[0].head, 0, memory_order_relaxed);
	nvme->adminq_attr = st->cq[0].size << 16 | st->sq[0].size;
	nvme->adminsq_base = (u64)plat_virt_to_phys(st->sq[0].buf);
	nvme->admincq_base = (u64)plat_virt_to_phys(st->cq[0].buf);
	cfg |= NVME_CMDSET_NVM << NVME_CC_CSS_SHIFT | 0 << NVME_CC_MPS_SHIFT | 0 << NVME_CC_AMS_SHIFT;
	atomic_store_explicit(&nvme->config, cfg, memory_order_relaxed);
	cfg |= NVME_CC_EN;
	/* release because clearing the ACQ memory has to be cleared before enabling */
	atomic_store_explicit(&nvme->config, cfg, memory_order_release);
	atomic_thread_fence(memory_order_seq_cst);
	timestamp_t t_ready;
	while (1) {
		u32 status = atomic_load_explicit(&nvme->status, memory_order_acquire);
		info("CSTS: %08"PRIx32"\n", status);
		t_ready = get_timestamp();
		if (status & NVME_CSTS_RDY) {break;}
		if (t_ready - t_idle > timeout) {
			info("Controller ready timeout after %"PRIuTS" μs\n", (t_ready - t_idle) / TICKS_PER_MICROSECOND);
			return IOST_GLOBAL;
		}
		usleep(1000);
	}
	info("Controller ready after %"PRIuTS"  μs\n", (t_ready - t_idle) / TICKS_PER_MICROSECOND);
	st->cfg = cfg;
	st->rtd3e = 1000000;	/* according to spec, should wait 1s for shutdown if RTD3E not known */
	return IOST_OK;
}

static void nvme_dump_completion(struct nvme_completion *cqe, u16 status) {
	status = (status & 0xf000) | (status >> 1 & 0x07ff);
	info("%04"PRIx16" %04"PRIx16"%04"PRIx16" %04"PRIx16" %"PRIx32" %"PRIx32"\n", status, cqe->sqid, cqe->cid, cqe->sqhd, cqe->cmd_spec, cqe->reserved);
}

static enum iost nvme_process_cqe(volatile struct nvme_state *st, u16 cqid) {
	struct nvme_cq *cq = st->cq + cqid;
	u16 pos = cq->pos;
	_Bool phase = cq->phase;
	struct nvme_completion *cqe = cq->buf + pos;
	u16 cmd_st;
	if (((cmd_st = from_le16(atomic_load_explicit(&cqe->status, memory_order_acquire))) & 1) != phase) {return IOST_TRANSIENT;}
	if (cqe->sqid > st->num_iosq) {
		infos("unexpected SQID: ");
		nvme_dump_completion(cqe, cmd_st);
		return IOST_GLOBAL;
	}
	struct nvme_sq *sq = st->sq + cqe->sqid;
	if (sq->cq != cqid) {
		infos("completion for unassociated SQ: ");
		nvme_dump_completion(cqe, cmd_st);
		return IOST_GLOBAL;
	}
	if (!sq->cmd || sq->max_cid < cqe->cid) {
		infos("command ID out of range: ");
		nvme_dump_completion(cqe, cmd_st);
		return IOST_GLOBAL;
	}
	struct nvme_req *cmd = atomic_load_explicit(sq->cmd + cqe->cid, memory_order_acquire);
	nvme_dump_completion(cqe, cmd_st);
	cmd->data[0] = from_le32(cqe->cmd_spec);
	cmd->data[1] = from_le32(cqe->reserved);
	cmd->aux = 0;
	u16 sqhd = cqe->sqhd;
	atomic_thread_fence(memory_order_release);
	atomic_store_explicit(sq->cmd + cqe->cid, 0, memory_order_relaxed);
	atomic_store_explicit(&cmd->status, cmd_st | 1, memory_order_relaxed);
	atomic_store_explicit(&sq->head, sqhd, memory_order_relaxed);

	if (pos < cq->size) {
		cq->pos = ++pos;
	} else {
		cq->pos = pos = 0;
		cq->phase = !phase;
	}
	atomic_store_explicit(cq->doorbell, pos, memory_order_relaxed);
	return IOST_OK;
}

static enum iost wait_single_command(struct nvme_state *st, u16 sqid) {
	assert(sqid <= st->num_iosq);
	struct nvme_sq *sq = st->sq + sqid;
	u16 tail = sq->tail, head = atomic_load_explicit(&sq->head, memory_order_acquire);
	u16 next = tail == sq->size ? 0 : tail + 1;
	if (next == head) {	/* TODO: wait on full queue */
		infos("queue full\n");
		return IOST_TRANSIENT;
	}
	struct nvme_req req;
	memset(&req, 0, sizeof(req));
	atomic_store_explicit(&req.status, NVME_SUBMITTED, memory_order_relaxed);
	u16 cid;
	for_range(i, 0, (u32)sq->max_cid + 1) {
		struct nvme_req *ptr = 0;
		if (atomic_compare_exchange_strong_explicit(sq->cmd + i, &ptr, &req, memory_order_release, memory_order_relaxed)) {
			cid = i;
			goto cid_assigned;
		}
	}
	infos("no free command ID found\n");
	return IOST_TRANSIENT;
cid_assigned:
	sq->buf[tail].cid = to_le16(cid);

	sq->tail = next;
	atomic_store_explicit(sq->doorbell, next, memory_order_release);
	u16 cmd_st;
	volatile struct nvme_regs *nvme = st->regs;
	while (1) {
		for_range(cqid, 0, (u32)st->num_iocq + 1) {
			nvme_process_cqe(st, cqid);
		}
		cmd_st = atomic_load_explicit(&req.status, memory_order_acquire);
		if (cmd_st != NVME_SUBMITTED) {break;}
		u32 status = nvme->status;
		info("waiting cid%04"PRIx16" st%08"PRIx32"\n", cid, status);
		if (status & NVME_CSTS_CFS) {return IOST_GLOBAL;}
		usleep(100);
	}
	if (cmd_st & 0xfffe) {
		info("completion error: %03"PRIx16" %08"PRIx32" %"PRIx32"\n",
			(cmd_st & 0xf000) | (cmd_st >> 1 & 0x07ff),
			req.data[0], req.data[1]
		);
		return IOST_LOCAL;
	}
	return IOST_OK;
}

enum iost nvme_init_queues(struct nvme_state *st, u16 num_iocq, u16 num_iosq) {
	volatile struct nvme_regs *nvme = st->regs;
	assert(num_iocq && num_iosq);
	struct nvme_cmd *cmd;
	cmd = st->sq[0].buf + st->sq[0].tail;
	memset(cmd, 0, sizeof(*cmd));
	cmd->opc = NVME_ADMIN_IDENTIFY;
	const u8 *idctl = uncached_buf[UBUF_IDCTL];
	cmd->dptr[0] = (u64)(uintptr_t)idctl;
	cmd->dptr[1] = 0;
	cmd->dw10 = NVME_IDENTIFY_CONTROLLER;
	enum iost res = wait_single_command(st, 0);
	if (res != IOST_OK) {return res;}

	dump_mem(idctl, 0x1000);
	u32 rtd3e = nvme_extr_idctl_rtd3e(idctl);
	info("RTD3E: %"PRIu32" μs\n", rtd3e);
	if (!rtd3e) {st->rtd3e = rtd3e;}
	u32 num_ns = nvme_extr_idctl_nn(idctl);
	if (!num_ns) {
		infos("controller has no valid NSIDs\n");
		return IOST_INVALID;
	}

	cmd = st->sq[0].buf + st->sq[0].tail;
	memset(cmd, 0, sizeof(*cmd));
	cmd->opc = NVME_ADMIN_IDENTIFY;
	cmd->nsid = 1;
	const u8 *idns = uncached_buf[UBUF_IDNS];
	cmd->dptr[0] = (u64)(uintptr_t)idns;
	cmd->dptr[1] = 0;
	cmd->dw10 = NVME_IDENTIFY_NS;
	res = wait_single_command(st, 0);
	if (res != IOST_OK) {return res;}

	dump_mem(idns, 0x180);
	u64 ns_size = nvme_extr_idns_nsze(idns);
	u8 flbas = nvme_extr_idns_flbas(idns);
	const u8 *lbaf = idns + 128 + 4 * (flbas & 15);
	if (lbaf[0] || lbaf[1]) {
		infos("formatted LBAF has metadata, don't know how to deal with it\n");
		return IOST_INVALID;
	}
	if (lbaf[2] > 12 || lbaf[2] < 9) {
		info("unexpected LBA size 1 << %"PRIu8"\n", lbaf[2]);
		return IOST_INVALID;
	}
	st->lba_shift = lbaf[2];
	u32 block_size = 1 << lbaf[2];
	info("namespace has %"PRIu64" (0x%"PRIx64") %"PRIu32"-byte sectors\n", ns_size, ns_size, block_size);
	if ((nvme_extr_idctl_sqes(idctl) & 15) > 6 || (nvme_extr_idctl_cqes(idctl) & 15) > 4) {
		info("unsupported queue entry sizes\n");
		return IOST_INVALID;
	}

	nvme->config = st->cfg |= 4 << NVME_CC_IOCQES_SHIFT | 6 << NVME_CC_IOSQES_SHIFT;

	cmd = st->sq[0].buf + st->sq[0].tail;
	memset(cmd, 0, sizeof(*cmd));
	cmd->opc = NVME_ADMIN_SET_FEATURES;
	cmd->dw10 = NVME_FEATURE_NUM_QUEUES;
	cmd->dw11 = to_le32((num_iocq - 1) << 16 | (num_iosq - 1));
	res = wait_single_command(st, 0);
	if (res != IOST_OK) {return res;}

	u32 dstrd = 4 << nvme_extr_cap_dstrd(st->cap);
	st->cq[1].doorbell = (_Atomic u32 *)((uintptr_t)nvme + 0x1000 + 3 * dstrd);
	st->cq[1].phase = 1;
	st->cq[1].pos = 0;
	memset(st->cq[1].buf, 0, sizeof(struct nvme_completion) * ((size_t)st->cq[1].size + 1));
	cmd = st->sq[0].buf + st->sq[0].tail;
	memset(cmd, 0, sizeof(*cmd));
	cmd->opc = NVME_ADMIN_CREATE_IOCQ;
	cmd->cid = 0;
	cmd->dptr[0] = (u64)plat_virt_to_phys(st->cq[1].buf);
	cmd->dw10 = 1 | (u32)st->cq[1].size << 16;
	cmd->dw11 = 1;
	res = wait_single_command(st, 0);
	if (res != IOST_OK) {return res;}
	st->num_iocq = 1;

	st->sq[1].doorbell = (_Atomic u32 *)((uintptr_t)nvme + 0x1000 + 2 * dstrd);
	cmd = st->sq[0].buf + st->sq[0].tail;
	memset(cmd, 0, sizeof(*cmd));
	cmd->opc = NVME_ADMIN_CREATE_IOSQ;
	cmd->cid = 0;
	cmd->dptr[0] = (u64)plat_virt_to_phys(st->sq[1].buf);
	cmd->dw10 = 1 | 3 << 16;
	cmd->dw11 = 1 << 16 | 1;
	res = wait_single_command(st, 0);
	if (res != IOST_OK) {return res;}
	st->sq[1].cq = 1;
	st->num_iosq = 1;
	return IOST_OK;
}

void nvme_reset(struct nvme_state *st) {
	volatile struct nvme_regs *nvme = st->regs;
	nvme->config = st->cfg | 1 << NVME_CC_SHN_SHIFT;
	timestamp_t shutdown_timeout = st->rtd3e * TICKS_PER_MICROSECOND;
	timestamp_t t_wait_shutdown = get_timestamp(), t_shutdown;
	while (1) {
		u32 status = nvme->status;
		info("CSTS: %08"PRIx32"\n", status);
		t_shutdown = get_timestamp();
		if (nvme_extr_csts_shst(status) == 2) {
			nvme->config = st->cfg & ~NVME_CC_EN;
			return;
		}
		if (t_shutdown - t_wait_shutdown > shutdown_timeout) {break;}
		usleep(1000);
	}
	info("Controller shutdown timeout after %"PRIuTS" μs\n", (t_shutdown - t_wait_shutdown) / TICKS_PER_MICROSECOND);
	nvme->config = st->cfg | 2 << NVME_CC_SHN_SHIFT;
	while (1) {
		u32 status = nvme->status;
		info("CSTS: %08"PRIx32"\n", status);
		t_shutdown = get_timestamp();
		if (nvme_extr_csts_shst(status) == 2) {break;}
		if (t_shutdown - t_wait_shutdown > 2*shutdown_timeout) {
			info("Controller abrupt shutdown timeout after %"PRIuTS" μs\n", (t_shutdown - t_wait_shutdown) / TICKS_PER_MICROSECOND);
			break;
		}
		usleep(1000);
	}
	nvme->config = st->cfg & ~NVME_CC_EN;
}

struct nvme_xfer {
	struct nvme_req req;
	phys_addr_t prp_list_addr;
	phys_addr_t first_prp_entry, last_prp_entry;
	size_t prp_size, prp_cap, xfer_bytes;
	u64 *prp_list;
};

enum iost nvme_xfer_reset(struct nvme_xfer *xfer) {
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
			infos("first page\n");
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
		infos("second page\n");
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
			xfer->prp_list[idx] = plat_virt_to_phys(xfer->prp_list + idx + 1);
			idx += 1;
			if (idx >= xfer->prp_cap - 1) {return 0;}
		}
		xfer->prp_list[idx] = xfer->last_prp_entry;
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

enum iost nvme_read_wait(struct nvme_state *st, u16 sqid, struct nvme_xfer *xfer, u64 lba) {
	if (xfer->xfer_bytes & ((1 << st->lba_shift) - 1)) {return IOST_INVALID;}
	struct nvme_cmd *cmd = st->sq[sqid].buf + st->sq[sqid].tail;
	memset(cmd, 0, sizeof(*cmd));
	cmd->opc = NVME_NVM_READ;
	cmd->fuse_psdt = NVME_PSDT_PRP;
	cmd->nsid = 1;
	cmd->dptr[0] = xfer->first_prp_entry;
	if (xfer->prp_size == 2) {
		cmd->dptr[1] = xfer->last_prp_entry;
	} else if (xfer->prp_size > 2) {
		cmd->dptr[1] = xfer->prp_list_addr;
		/* add last entry at the end, even if it is the last on the page */
		xfer->prp_list[xfer->prp_size - 2] = xfer->last_prp_entry;
	}
	cmd->cmd_spec[0] = lba;
	cmd->dw12 = (xfer->xfer_bytes >> st->lba_shift) - 1;

	timestamp_t t_submit = get_timestamp();
	enum iost res = wait_single_command(st, sqid);
	if (res != IOST_OK) {return res;}
	info("read finished after %"PRIuTS" μs\n", (get_timestamp() - t_submit) / TICKS_PER_MICROSECOND);
	return IOST_OK;
}


static struct nvme_cq cqs[] = {
	{
		.buf = (struct nvme_completion *)uncached_buf[UBUF_ACQ],
		.size = 3,
	}, {
		.buf =(struct nvme_completion *)uncached_buf[UBUF_IOCQ],
		.size = 3,
	},
};
static _Atomic(struct nvme_req *) admin_cmd[2];
static _Atomic(struct nvme_req *) io_cmd[4];
static struct nvme_sq sqs[] = {
	{
		.buf = (struct nvme_cmd *)wt_buf[WTBUF_ASQ],
		.size = 3,
		.max_cid = 1,
		.cq = 0,
		.cmd = admin_cmd
	}, {
		.buf = (struct nvme_cmd *)wt_buf[WTBUF_IOSQ],
		.size = 3,
		.max_cid = 3,
		.cmd = io_cmd,
	},
};
static struct nvme_state st = {
	.regs = (struct nvme_regs *)0xfa100000,
	.num_iocq = 0,
	.num_iosq = 0,
	.cq = cqs,
	.sq = sqs,
};

void boot_nvme() {
	if ((cru[CRU_CLKGATE_CON+12] & 1 << 6) || (cru[CRU_CLKGATE_CON+20] & 3 << 10)) {
		info("sramstage left PCIe disabled\n");
		goto out;
	}
	if (!finish_pcie_link()) {goto shut_down_phys;}
	/* map MMIO regions 1 and 2 (1MiB each, creates one 2MiB block mapping) }*/
	mmu_map_range(0xfa000000, 0xfa1fffff, 0xfa000000, MEM_TYPE_DEV_nGnRnE);
	volatile struct rkpcie_addr_xlation *xlat = regmap_base(REGMAP_PCIE_ADDR_XLATION);
	/* map configuration space for bus 0 in MMIO region 1 */
	xlat->ob[1].addr[0] = 19;	/* forward 20 bits of address starting at 0 (ECAM mapping) */
	xlat->ob[1].addr[1] = 0;
	xlat->ob[1].desc[0] = 0x0080000a;
	xlat->ob[1].desc[1] = 0;
	xlat->ob[1].desc[2] = 0;
	xlat->ob[1].desc[3] = 0;
	dsb_sy();
	usleep(10);
	volatile u32 *conf = (u32 *)0xfa000000;
	for_range(dev, 0, 32) {
		u32 id = conf[dev << 13];
		info("00:%02"PRIx32": %04"PRIx32":%04"PRIx32"\n", dev, id & 0xffff, id >> 16);
	}
	u32 cmd_sts = conf[PCI_CMDSTS];
	if (~cmd_sts & PCI_CMDSTS_CAP_LIST) {
		info("no capability list\n");
		goto shut_down_phys;
	}
	u32 pciecap_id = 0, UNUSED msicap_id = 0, UNUSED msixcap_id = 0;
	u8 cap_ptr = conf[PCI_CAP_PTR] & 0xfc;
	while (cap_ptr) {
		u32 cap = conf[cap_ptr >> 2];
		{u8 nextptr = cap >> 8 & 0xfc;
			cap = (cap & 0xffff00ff) | (u32)cap_ptr << 8;
		cap_ptr = nextptr;}
		switch (cap & 0xff) {
		case 1:
			infos("PCI PM capability found\n");
			break;
		case 5:
			infos("MSI capability found\n");
			msicap_id = cap;
			break;
		case 16:
			infos("PCIe capability found\n");
			pciecap_id = cap;
			break;
		case 17:
			infos("MSI-X capability found\n");
			msixcap_id = cap;
			break;
		default:
			info("unknown PCI capabiltiy %02"PRIx32"\n", cap & 0xff);
		}
	}
	if (!pciecap_id) {
		infos("no PCIe capability found\n");
		goto shut_down_phys;
	}
	if ((pciecap_id >> 16 & 15) < 2) {
		infos("unexpected PCIe capability version\n");
		goto out;
	}
	if ((pciecap_id >> 20 & 15) != PCIECAP_ID_ENDPOINT) {
		infos("not a PCIe endpoint\n");
		goto out;
	}
	u32 cc = conf[PCI_CC];
	if (cc >> 8 != 0x010802) {
		infos("PCIe link partner is not NVMe\n");
		goto out;
	}
	conf[PCI_BAR+0] = 0xfffffff0;
	conf[PCI_BAR+1] = 0xffffffff;
	u32 a = conf[PCI_BAR+0], b = conf[PCI_BAR+1];
	if (b != 0xffffffff || (a & 0xfff00000) != 0xfff00000) {
		infos("Endpoint wants more than 1MiB of BAR space\n");
		goto out;
	}
	conf[PCI_BAR+0] = 0xfa100000;
	conf[PCI_BAR+1] = 0;
	//pcie_mgmt[RKPCIE_MGMT_RCBAR] = 0x8000001e;
	for_range(i, 0, 3) {
		xlat->rc_bar_addr[i][0] = 31;
		xlat->rc_bar_addr[i][1] = 0;
	}
	conf[PCI_CMDSTS] = PCI_CMD_MEM_EN | PCI_CMD_BUS_MASTER | PCI_CMD_SERR_EN;
	/* map first MiB of Memory space to region 2 */
	xlat->ob[2].addr[0] = 0xfa100000 | (20 - 1);	/* forward 20 bits of address, starting at 0xfa100000 */
	xlat->ob[2].addr[1] = 0;
	xlat->ob[2].desc[0] = 0x00800002;
	xlat->ob[2].desc[1] = 0;
	xlat->ob[2].desc[2] = 0;
	xlat->ob[2].desc[3] = 0;
	dsb_sy();
	
	mmu_unmap_range((u64)(uintptr_t)&wt_buf, (u64)(uintptr_t)&wt_buf + sizeof(wt_buf) - 1);
	mmu_unmap_range((u64)(uintptr_t)&uncached_buf, (u64)(uintptr_t)&uncached_buf + sizeof(uncached_buf) - 1);
	mmu_flush();
	mmu_map_range((u64)(uintptr_t)&wt_buf, (u64)(uintptr_t)&wt_buf + sizeof(wt_buf) - 1, (u64)(uintptr_t)&wt_buf, MEM_TYPE_WRITE_THROUGH);
	mmu_map_range((u64)(uintptr_t)&uncached_buf, (u64)(uintptr_t)&uncached_buf + sizeof(uncached_buf) - 1, (u64)(uintptr_t)&uncached_buf, MEM_TYPE_UNCACHED);
	mmu_flush();

	switch (nvme_init(&st)) {
	case IOST_OK: break;
	case IOST_INVALID: goto out;
	default: goto shut_down_log;
	}
	infos("NVMe MMIO init complete\n");
	if (IOST_OK != nvme_init_queues(&st, 1, 1)) {goto shut_down_nvme;}
	infos("NVMe queue init complete\n");
	struct nvme_xfer xfer = {
		.prp_list = (u64 *)wt_buf[WTBUF_PRP],
		.prp_list_addr = plat_virt_to_phys(wt_buf[WTBUF_PRP]),
		.prp_cap = 1 << PLAT_PAGE_SHIFT >> 3
	};
	if (IOST_OK != nvme_xfer_reset(&xfer)) {goto shut_down_nvme;}
	infos("xfer reset\n");
	phys_addr_t buf = plat_virt_to_phys(uncached_buf[UBUF_DATA]);
	if (!nvme_add_phys_buffer(&xfer, buf, buf + (1 << PLAT_PAGE_SHIFT))) {goto shut_down_nvme;}
	info("buffer added; first=%"PRIx64" last=%"PRIx64"\n", xfer.first_prp_entry, xfer.last_prp_entry);
	if (xfer.prp_size > 2) {
		for_range(i, 0, xfer.prp_size - 2) {
			info("%"PRIx64"\n", xfer.prp_list[i]);
		}
	}
	if (IOST_OK != nvme_read_wait(&st, 1, &xfer, 0)) {goto shut_down_nvme;}
	infos("data read\n");
	dump_mem(uncached_buf[UBUF_DATA], 1 << PLAT_PAGE_SHIFT);

shut_down_nvme:
	nvme_reset(&st);
shut_down_log:
	conf[PCI_CMDSTS] = 0;	/* soft-disconnect the device */
	goto out;
shut_down_phys:
	grf[GRF_SOC_CON0+5] = SET_BITS16(4, 15) << 3;	/* disable all lanes */
	dsb_st();
	cru[CRU_CLKGATE_CON+12] = SET_BITS16(1, 1) << 6;	/* clk_pciephy_ref100m */
	cru[CRU_CLKGATE_CON+20] = SET_BITS16(2, 3) << 10	/* {a,p}clk_pcie */
		| SET_BITS16(1, 1) << 2;	/* aclk_perf_pcie */
out:
	boot_medium_exit(BOOT_MEDIUM_NVME);
}
