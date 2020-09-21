/* SPDX-License-Identifier: CC0-1.0 */
#include <rk3399.h>
#include <rk3399/dramstage.h>
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>

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

static UNINITIALIZED _Alignas(0x1000) u8 wt_buf[NUM_WTBUF][0x1000];
static UNINITIALIZED _Alignas(0x1000) u8 uncached_buf[NUM_UBUF][0x1000];

static enum iost wait_single_command(volatile struct nvme_regs *nvme, struct nvme_completion *cqe, _Bool phase) {
	u16 cmd_st;
	while (((cmd_st = atomic_load_explicit(&cqe->status, memory_order_acquire)) & 1) != phase) {
		u32 status = nvme->status;
		info("waiting %08"PRIx32"\n", status);
		if (status & NVME_CSTS_CFS) {return IOST_GLOBAL;}
		usleep(1000);
	}
	if ((cmd_st & 0xfffe) || cqe->cid) {
		dump_mem(cqe, sizeof(*cqe));
		infos("completion error\n");
		return IOST_LOCAL;
	}
	return IOST_OK;
}

void boot_nvme() {
	if ((cru[CRU_CLKGATE_CON+12] & 1 << 6) || (cru[CRU_CLKGATE_CON+20] & 3 << 10)) {
		info("sramstage left PCIe disabled\n");
		goto out;
	}
	timestamp_t start = get_timestamp(), gen1_trained, retrained;
	volatile u32 *pcie_client = regmap_base(REGMAP_PCIE_CLIENT);
	while (1) {
		u32 basic_status1 = pcie_client[RKPCIE_CLIENT_BASIC_STATUS+1];
		info("PCIe link status %08"PRIx32" %08"PRIx32"\n", pcie_client[RKPCIE_CLIENT_DEBUG_OUT+0], basic_status1);
		gen1_trained = get_timestamp();
		if ((basic_status1 >> 20 & 3) == 3) {break;}
		if (gen1_trained - start > MSECS(500)) {
			info("timed out waiting for PCIe link\n");
			goto shut_down_phys;
		}
		usleep(1000);
	}
	volatile u32 *pcie_mgmt = regmap_base(REGMAP_PCIE_MGMT);
	u32 plc0 = pcie_mgmt[RKPCIE_MGMT_PLC0];
	if (~plc0 & RKPCIE_MGMT_PLC0_LINK_TRAINED) {goto shut_down_phys;}
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
			goto out;
		}
		usleep(1000);
	}
	info("link speed after retrain: %"PRIu32" (waited %"PRIuTS" μs)\n", lcs_val >> 16 & 15, (retrained - gen1_trained) / TICKS_PER_MICROSECOND);
	/* could power down unused lanes here; we assume full width, so we don't waste code size implementing it */
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
	volatile struct nvme_regs *nvme = (struct nvme_regs *)0xfa100000;
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
	u64 nvme_cap = nvme->capabilities;
	u32 cmbsz = nvme->cmb_size;
	info("NVMe CAP: %016"PRIx64" CMBSZ: %08"PRIx32"\n", nvme_cap, cmbsz);
	if (~nvme_extr_cap_css(nvme_cap) & 1 << NVME_CMDSET_NVM) {
		infos("Controller does not support NVM command set\n");
		goto out;
	}
	if (nvme_cap & NVME_CAP_MPSMIN_MASK) {
		infos("Controller does not support 4k pages\n");
		goto out;
	}
	if (nvme_extr_cap_mqes(nvme_cap) < 3) {
		infos("Controller does not support 4+ entry queues\n");
		goto out;
	}
	mmu_unmap_range((u64)(uintptr_t)&wt_buf, (u64)(uintptr_t)&wt_buf + sizeof(wt_buf) - 1);
	mmu_unmap_range((u64)(uintptr_t)&uncached_buf, (u64)(uintptr_t)&uncached_buf + sizeof(uncached_buf) - 1);
	mmu_flush();
	mmu_map_range((u64)(uintptr_t)&wt_buf, (u64)(uintptr_t)&wt_buf + sizeof(wt_buf) - 1, (u64)(uintptr_t)&wt_buf, MEM_TYPE_WRITE_THROUGH);
	mmu_map_range((u64)(uintptr_t)&uncached_buf, (u64)(uintptr_t)&uncached_buf + sizeof(uncached_buf) - 1, (u64)(uintptr_t)&uncached_buf, MEM_TYPE_UNCACHED);
	mmu_flush();
	timestamp_t timeout = nvme_extr_cap_to(nvme_cap) * MSECS(500), t_wait_idle = get_timestamp(), t_idle;
	u32 cfg = nvme->config = 0;
	while (1) {
		u32 status = nvme->status;
		info("CSTS: %08"PRIx32"\n", status);
		t_idle = get_timestamp();
		if (~status & NVME_CSTS_RDY) {break;}
		if (t_idle - t_wait_idle > timeout) {
			infos("Controller idle timeout\n");
			goto shut_down_log;
		}
		usleep(1000);
	}
	nvme->adminq_attr = 3 << 16 | 3;
	struct nvme_cmd *sq = (struct nvme_cmd *)wt_buf[WTBUF_ASQ];
	struct nvme_completion *cq = (struct nvme_completion *)uncached_buf[UBUF_ACQ];
	memset(cq, 0, sizeof(*cq) * 4);
	nvme->adminsq_base = (u64)(uintptr_t)sq;
	nvme->admincq_base = (u64)(uintptr_t)cq;
	nvme->config = cfg |= NVME_CMDSET_NVM << NVME_CC_CSS_SHIFT | 0 << NVME_CC_MPS_SHIFT | 0 << NVME_CC_AMS_SHIFT;
	nvme->config = cfg |= NVME_CC_EN;
	timestamp_t t_ready;
	while (1) {
		u32 status = nvme->status;
		info("CSTS: %08"PRIx32"\n", status);
		t_ready = get_timestamp();
		if (status & NVME_CSTS_RDY) {break;}
		if (t_ready - t_idle > timeout) {
			info("Controller ready timeout after %"PRIuTS" μs\n", (t_ready - t_idle) / TICKS_PER_MICROSECOND);
			goto shut_down_log;
		}
		usleep(1000);
	}
	info("Controller ready after %"PRIuTS"  μs\n", (t_ready - t_idle) / TICKS_PER_MICROSECOND);
	u32 dstrd = 4 << nvme_extr_cap_dstrd(nvme_cap);
	volatile _Atomic u32 *sqdb = (_Atomic u32 *)((uintptr_t)nvme + 0x1000), *cqdb = (_Atomic u32 *)((uintptr_t)nvme + 0x1000 + dstrd);
	memset(sq, 0, sizeof(*sq));
	sq->opc = NVME_ADMIN_IDENTIFY;
	sq->cid = 0;
	const u8 *idctl = uncached_buf[UBUF_IDCTL];
	sq->dptr[0] = (u64)(uintptr_t)idctl;
	sq->dptr[1] = 0;
	sq->dw10 = NVME_IDENTIFY_CONTROLLER;
	atomic_store_explicit(sqdb, 1, memory_order_release);
	timestamp_t shutdown_timeout = MSECS(1000);
	switch (wait_single_command(nvme, cq + 0, 1)) {
	case IOST_OK: break;
	case IOST_LOCAL: case IOST_GLOBAL: goto shut_down_nvme;
	default: abort();
	}
	dump_mem(cq + 0, sizeof(*cq));
	atomic_store_explicit(cqdb, 1, memory_order_release);
	dump_mem(idctl, 0x1000);
	timestamp_t rtd3e = nvme_extr_idctl_rtd3e(idctl);
	info("RTD3E: %"PRIuTS" μs\n", rtd3e);
	shutdown_timeout = rtd3e * TICKS_PER_MICROSECOND;
	if (!shutdown_timeout) {shutdown_timeout = MSECS(1000);}
	u32 num_ns = nvme_extr_idctl_nn(idctl);
	if (!num_ns) {
		infos("controller has no valid NSIDs\n");
		goto shut_down_nvme;
	}
	memset(sq + 1, 0, sizeof(*sq));
	sq[1].opc = NVME_ADMIN_IDENTIFY;
	sq[1].cid = 0;
	sq[1].nsid = 1;
	const u8 *idns = uncached_buf[UBUF_IDNS];
	sq[1].dptr[0] = (u64)(uintptr_t)idns;
	sq[1].dptr[1] = 0;
	sq[1].dw10 = NVME_IDENTIFY_NS;
	atomic_store_explicit(sqdb, 2, memory_order_release);
	switch (wait_single_command(nvme, cq + 1, 1)) {
	case IOST_OK: break;
	case IOST_LOCAL: case IOST_GLOBAL: goto shut_down_nvme;
	default: abort();
	}
	atomic_store_explicit(cqdb, 2, memory_order_release);
	dump_mem(idns, 0x180);
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
	u32 block_size = 1 << lbaf[2];
	info("namespace has %"PRIu64" (0x%"PRIx64") %"PRIu32"-byte sectors\n", ns_size, ns_size, block_size);
	if ((nvme_extr_idctl_sqes(idctl) & 15) > 6 || (nvme_extr_idctl_cqes(idctl) & 15) > 4) {
		info("unsupported queue entry sizes\n");
		goto shut_down_nvme;
	}
	nvme->config = cfg |= 4 << NVME_CC_IOCQES_SHIFT | 6 << NVME_CC_IOSQES_SHIFT;
	memset(sq+2, 0, sizeof(*sq));
	sq[2].opc = NVME_ADMIN_SET_FEATURES;
	sq[2].cid = 0;
	sq[2].dw10 = NVME_FEATURE_NUM_QUEUES;
	sq[2].dw11 = (1 - 1) << 16 | (1 - 1);
	atomic_store_explicit(sqdb, 3, memory_order_release);
	switch (wait_single_command(nvme, cq + 2, 1)) {
	case IOST_OK: break;
	case IOST_LOCAL: case IOST_GLOBAL: goto shut_down_nvme;
	default: abort();
	}
	dump_mem(cq + 2, sizeof(*cq));
	atomic_store_explicit(cqdb, 3, memory_order_release);
	struct nvme_cmd *cmd;
	cmd = sq + 3;
	memset(cmd, 0, sizeof(*cmd));
	cmd->opc = NVME_ADMIN_CREATE_IOCQ;
	cmd->cid = 0;
	struct nvme_completion *iocq = (struct nvme_completion *)(uncached_buf + UBUF_IOCQ);
	memset(iocq, 0, sizeof(*iocq) * 4);
	cmd->dptr[0] = (u64)(uintptr_t)iocq;
	cmd->dw10 = 1 | 3 << 16;
	cmd->dw11 = 1;
	atomic_store_explicit(sqdb, 0, memory_order_release);
	switch (wait_single_command(nvme, cq + 3, 1)) {
	case IOST_OK: break;
	case IOST_LOCAL: case IOST_GLOBAL: goto shut_down_nvme;
	default: abort();
	}
	dump_mem(cq + 3, sizeof(*cq));
	atomic_store_explicit(cqdb, 0, memory_order_release);
	cmd = sq;
	memset(cmd, 0, sizeof(*cmd));
	cmd->opc = NVME_ADMIN_CREATE_IOSQ;
	cmd->cid = 0;
	struct nvme_cmd *iosq = (struct nvme_cmd *)(wt_buf + WTBUF_IOSQ);
	cmd->dptr[0] = (u64)(uintptr_t)iosq;
	cmd->dw10 = 1 | 3 << 16;
	cmd->dw11 = 1 << 16 | 1;
	atomic_store_explicit(sqdb, 1, memory_order_release);
	switch (wait_single_command(nvme, cq + 0, 0)) {
	case IOST_OK: break;
	case IOST_LOCAL: case IOST_GLOBAL: goto shut_down_nvme;
	default: abort();
	}
	dump_mem(cq + 0, sizeof(*cq));
	atomic_store_explicit(cqdb, 1, memory_order_release);

	volatile _Atomic u32 *iosqdb = (_Atomic u32 *)((uintptr_t)nvme + 0x1000 + 2 * dstrd), *iocqdb = (_Atomic u32 *)((uintptr_t)nvme + 0x1000 + 3 * dstrd);
	cmd = iosq + 0;
	memset(cmd, 0, sizeof(*cmd));
	cmd->opc = NVME_NVM_READ;
	cmd->fuse_psdt = NVME_PSDT_PRP;
	cmd->cid = 0;
	cmd->nsid = 1;
	cmd->dptr[0] = (u64)(uintptr_t)uncached_buf[UBUF_DATA];
	cmd->cmd_spec[0] = 1 << 16 >> 9;
	atomic_store_explicit(iosqdb, 1, memory_order_release);
	switch (wait_single_command(nvme, iocq + 0, 1)) {
	case IOST_OK: break;
	case IOST_LOCAL: case IOST_GLOBAL: goto shut_down_nvme;
	default: abort();
	}
	dump_mem(iocq + 0, sizeof(*iocq));
	atomic_store_explicit(iocqdb, 1, memory_order_release);
	dump_mem(uncached_buf[UBUF_DATA], block_size);

