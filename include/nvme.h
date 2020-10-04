/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

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

enum iost nvme_init(struct nvme_state *st);
void nvme_dump_completion(struct nvme_completion *cqe, u16 status);
enum iost nvme_process_cqe(volatile struct nvme_state *st, u16 cqid);
_Bool nvme_submit_single_command(struct nvme_state *st, u16 sqid, struct nvme_req *req);
enum iost nvme_wait_req(struct nvme_state *st, struct nvme_req *req);
enum iost wait_single_command(struct nvme_state *st, u16 sqid);
enum iost nvme_init_queues(struct nvme_state *st, u16 num_iocq, u16 num_iosq, u8 *idctl);
void nvme_reset(struct nvme_state *st);
