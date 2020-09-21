/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

struct nvme_regs {
	u64 capabilities;
	u32 version;
	u32 int_en;
	u32 int_dis;
	u32 config;
	u32 reserved;
	u32 status;
	u32 nvm_ss_reset;
	u32 adminq_attr;
	u64 adminsq_base;
	u64 admincq_base;
	u32 cmb_location;
	u32 cmb_size;
	u32 bp_info;
	u32 bp_read_sel;
	u64 bp_buffer;
};
CHECK_OFFSET(nvme_regs, bp_buffer, 0x48);

struct nvme_cmd {
	u8 opc;
	u8 fuse_psdt;
	u16 cid;
	u32 nsid;
	u64 reserved;
	u64 mptr;
	u64 dptr[2];
	u64 cmd_spec[3];
};
_Static_assert(sizeof(struct nvme_cmd) == 64, "NVMe command struct must be 64 bytes long");

struct nvme_completion {
	u32 cmd_spec;
	u32 reserved;
	u16 sqid;
	u16 sdhd;
	u16 cid;
	_Atomic u16 status;
};
_Static_assert(sizeof(struct nvme_completion) == 16, "NVMe completion struct must be 16 bytes long");

enum nvme_command_sets {
	NVME_CMDSET_NVM,
};

#define DEFINE_NVME_CAP\
	FIELD(MQES, mqes, "Maximum Queue Entries Supported", 0, 16)\
	FLAG(CQR, "Contiguous Queues Required", 16)\
	FIELD(AMS, ams, "Arbitration Mechanism Supported", 17, 2)\
		FLAG(AMS_WRR_URGENT, "Weighted Round Robin with Urgent Priority Class", 17)\
		FLAG(AMS_VENDOR, "vendor-specific arbitration mechanism", 18)\
	FIELD(TO, to, "Timeout (for controller enable and reset)", 24, 8)\
	FIELD(DSTRD, dstrd, "Doorbell stride (shift 4)", 32, 4)\
	FLAG(NSSRS, "NVM Subsystem Reset Supported", 36)\
	FIELD(CSS, css, "Command Sets Supported", 37, 8)\
	FLAG(BPS, "Boot Partition Support", 45)\
	/* 46:47 reserved */\
	FIELD(MPSMIN, mpsmin, "Memory Page Size Minimum (shift 4096)", 48, 4)\
	FIELD(MPSMAX, mpsmax, "Memory Page Size Maximum (shift 4096)", 52, 4)\
	/* 56:63 reserved */

enum nvme_cap_shifts {
#define FIELD(caps, snake, desc, shift, len) NVME_CAP_##caps##_SHIFT = shift,
#define FLAG(caps, desc, bit) NVME_CAP_##caps##_SHIFT = bit,
	DEFINE_NVME_CAP
#undef FIELD
#undef FLAG
};

enum {
#define FIELD(caps, snake, desc, shift, len) NVME_CAP_##caps##_MASK = ~(~UINT64_C(0) << len) << shift,
#define FLAG(caps, desc, bit) NVME_CAP_##caps = UINT64_C(1) << bit,
	DEFINE_NVME_CAP
#undef FIELD
#undef FLAG
};

#define FIELD(caps, snake, desc, shift, len) HEADER_FUNC u64 nvme_extr_cap_##snake(u64 cap) {return (cap & NVME_CAP_##caps##_MASK) >> shift;}
#define FLAG(caps, desc, bit)
DEFINE_NVME_CAP
#undef FIELD
#undef FLAG

#define DEFINE_NVME_CC\
	FLAG(EN, "Enable", 0)\
	FIELD(CSS, css, "I/O Command Set Selected", 4, 3)\
	FIELD(MPS, mps, "Memory Page Size (shift 4096)", 7, 4)\
	FIELD(AMS, ams, "Arbitration Mechanism Selected", 11, 3)\
	FIELD(SHN, shn, "Shutdown Notification", 14, 2)\
	FIELD(IOSQES, iosqes, "I/O Submission Queue Entry Size (shift)", 16, 4)\
	FIELD(IOCQES, iocqes, "I/O Completion Queue Entry Size (shift)", 20, 4)

