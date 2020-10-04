/* SPDX-License-Identifier: CC0-1.0 */
#include <rk3399.h>
#include <rk3399/dramstage.h>
#include <rk3399/payload.h>
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include <pci_regs.h>
#include <rkpcie_regs.h>
#include <nvme.h>
#include <nvme_regs.h>
#include <log.h>
#include <runqueue.h>
#include <timer.h>
#include <aarch64.h>
#include <mmu.h>
#include <dump_mem.h>
#include <iost.h>
#include <async.h>
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

struct nvme_xfer {
	struct nvme_req req;
	phys_addr_t prp_list_addr;
	phys_addr_t first_prp_entry, last_prp_entry;
	size_t prp_size, prp_cap, xfer_bytes;
	u64 *prp_list;
};

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

_Bool nvme_emit_read(struct nvme_state *st, struct nvme_sq *sq, struct nvme_xfer *xfer, u32 nsid, u64 lba) {
	if (xfer->xfer_bytes & ((1 << st->lba_shift) - 1)) {return 0;}
	struct nvme_cmd *cmd = sq->buf + sq->tail;
	memset(cmd, 0, sizeof(*cmd));
	cmd->opc = NVME_NVM_READ;
	cmd->fuse_psdt = NVME_PSDT_PRP;
	cmd->nsid = nsid;
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

struct nvme_blockdev {
	struct async_blockdev blk;
	struct nvme_state *st;
	struct nvme_xfer xfer;
	u8 *consume_ptr, *end_ptr, *next_end_ptr, *stop_ptr;
	u64 next_lba;
	u8 xfer_shift;
	u32 nsid;
};

static u8 iost_u8[NUM_IOST];

static struct async_buf pump(struct async_transfer *async, size_t consume, size_t min_size) {
	struct nvme_blockdev *dev = (struct nvme_blockdev *)async;
	assert((size_t)(dev->end_ptr - dev->consume_ptr) >= consume);
	dev->consume_ptr += consume;
	while ((size_t)(dev->end_ptr - dev->consume_ptr) < min_size) {
		if (dev->end_ptr != dev->next_end_ptr) {
			enum iost res = nvme_wait_req(dev->st, &dev->xfer.req);
			if (res != IOST_OK) {return (struct async_buf) {iost_u8 + res, iost_u8};}
			dev->end_ptr = dev->next_end_ptr;
		}
		if (dev->stop_ptr == dev->end_ptr) {
			return (struct async_buf) {dev->consume_ptr, dev->end_ptr};
		}
		u32 xfer_size = UINT32_C(1) << dev->xfer_shift;
		u8 *end = dev->stop_ptr - dev->end_ptr > xfer_size ? dev->end_ptr + xfer_size: dev->stop_ptr;
		debug("starting new xfer LBA 0x%08"PRIx64" buf 0x%"PRIx64"–0x%"PRIx64"\n", dev->next_lba, (u64)dev->end_ptr, (u64)end);
		enum iost res = nvme_reset_xfer(&dev->xfer);
		if (res != IOST_OK) {return (struct async_buf) {iost_u8 + res, iost_u8};}
		_Bool success = nvme_add_phys_buffer(&dev->xfer, plat_virt_to_phys(dev->end_ptr), plat_virt_to_phys(end));
		assert(success);
		if (!nvme_emit_read(dev->st, dev->st->sq + 1, &dev->xfer, dev->nsid, dev->next_lba)) {return (struct async_buf) {iost_u8 + IOST_INVALID, iost_u8};}
		if (!nvme_submit_single_command(dev->st, 1, &dev->xfer.req)) {return (struct async_buf) {iost_u8 + IOST_TRANSIENT, iost_u8};}
		dev->next_lba += xfer_size / dev->blk.block_size;
		dev->next_end_ptr = end;
	}
	return (struct async_buf) {dev->consume_ptr, dev->end_ptr};
}

static enum iost start(struct async_blockdev *dev_, u64 addr, u8 *buf, u8 *buf_end) {
	struct nvme_blockdev *dev = (struct nvme_blockdev *)dev_;
	if (buf_end < buf
		|| (size_t)(buf_end - buf) % dev->blk.block_size != 0
		|| addr >= dev->blk.num_blocks
	) {return IOST_INVALID;}
	dev->next_lba = (u32)addr;
	dev->consume_ptr = dev->end_ptr = dev->next_end_ptr = buf;
	dev->stop_ptr = buf_end;
	return IOST_OK;
}

struct nvme_blockdev nvme_blk = {
	.blk = {
		.async = {pump},
		.start = start,
	},
	.xfer = {
		.prp_list = (u64 *)wt_buf[WTBUF_PRP],
		.prp_cap = 1 << PLAT_PAGE_SHIFT >> 3,
	},
	.st = &st,
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
	if (IOST_OK != nvme_init_queues(&st, 1, 1, uncached_buf[UBUF_IDCTL])) {goto shut_down_nvme;}
	infos("NVMe queue init complete\n");
	nvme_blk.xfer.prp_list_addr = plat_virt_to_phys(wt_buf[WTBUF_PRP]);
	u8 read_shift = 17;
	_Static_assert(PLAT_PAGE_SHIFT <= 17, "page size larger than transfer size");
	u8 mdts = nvme_extr_idctl_mdts(uncached_buf[UBUF_IDCTL]);
	u8 mpsmin = nvme_extr_cap_mpsmin(st.cap) + 12;
	if (mdts && mdts < read_shift - mpsmin) {
		read_shift = mdts + mpsmin;
	}
	nvme_blk.xfer_shift = read_shift;
	info("MDTS: %"PRIu32"B\n", UINT32_C(1) << read_shift);
	u32 num_ns = nvme_extr_idctl_nn(uncached_buf[UBUF_IDCTL]);
	if (!num_ns) {
		infos("controller has no valid NSIDs\n");
		goto shut_down_nvme;
	}
	if (num_ns == ~(u32)0) {
		infos("bogus IDCtl.NN value\n");
		goto shut_down_nvme;
	}
	for_range(nsid, 1, num_ns + 1) {
		struct nvme_cmd *cmd = st.sq[0].buf + st.sq[0].tail;
		memset(cmd, 0, sizeof(*cmd));
		cmd->opc = NVME_ADMIN_IDENTIFY;
		cmd->nsid = nsid;
		const u8 *idns = uncached_buf[UBUF_IDNS];
		cmd->dptr[0] = (u64)(uintptr_t)idns;
		cmd->dptr[1] = 0;
		cmd->dw10 = NVME_IDENTIFY_NS;
		enum iost res = wait_single_command(&st, 0);
		if (res != IOST_OK) {goto shut_down_nvme;}

#ifdef DEBUG_MSG
		dump_mem(idns, 0x180);
#endif
		u64 ns_size = nvme_extr_idns_nsze(idns);
		u8 flbas = nvme_extr_idns_flbas(idns);
		const u8 *lbaf = idns + 128 + 4 * (flbas & 15);
		if (lbaf[0] || lbaf[1]) {
			infos("formatted LBAF has metadata, don't know how to deal with it\n");
			goto shut_down_nvme;
		}
		if (lbaf[2] > 12 || lbaf[2] < 9) {
			info("unexpected LBA size 1 << %"PRIu8"\n", lbaf[2]);
			goto shut_down_nvme;
		}
		st.lba_shift = lbaf[2];
		nvme_blk.nsid = nsid;
		nvme_blk.blk.block_size = 1 << st.lba_shift;
		nvme_blk.blk.num_blocks = ns_size;
		info("namespace %"PRIu32" has %"PRIu64" (0x%"PRIx64") %"PRIu32"-byte sectors\n", nsid, ns_size, ns_size, nvme_blk.blk.block_size);

		if (!wait_for_boot_cue(BOOT_MEDIUM_NVME)) {goto shut_down_nvme;}
		switch (boot_blockdev(&nvme_blk.blk)) {
		case IOST_OK: boot_medium_loaded(BOOT_MEDIUM_NVME); goto shut_down_nvme;
		case IOST_GLOBAL: goto shut_down_nvme;
		case IOST_INVALID: infos("invalid");break;
		case IOST_LOCAL: infos("local"); break;
		case IOST_TRANSIENT: infos("transient"); break;
		default: break;
		}
	}
	infos("tried all namespaces\n");

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
