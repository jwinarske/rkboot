/* SPDX-License-Identifier: CC0-1.0 */
#include <nvme_regs.h>
#include <nvme.h>
#include <stdatomic.h>
#include <string.h>
#include <assert.h>

#include <log.h>
#include <iost.h>
#include <timer.h>
#include <runqueue.h>
#include <byteorder.h>
#if defined(SPEW_MSG) || defined(DEBUG_MSG)
# include "dump_mem.h"
#endif


enum iost nvme_init(struct nvme_state *st) {
	volatile struct nvme_regs *nvme = st->regs;
	u64 nvme_cap = st->cap = from_le64(nvme->capabilities);
	u32 version = from_le32(nvme->version);
	info("NVMe CAP: %016"PRIx64" VS: %08"PRIx32"\n", nvme_cap, version);
	if ((version >> 16) != 1) {
		infos("unexpected major version\n");
		return IOST_INVALID;
	}
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
		debug("CSTS: %08"PRIx32"\n", status);
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
	nvme->adminq_attr = to_le32(st->cq[0].size << 16 | st->sq[0].size);
	nvme->adminsq_base = to_le64(plat_virt_to_phys(st->sq[0].buf));
	nvme->admincq_base = to_le64(plat_virt_to_phys(st->cq[0].buf));
	cfg |= NVME_CMDSET_NVM << NVME_CC_CSS_SHIFT | 0 << NVME_CC_MPS_SHIFT | 0 << NVME_CC_AMS_SHIFT;
	atomic_store_explicit(&nvme->config, to_le32(cfg), memory_order_relaxed);
	cfg |= NVME_CC_EN;
	/* release because clearing the ACQ memory has to be cleared before enabling */
	atomic_store_explicit(&nvme->config, to_le32(cfg), memory_order_release);
	atomic_thread_fence(memory_order_seq_cst);
	timestamp_t t_ready;
	while (1) {
		u32 status = from_le32(atomic_load_explicit(&nvme->status, memory_order_acquire));
		debug("CSTS: %08"PRIx32"\n", status);
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

void nvme_dump_completion(struct nvme_completion *cqe, u16 status) {
	status = (status & 0xf000) | (status >> 1 & 0x07ff);
	info("%04"PRIx16" %04"PRIx16"%04"PRIx16" %04"PRIx16" %"PRIx32" %"PRIx32"\n",
		status,
		from_le16(cqe->sqid), from_le16(cqe->cid), from_le16(cqe->sqhd),
		from_le32(cqe->cmd_spec), from_le32(cqe->reserved)
	);
}

enum iost nvme_process_cqe(volatile struct nvme_state *st, u16 cqid) {
	struct nvme_cq *cq = st->cq + cqid;
	u16 pos = cq->pos;
	_Bool phase = cq->phase;
	struct nvme_completion *cqe = cq->buf + pos;
	u16 cmd_st = from_le16(atomic_load_explicit(&cqe->status, memory_order_acquire));
	if ((cmd_st & 1) != phase) {return IOST_TRANSIENT;}
	u16 sqid = from_le16(cqe->sqid);
	if (sqid > st->num_iosq) {
		infos("unexpected SQID: ");
		nvme_dump_completion(cqe, cmd_st);
		return IOST_GLOBAL;
	}
	struct nvme_sq *sq = st->sq + sqid;
	if (sq->cq != cqid) {
		infos("completion for unassociated SQ: ");
		nvme_dump_completion(cqe, cmd_st);
		return IOST_GLOBAL;
	}
	u16 cid = from_le16(cqe->cid);
	if (!sq->cmd || sq->max_cid < cid) {
		infos("command ID out of range: ");
		nvme_dump_completion(cqe, cmd_st);
		return IOST_GLOBAL;
	}
	struct nvme_req *cmd = atomic_load_explicit(sq->cmd + cid, memory_order_acquire);
#if DEBUG_MSG
	nvme_dump_completion(cqe, cmd_st);
#endif
	cmd->data[0] = from_le32(cqe->cmd_spec);
	cmd->data[1] = from_le32(cqe->reserved);
	cmd->aux = 0;
	u16 sqhd = from_le16(cqe->sqhd);
	atomic_thread_fence(memory_order_release);
	atomic_store_explicit(sq->cmd + cid, 0, memory_order_relaxed);
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

_Bool nvme_submit_single_command(struct nvme_state *st, u16 sqid, struct nvme_req *req) {
	assert(sqid <= st->num_iosq);
	struct nvme_sq *sq = st->sq + sqid;
	u16 tail = sq->tail, head = atomic_load_explicit(&sq->head, memory_order_acquire);
	u16 next = tail == sq->size ? 0 : tail + 1;
	if (next == head) {	/* TODO: wait on full queue */
		infos("queue full\n");
		return 0;
	}
	atomic_store_explicit(&req->status, NVME_SUBMITTED, memory_order_relaxed);
	u16 cid;
	for_range(i, 0, (u32)sq->max_cid + 1) {
		struct nvme_req *ptr = 0;
		if (atomic_compare_exchange_strong_explicit(sq->cmd + i, &ptr, req, memory_order_release, memory_order_relaxed)) {
			cid = i;
			goto cid_assigned;
		}
	}
	infos("no free command ID found\n");
	return 0;
cid_assigned:
	sq->buf[tail].cid = to_le16(cid);
	sq->tail = next;
	atomic_store_explicit(sq->doorbell, to_le16(next), memory_order_release);
	debug("submitted %04"PRIx16"%04"PRIx16"\n", sqid, cid);
#ifdef SPEW_MSG
	dump_mem(sq->buf + tail, sizeof(struct nvme_cmd));
#endif
	return 1;
}

enum iost nvme_wait_req(struct nvme_state *st, struct nvme_req *req) {
	u16 cmd_st;
	volatile struct nvme_regs *nvme = st->regs;
	while (1) {
		for_range(cqid, 0, (u32)st->num_iocq + 1) {
			while (1) {switch (nvme_process_cqe(st, cqid)) {
			case IOST_TRANSIENT: goto next_cq;
			case IOST_OK: continue;
			default: return IOST_GLOBAL;
			}}
		next_cq:;
		}
		cmd_st = atomic_load_explicit(&req->status, memory_order_acquire);
		if (cmd_st != NVME_SUBMITTED) {break;}
		u32 status = from_le32(nvme->status);
		debug("waiting st%08"PRIx32"\n", status);
		if (status & NVME_CSTS_CFS) {return IOST_GLOBAL;}
		usleep(100);
	}
	if (cmd_st & 0xfffe) {
		u16 hex_aligned_code = (cmd_st & 0xf000) | (cmd_st >> 1 & 0x07ff);
		info("completion error: %03"PRIx16" %08"PRIx32" %"PRIx32"\n",
			hex_aligned_code, req->data[0], req->data[1]
		);
		return IOST_LOCAL;
	}
	return IOST_OK;
}

enum iost wait_single_command(struct nvme_state *st, u16 sqid) {
	struct nvme_req req;
	memset(&req, 0, sizeof(req));
	if (!nvme_submit_single_command(st, sqid, &req)) {return IOST_TRANSIENT;}
	return nvme_wait_req(st, &req);
}

enum iost nvme_init_queues(struct nvme_state *st, u16 num_iocq, u16 num_iosq, u8 *idctl) {
	volatile struct nvme_regs *nvme = st->regs;
	assert(num_iocq && num_iosq);
	struct nvme_cmd *cmd;
	cmd = st->sq[0].buf + st->sq[0].tail;
	memset(cmd, 0, sizeof(*cmd));
	cmd->opc = NVME_ADMIN_IDENTIFY;
	cmd->dptr[0] = (u64)(uintptr_t)idctl;
	cmd->dptr[1] = 0;
	cmd->dw10 = NVME_IDENTIFY_CONTROLLER;
	enum iost res = wait_single_command(st, 0);
	if (res != IOST_OK) {return res;}

#ifdef DEBUG_MSG
	dump_mem(idctl, 0x1000);
#endif
	u32 rtd3e = nvme_extr_idctl_rtd3e(idctl);
	info("RTD3E: %"PRIu32" μs\n", rtd3e);
	if (!rtd3e) {st->rtd3e = rtd3e;}

	if ((nvme_extr_idctl_sqes(idctl) & 15) > 6 || (nvme_extr_idctl_cqes(idctl) & 15) > 4) {
		info("unsupported queue entry sizes\n");
		return IOST_INVALID;
	}

	st->cfg |= 4 << NVME_CC_IOCQES_SHIFT | 6 << NVME_CC_IOSQES_SHIFT;
	nvme->config = to_le32(st->cfg);

	cmd = st->sq[0].buf + st->sq[0].tail;
	memset(cmd, 0, sizeof(*cmd));
	cmd->opc = NVME_ADMIN_SET_FEATURES;
	cmd->dw10 = to_le32(NVME_FEATURE_NUM_QUEUES);
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
	cmd->dptr[0] = to_le64(plat_virt_to_phys(st->cq[1].buf));
	cmd->dw10 = to_le32(1 | (u32)st->cq[1].size << 16);
	cmd->dw11 = to_le32(1);
	res = wait_single_command(st, 0);
	if (res != IOST_OK) {return res;}
	st->num_iocq = 1;

	st->sq[1].doorbell = (_Atomic u32 *)((uintptr_t)nvme + 0x1000 + 2 * dstrd);
	cmd = st->sq[0].buf + st->sq[0].tail;
	memset(cmd, 0, sizeof(*cmd));
	cmd->opc = NVME_ADMIN_CREATE_IOSQ;
	cmd->dptr[0] = to_le64(plat_virt_to_phys(st->sq[1].buf));
	cmd->dw10 = to_le32(1 | 3 << 16);
	cmd->dw11 = to_le32(1 << 16 | 1);
	res = wait_single_command(st, 0);
	if (res != IOST_OK) {return res;}
	st->sq[1].cq = 1;
	st->num_iosq = 1;
	return IOST_OK;
}

void nvme_reset(struct nvme_state *st) {
	volatile struct nvme_regs *nvme = st->regs;
	nvme->config = to_le32(st->cfg | 1 << NVME_CC_SHN_SHIFT);
	timestamp_t shutdown_timeout = st->rtd3e * TICKS_PER_MICROSECOND;
	timestamp_t t_wait_shutdown = get_timestamp(), t_shutdown;
	while (1) {
		u32 status = from_le32(nvme->status);
		debug("CSTS: %08"PRIx32"\n", status);
		t_shutdown = get_timestamp();
		if (nvme_extr_csts_shst(status) == 2) {
			nvme->config = to_le32(st->cfg & ~NVME_CC_EN);
			return;
		}
		if (t_shutdown - t_wait_shutdown > shutdown_timeout) {break;}
		usleep(1000);
	}
	info("Controller shutdown timeout after %"PRIuTS" μs\n", (t_shutdown - t_wait_shutdown) / TICKS_PER_MICROSECOND);
	nvme->config = to_le32(st->cfg | 2 << NVME_CC_SHN_SHIFT);
	while (1) {
		u32 status = from_le32(nvme->status);
		info("CSTS: %08"PRIx32"\n", status);
		t_shutdown = get_timestamp();
		if (nvme_extr_csts_shst(status) == 2) {break;}
		if (t_shutdown - t_wait_shutdown > 2*shutdown_timeout) {
			info("Controller abrupt shutdown timeout after %"PRIuTS" μs\n", (t_shutdown - t_wait_shutdown) / TICKS_PER_MICROSECOND);
			break;
		}
		usleep(1000);
	}
	nvme->config = to_le32(st->cfg & ~NVME_CC_EN);
}
