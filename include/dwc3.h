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

enum dwc3_ep0phase {
	DWC3_EP0_SETUP,
	DWC3_EP0_DATA,
	DWC3_EP0_STATUS2,
	DWC3_EP0_STATUS3,
	DWC3_EP0_DISCONNECTED,
};

typedef u32 buf_id_t;

struct dwc3_state;
struct dwc3_gadget_ops {
	buf_id_t (*prepare_descriptor)(struct dwc3_state *st, const struct usb_setup *req);
	_Bool (*set_configuration)(struct dwc3_state *st, const struct usb_setup *req);
	void (*release_buffer)(struct dwc3_state *st, buf_id_t buf);
	void (*ep_event)(struct dwc3_state *st, u32 event);
};

struct dwc3_state {
	volatile struct dwc3_regs *regs;
	struct dwc3_bufs *bufs;
	const struct dwc3_gadget_ops *ops;
	u32 hwparams[9];
	u32 num_ep;
	u32 evt_slots, evt_pos;
	enum usb_speed speed;
	u32 dcfg;

	struct usb_setup last_setup;
	enum dwc3_ep0phase ep0phase;
	buf_id_t ep0_buf;
	u64 ep0_buf_addr;
	u32 ep0_buf_size;
};

void dwc3_submit_trb(volatile struct dwc3_regs *dwc3, u32 ep, struct xhci_trb *trb, u64 param, u32 status, u32 ctrl);
void dwc3_post_depcmd(volatile struct dwc3_regs *dwc3, u32 ep, u32 cmd, u32 par0, u32 par1, u32 par2);
void dwc3_wait_depcmd(volatile struct dwc3_regs *dwc3, u32 ep);
void dwc3_new_configuration(volatile struct dwc3_regs *dwc3, u32 max_packet_size, u32 num_ep);
void dwc3_configure_bulk_ep(volatile struct dwc3_regs *dwc3, u32 ep, u32 action, u32 max_packet_size);
void dwc3_irq(struct dwc3_state *dwc3);
void dwc3_start(volatile struct dwc3_regs *dwc3);
void dwc3_halt(volatile struct dwc3_regs *dwc3);


static const enum usb_speed dwc3_speed_to_standard[8] = {
	USB_HIGH_SPEED,
	USB_FULL_SPEED,
	USB_LOW_SPEED,
	NUM_USB_SPEED,
	USB_SUPER_SPEED,
	USB_SUPER_SPEED_PLUS,
	NUM_USB_SPEED,
	NUM_USB_SPEED,
};