shut_down_nvme:
	nvme->config = cfg | 1 << NVME_CC_SHN_SHIFT;
	timestamp_t t_wait_shutdown = get_timestamp(), t_shutdown;
	while (1) {
		u32 status = nvme->status;
		info("CSTS: %08"PRIx32"\n", status);
		t_shutdown = get_timestamp();
		if (nvme_extr_csts_shst(status) == 2) {
			nvme->config = cfg & ~NVME_CC_EN;
			goto out;
		}
		if (t_shutdown - t_wait_shutdown > shutdown_timeout) {break;}
		usleep(1000);
	}
	info("Controller shutdown timeout after %"PRIuTS" μs\n", (t_ready - t_idle) / TICKS_PER_MICROSECOND);
	nvme->config = cfg | 2 << NVME_CC_SHN_SHIFT;
	while (1) {
		u32 status = nvme->status;
		info("CSTS: %08"PRIx32"\n", status);
		t_shutdown = get_timestamp();
		if (nvme_extr_csts_shst(status) == 2) {break;}
		if (t_shutdown - t_wait_shutdown > 2*shutdown_timeout) {
			info("Controller abrupt shutdown timeout after %"PRIuTS" μs\n", (t_ready - t_idle) / TICKS_PER_MICROSECOND);
			break;
		}
		usleep(1000);
	}
	nvme->config = cfg & ~NVME_CC_EN;
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
