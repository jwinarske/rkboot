/* SPDX-License-Identifier: CC0-1.0 */
#include <dwc3.h>
#include <dwc3_regs.h>

#include <stdatomic.h>
#include <inttypes.h>

#include <log.h>
#include <byteorder.h>

void dwc3_post_depcmd(volatile struct dwc3_regs *dwc3, u32 ep, u32 cmd, u32 par0, u32 par1, u32 par2) {
	info("depcmd@%08"PRIx64" 0x%08"PRIx32" %"PRIx32" %"PRIx32" %"PRIx32"\n", (u64)&dwc3->device_ep_cmd[ep].par2, cmd, par0, par1, par2);
	dwc3->device_ep_cmd[ep].par2 = par2;
	dwc3->device_ep_cmd[ep].par1 = par1;
	dwc3->device_ep_cmd[ep].par0 = par0;
	dwc3->device_ep_cmd[ep].cmd = cmd | 1 << 10;
}

void dwc3_wait_depcmd(volatile struct dwc3_regs *dwc3, u32 ep) {
	while (dwc3->device_ep_cmd[ep].cmd & 1 << 10) {
		__asm__("yield");
	}
}

static void write_trb(struct xhci_trb *trb, u64 param, u32 status, u32 ctrl) {
	trb->param = to_le64(param);
	trb->status = to_le32(status);
	atomic_thread_fence(memory_order_release);
	trb->control = to_le32(ctrl);
}

void dwc3_submit_trb(volatile struct dwc3_regs *dwc3, u32 ep, struct xhci_trb *trb, u64 param, u32 status, u32 ctrl) {
	printf("submit TRB: %016"PRIx64" %08"PRIx32" %"PRIx32"\n", param, status, ctrl);
	write_trb(trb, param, status, ctrl | DWC3_TRB_HWO);
	atomic_thread_fence(memory_order_release);
	dwc3_post_depcmd(dwc3, ep, DWC3_DEPCMD_START_XFER, (u32)((u64)trb >> 32), (u32)(u64)trb, 0);
}
static void configure_ep(volatile struct dwc3_regs *dwc3, u32 ep, u32 cfg0, u32 cfg1) {
	cfg1 |= 0;	/*interrupt number*/
	cfg1 |= ep << 25 & 0x3e000000;	/*endpoint number*/
	if (ep & 1) {
		cfg0 |= ep << 16 & 0x3e0000;	/* FIFO number */
	}
	dwc3_post_depcmd(dwc3, ep, DWC3_DEPCMD_SET_EP_CONFIG, cfg0, cfg1, 0);
}

static void configure_control_ep(volatile struct dwc3_regs *dwc3, u32 ep, u32 action, u32 max_packet_size) {
	u32 cfg0 = action;
	cfg0 |= max_packet_size << 3 | USB_CONTROL << 1;
	u32 cfg1 = DWC3_DEPCFG1_XFER_COMPLETE_EN | DWC3_DEPCFG1_XFER_IN_PROGRESS_EN | DWC3_DEPCFG1_XFER_NOT_READY_EN;
	configure_ep(dwc3, ep, cfg0, cfg1);
}
void dwc3_configure_bulk_ep(volatile struct dwc3_regs *dwc3, u32 ep, u32 action, u32 max_packet_size) {
	u32 cfg0 = action | max_packet_size << 3 | USB_BULK << 1;
	u32 cfg1 = DWC3_DEPCFG1_XFER_IN_PROGRESS_EN | DWC3_DEPCFG1_XFER_NOT_READY_EN | DWC3_DEPCFG1_XFER_COMPLETE_EN;
	configure_ep(dwc3, ep, cfg0, cfg1);
}

void dwc3_new_configuration(volatile struct dwc3_regs *dwc3, u32 max_packet_size, u32 num_ep) {
	dwc3_post_depcmd(dwc3, 0, DWC3_DEPCMD_START_NEW_CONFIG, 0, 0, 0);
	dwc3_wait_depcmd(dwc3, 0);
	printf("GSTS: %08"PRIx32" DSTS: %08"PRIx32"\n", dwc3->global_status, dwc3->device_status);
	for_range(ep, 0, 2) {
		configure_control_ep(dwc3, ep, DWC3_DEPCFG0_INIT, max_packet_size);
	}
	for_range(ep, 0, num_ep) {
		dwc3_wait_depcmd(dwc3, ep);
		dwc3_post_depcmd(dwc3, ep, DWC3_DEPCMD_SET_XFER_RSC_CONFIG, 1, 0, 0);
	}
	dwc3->device_endpoint_enable = 3;
	for_range(ep, 0, num_ep) {
		dwc3_wait_depcmd(dwc3, ep);
		printf("%"PRIu32": %08"PRIx32"\n", ep, dwc3->device_ep_cmd[ep].cmd);
	}
}