enum nvme_cc_shift {
#define FIELD(caps, snake, desc, shift, len) NVME_CC_##caps##_SHIFT = shift,
#define FLAG(caps, desc, bit)
	DEFINE_NVME_CC
#undef FIELD
#undef FLAG
};

enum {
#define FIELD(caps, snake, desc, shift, len) NVME_CC_##caps##_MASK = ~(~UINT32_C(0) << len) << shift,
#define FLAG(caps, desc, bit) NVME_CC_##caps = UINT32_C(1) << bit,
	DEFINE_NVME_CC
#undef FIELD
#undef FLAG
};

enum {
	NVME_CSTS_RDY = 1,
	NVME_CSTS_CFS = 2,
	NVME_CSTS_SHST_OPERATIONAL = 0,
	NVME_CSTS_SHST_ACK = 1 << 2,
	NVME_CSTS_SHST_COMPLETE = 2 << 2,
	NVME_CSTS_SHST_MASK = 3 << 2,
};

HEADER_FUNC u32 nvme_extr_csts_shst(u32 csts) {return csts >> 2 & 3;}

#define DEFINE_NVME_IDCTL\
	U32(RTD3E, rtd3e, "RTD3 Entry Latency", 88)\
	BYTE(SQES, sqes, "Submission Queue Entry Size", 512)\
	BYTE(CQES, sqes, "Completion Queue Entry Size", 513)\
	U16(MAXCMD, maxcmd, "Maximum Outstanding Commands", 514)\
	U32(NN, nn, "Number of Namespaces", 516)

#define BYTE(caps, snake, desc, offset)
#define U16(caps, snake, desc, offset) HEADER_FUNC u16 nvme_extr_idctl_##snake(const u8 *idctl) {return idctl[offset] | (u16)idctl[(offset) + 1] << 8;}
#define U32(caps, snake, desc, offset) HEADER_FUNC u32 nvme_extr_idctl_##snake(const u8 *idctl) {return idctl[offset] | (u32)idctl[(offset) + 1] << 8 | (u32)idctl[(offset) + 2] << 16 | (u32)idctl[(offset) + 3] << 24;}
	DEFINE_NVME_IDCTL
#undef BYTE
#undef U16
#undef U32

#define DEFINE_NVME_ADMIN\
	NWR(0, DEL_IOSQ, CREATE_IOSQ, GET_LOG_PAGE)\
	NWR(1, DEL_IOCQ, CREATE_IOCQ, IDENTIFY)\
	NWR(2, ABORT, SET_FEATURES, GET_FEATURES)\
	NW(3, ASYNC_EV_REQ, NS_MGMT)\
	NW(4, FW_COMMIT, FW_IMG_DOWNLOAD)\
	NW(5, SELF_TEST, NS_ATTACHMENT)\
	NWR(6, KEEP_ALIVE, DIRECTIVE_SEND, DIRECTIVE_RECV)\
	NWR(7, VIRT_MGMT, MGMT_IF_SEND, MGMT_IF_RECV)

enum nvme_admin_opc {
#define NWR(func, nodata, write, read) NVME_ADMIN_##nodata = func << 2, NVME_ADMIN_##write, NVME_ADMIN_##read,
#define NW(func, nodata, write) NVME_ADMIN_##nodata = func << 2, NVME_ADMIN_##write,
	DEFINE_NVME_ADMIN
#undef NWR
#undef NW
};

enum {
	NVME_IDENTIFY_NS = 0,
	NVME_IDENTIFY_CONTROLLER,
	NVME_IDENTIFY_ACTIVE_NSID_LIST,
	NVME_IDENTIFY_NSID_DESC_LIST,
};
