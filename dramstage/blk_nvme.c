/* SPDX-License-Identifier: CC0-1.0 */
#include <rk3399.h>
#include <rk3399/dramstage.h>
#include <rk3399/payload.h>
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include <arch.h>
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
#include <cache.h>

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

static WRITE_THROUGH _Alignas(1 << PLAT_PAGE_SHIFT) u8 wt_buf[NUM_WTBUF][1 << PLAT_PAGE_SHIFT];
static UNCACHED _Alignas(1 << PLAT_PAGE_SHIFT) u8 uncached_buf[NUM_UBUF][1 << PLAT_PAGE_SHIFT];

static _Bool finish_pcie_link() {
	timestamp_t start = get_timestamp(), gen1_trained, retrained;
	static volatile u32 *const pcie_client = regmap_pcie_client;
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
	static volatile u32 *const pcie_mgmt = regmap_pcie_mgmt;
	u32 plc0 = pcie_mgmt[RKPCIE_MGMT_PLC0];
	if (~plc0 & RKPCIE_MGMT_PLC0_LINK_TRAINED) {return 0;}
	info("trained %"PRIu32"x link (waited %"PRIuTS" μs)\n", 1 << (plc0 >> 1 & 3), (gen1_trained - start) / TICKS_PER_MICROSECOND);
	static volatile u32 *const pcie_rcconf = regmap_pcie_rcconf;
	for_range(i, 0, 0x138 >> 2) {
		spew("%03"PRIx32": %08"PRIx32"\n", i*4, pcie_rcconf[i]);
	}
	static volatile u32 *const lcs = pcie_rcconf + RKPCIE_RCCONF_PCIECAP + PCIECAP_LCS;
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
	.regs = (struct nvme_regs *)0xf8000000,
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
			invalidate_range(dev->end_ptr, dev->next_end_ptr - dev->end_ptr);
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
		/* we will invalidate later, but this prevents any previous
		 * cache contents from overwriting DMA'd-in data */
		flush_range(dev->end_ptr, end - dev->end_ptr);
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
	static volatile u32 *const cru = regmap_cru;
	if ((cru[CRU_CLKGATE_CON+12] & 1 << 6) || (cru[CRU_CLKGATE_CON+20] & 3 << 10)) {
		info("sramstage left PCIe disabled\n");
		goto out;
	}
	if (!finish_pcie_link()) {goto shut_down_phys;}
	/* map MMIO regions 0–2 32+1+1 MiB, should create 17 2 MiB block mappings */
	mmu_map_range(0xf8000000, 0xfa1fffff, 0xf8000000, MEM_TYPE_DEV_nGnRnE);
	volatile struct rkpcie_addr_xlation *xlat = regmap_pcie_addr_xlation;
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
	if (b != 0xffffffff || (a & 0xfe000000) != 0xfe000000) {
		infos("Endpoint wants more than 32MiB of BAR space\n");
		goto out;
	}
	conf[PCI_BAR+0] = 0xf8000000;
	conf[PCI_BAR+1] = 0;
	//pcie_mgmt[RKPCIE_MGMT_RCBAR] = 0x8000001e;
	for_range(i, 0, 3) {
		xlat->rc_bar_addr[i][0] = 31;
		xlat->rc_bar_addr[i][1] = 0;
	}
	conf[PCI_CMDSTS] = PCI_CMD_MEM_EN | PCI_CMD_BUS_MASTER | PCI_CMD_SERR_EN;

	/* map some Memory space to region 0 */
	xlat->ob[0].addr[0] = 0xf8000000 | (25 - 1);	/* forward 25 bits of address, starting at 0xf8000000 */
	xlat->ob[0].addr[1] = 0;
	xlat->ob[0].desc[0] = 0x00800002;
	xlat->ob[0].desc[1] = 0;
	xlat->ob[0].desc[2] = 0;
	xlat->ob[0].desc[3] = 0;

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
	regmap_grf[GRF_SOC_CON0+5] = SET_BITS16(4, 15) << 3;	/* disable all lanes */
	dsb_st();
	cru[CRU_CLKGATE_CON+12] = SET_BITS16(1, 1) << 6;	/* clk_pciephy_ref100m */
	cru[CRU_CLKGATE_CON+20] = SET_BITS16(2, 3) << 10	/* {a,p}clk_pcie */
		| SET_BITS16(1, 1) << 2;	/* aclk_perf_pcie */
out:
	boot_medium_exit(BOOT_MEDIUM_NVME);
}
