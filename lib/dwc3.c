/* SPDX-License-Identifier: CC0-1.0 */
#include <dwc3.h>
#include <dwc3_regs.h>
#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>

#include <cache.h>
#include <die.h>
#include <log.h>
#include <memaccess.h>

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
	trb->param = le64(param);
	trb->status = le32(status);
	atomic_thread_fence(memory_order_release);
	trb->control = le32(ctrl);
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
	u32 cfg1 = DWC3_DEPCFG1_XFER_COMPLETE_EN;
	configure_ep(dwc3, ep, cfg0, cfg1);
}
void dwc3_configure_bulk_ep(volatile struct dwc3_regs *dwc3, u32 ep, u32 action, u32 max_packet_size) {
	u32 cfg0 = action | max_packet_size << 3 | USB_BULK << 1;
	u32 cfg1 = DWC3_DEPCFG1_XFER_COMPLETE_EN;
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


static void dwc3_write_dctl(volatile struct dwc3_regs *dwc3, u32 val) {
	dwc3->device_control = val & ~(u32)DWC3_DCTL_LST_CHANGE_REQ_MASK;
}

enum {LAST_TRB = DWC3_TRB_ISP_IMI | DWC3_TRB_IOC | DWC3_TRB_LAST};

static void dwc3_ep0_restart(struct dwc3_state *st) {
	puts("restarting ep0");
	volatile struct dwc3_regs *dwc3 = st->regs;
	dwc3_post_depcmd(dwc3, 0, DWC3_DEPCMD_SET_STALL, 0, 0, 0);
	dwc3_wait_depcmd(dwc3, 0);
	struct dwc3_bufs *bufs = st->bufs;
	dwc3_submit_trb(dwc3, 0, &bufs->ep0_trb, (u64)&bufs->setup_packet, 8, DWC3_TRB_TYPE_CONTROL_SETUP | LAST_TRB);
	st->ep0phase = DWC3_EP0_SETUP;
}

static void dwc3_dispatch_control_request(struct dwc3_state *st, struct usb_setup *req) {
	volatile struct dwc3_regs *dwc3 = st->regs;
	struct dwc3_bufs *bufs = st->bufs;
	u16 req_header = (u16)req->bRequestType << 8 | req->bRequest;
	u16 val = le16(req->wValue), len = le16(req->wLength), idx = le16(req->wIndex);
	info("USB request: %02"PRIx8" %02"PRIx8" %04"PRIx16" %04"PRIx16" %04"PRIx16"\n",
		req->bRequestType, req->bRequest, val, idx, len
	);
	if ((req_header & 0xe0ff) == 0x8006) { /* GET_DESCRIPTOR */
		if (idx) {goto error;}
		st->ep0_buf = st->ops->prepare_descriptor(st, req);
		if (!st->ep0_buf) {goto error;}
		if (st->ep0_buf_size < len) {len = st->ep0_buf_size;}
		st->ep0phase = DWC3_EP0_DATA;
	} else if ((req_header & 0xe0ff) == 0x0005) { /* SET_ADDRESS */
		if (len || idx || val > 0x7f) {goto error;}
		// interestingly this register update must be done now, not after status
		// phase as one might expect. Perhaps this was designed this way
		// such that interrupt latency cannot lead to missing the address
		// change time window.
		info("setting address to %"PRIu16"\n", val);
		u32 tmp = st->dcfg & ~(u32)DWC3_DCFG_DEVADDR_MASK;
		tmp |= (u32)val << 3 & 0x3f8;
		dwc3->device_config = st->dcfg = tmp;
		st->ep0phase = DWC3_EP0_STATUS2;
	} else if ((req_header & 0xe0ff) == 0x0009) { /* SET_CONFIGURATION */
		if (len || idx) {goto error;}
		if (!st->ops->set_configuration(st, req)) {goto error;}
		dwc3_write_dctl(dwc3, dwc3->device_control | DWC3_DCTL_ACCEPT_U1 | DWC3_DCTL_ACCEPT_U2);
		st->ep0phase = DWC3_EP0_STATUS2;
	} else {goto error;}

	invalidate_range(req, sizeof(*req));
	if (st->ep0phase != DWC3_EP0_STATUS2) {
		assert(st->ep0phase == DWC3_EP0_DATA);
		dwc3_submit_trb(dwc3, req_header >> 15, &bufs->ep0_trb, st->ep0_buf_addr, len, DWC3_TRB_TYPE_CONTROL_DATA | LAST_TRB);
		return;
	}
	dwc3_submit_trb(dwc3, 1, &bufs->ep0_trb, 0, 0, DWC3_TRB_TYPE_CONTROL_STATUS2 | LAST_TRB);
	return;
error:
	dwc3_ep0_restart(st);
}

static void ep0_xfer_complete(struct dwc3_state *st, u32 event) {
	volatile struct dwc3_regs *dwc3 = st->regs;
	struct dwc3_bufs *bufs = st->bufs;
	const enum dwc3_ep0phase phase = st->ep0phase;
	u32 ep = event >> 1 & 0x1f;
	if (phase == DWC3_EP0_SETUP) {
		puts(" setup complete");
		atomic_thread_fence(memory_order_acquire);
		struct usb_setup *req = &bufs->setup_packet;
		dwc3_dispatch_control_request(st, req);
		invalidate_range(req, sizeof(*req));
	} else if (phase == DWC3_EP0_DATA) {
		puts(" data complete");
		st->ops->release_buffer(st, st->ep0_buf);
		st->ep0_buf = 0;
		st->ep0phase = DWC3_EP0_STATUS3;
		assert(ep == 1);
		dwc3_submit_trb(dwc3, 1- ep, &bufs->ep0_trb, 0, 0, DWC3_TRB_TYPE_CONTROL_STATUS3 | LAST_TRB);
	} else if (phase == DWC3_EP0_STATUS2 || phase == DWC3_EP0_STATUS3) {
		puts(" restarting EP0 cycle");
		dwc3_submit_trb(dwc3, 0, &bufs->ep0_trb, (u64)&bufs->setup_packet, 8, DWC3_TRB_TYPE_CONTROL_SETUP | LAST_TRB);
		st->ep0phase = DWC3_EP0_SETUP;
	} else {
		die("XferComplete in unexpected EP0 phase");
	}
	assert(st->ep0phase != phase);
}

static void process_device_event(struct dwc3_state *st, u32 event) {
	printf("Device event: %08"PRIx32"\n", event);
	assert((event >> 1 & 0x7f) == 0);
	volatile struct dwc3_regs *dwc3 = st->regs;
	switch (event >> 8 & 15) {
	case DWC3_DEVT_DISCONNECT:
		puts(" disconnect");
		dwc3_write_dctl(dwc3, dwc3->device_control & ~(u32)DWC3_DCTL_INIT_U1 & ~(u32)DWC3_DCTL_INIT_U2);
		st->ep0phase = DWC3_EP0_DISCONNECTED;
		break;
	case DWC3_DEVT_RESET:
		dwc3_write_dctl(dwc3, dwc3->device_control & ~(u32)DWC3_DCTL_TEST_CONTROL_MASK);
		for_range(ep, 1, st->num_ep) {	/* don't clear DEP0 */
			dwc3_post_depcmd(dwc3, ep, DWC3_DEPCMD_CLEAR_STALL, 0, 0, 0);
		}
		dwc3->device_config = st->dcfg &= ~(u32)DWC3_DCFG_DEVADDR_MASK;
		puts(" USB reset");
		st->ep0phase = DWC3_EP0_SETUP;
		break;
	case DWC3_DEVT_CONNECTION_DONE:
		printf(" connection done, status 0x%08"PRIx32, dwc3->device_status);
		enum usb_speed speed = dwc3_speed_to_standard[dwc3->device_status & 7];
		assert(speed < NUM_USB_SPEED);
		st->speed = speed;
		dwc3_write_dctl(dwc3, dwc3->device_control & ~(u32)DWC3_DCTL_HIRDTHRES_MASK & ~(u32)DWC3_DCTL_L1_HIBERNATION_EN);
		dwc3->device_endpoint_enable |= 1 << 4;
		break;
	case DWC3_DEVT_LINK_STATE_CHANGE:
		puts(" link status change");
		break;
	default: abort();
	}
}

void dwc3_irq(struct dwc3_state *st) {
	volatile struct dwc3_regs *dwc3 = st->regs;
	u32 evtcount = acquire32v(&dwc3->event_count);
	assert(evtcount % 4 == 0);
	evtcount /= 4;
	debug("GSTS: %08"PRIx32" DSTS: %08"PRIx32"\n", dwc3->global_status, dwc3->device_status);
	u32 *evtbuf = st->bufs->event_buffer;
	for_range(i, 0, evtcount) {
		u32 event = evtbuf[st->evt_pos];
		st->evt_pos = (st->evt_pos + 1) % st->evt_slots;

		if (event & 1) { /* generic event */
			process_device_event(st, event);
		} else { /* endpoint event */
			u32 ep = event >> 1 & 0x1f;
			u32 type = event >> 6 & 15, status = event >> 12 & 15;
			info("EP%"PRIu32": %08"PRIx32" type%"PRIu32" status%"PRIu32"\n", ep, event, type, status);
			if (ep <= 1) {
				if (type == DWC3_DEPEVT_XFER_COMPLETE) {
					ep0_xfer_complete(st, event);
				} else {die(" unexpected ep0 event\n");}
			} else {
				st->ops->ep_event(st, event);
			}
		}
	}
	invalidate_range(evtbuf, sizeof(st->bufs->event_buffer));
	release32v(&dwc3->event_count, evtcount * 4);
}

void dwc3_start(volatile struct dwc3_regs *dwc3) {
	dwc3_write_dctl(dwc3, dwc3->device_control | DWC3_DCTL_RUN);
}
void dwc3_halt(volatile struct dwc3_regs *dwc3) {
	dwc3_write_dctl(dwc3, dwc3->device_control & ~(u32)DWC3_DCTL_RUN);
	while (~dwc3->device_status & DWC3_DSTS_HALTED) {
		__asm__("yield");
	}
}
