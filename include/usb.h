/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

enum usb_descriptor_type {
	USB_DEVICE_DESC = 1,
	USB_CONFIG_DESC = 2,
	/* … */
	USB_INTERFACE_DESC = 4,
	USB_EP_DESC = 5,
	USB_DEVICE_QUALIFIER = 6,
	/* … */
};

enum usb_transfer_type {
	USB_CONTROL = 0,
	USB_ISOCHRONOUS = 1,
	USB_BULK = 2,
	USB_INTERRUPT = 3,
};

enum usb_speed {
	USB_LOW_SPEED = 0,
	USB_FULL_SPEED,
	USB_HIGH_SPEED,
	USB_SUPER_SPEED,
	USB_SUPER_SPEED_PLUS,
	NUM_USB_SPEED
};

static const u16 usb_max_packet_size[NUM_USB_SPEED] = {
	[USB_LOW_SPEED] = 8,
	[USB_FULL_SPEED] = 64,
	[USB_HIGH_SPEED] = 512,
	[USB_SUPER_SPEED] = 1024,
	[USB_SUPER_SPEED_PLUS] = 1024,
};

struct usb_setup {
	u8 bRequestType;
	u8 bRequest;
	u16 wValue;
	u16 wIndex;
	u16 wLength;
};
