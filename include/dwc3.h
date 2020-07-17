/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>
#include <usb.h>
#include <xhci_regs.h>

struct dwc3_bufs {
	u32 event_buffer[64];
	union {
		/* this is what the Linux driver does, since only one control transfer is handled at a time. Why not do the same here? */
		struct xhci_trb ep0_trb;
		struct usb_setup setup_packet;
	};
};

struct dwc3_setup {
	volatile struct dwc3_regs *dwc3;
	struct dwc3_bufs *bufs;
	const struct dwc3_gadget_ops *ops;
	u32 hwparams[9];
	u32 evt_slots;
};

enum dwc3_ep0phase {
	DWC3_EP0_SETUP,
	DWC3_EP0_DATA,
	DWC3_EP0_STATUS2,
	DWC3_EP0_STATUS3,
	DWC3_EP0_DISCONNECTED,
};

typedef u32 buf_id_t;

struct dwc3_state {
	enum dwc3_ep0phase ep0phase;
	enum usb_speed speed;
	u32 dcfg;
	buf_id_t ep0_buf;
	u64 ep0_buf_addr;
	u32 ep0_buf_size;
};

void dwc3_submit_trb(volatile struct dwc3_regs *dwc3, u32 ep, struct xhci_trb *trb, u64 param, u32 status, u32 ctrl);
void dwc3_post_depcmd(volatile struct dwc3_regs *dwc3, u32 ep, u32 cmd, u32 par0, u32 par1, u32 par2);
void dwc3_wait_depcmd(volatile struct dwc3_regs *dwc3, u32 ep);
void dwc3_new_configuration(volatile struct dwc3_regs *dwc3, u32 max_packet_size, u32 num_ep);
void dwc3_configure_bulk_ep(volatile struct dwc3_regs *dwc3, u32 ep, u32 action, u32 max_packet_size);
