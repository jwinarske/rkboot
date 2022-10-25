// SPDX-License-Identifier: CC0-1.0
#pragma once
#include <stdint.h>

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

#define USB_DEV2HOST 0x80
#define USB_STD_REQ 0
#define USB_CLASS_REQ 0x20
#define USB_VENDOR_REQ 0x40
#define USB_DEV_REQ 0
#define USB_IFACE_REQ 1
#define USB_EP_REQ 2
#define USB_OTHER_REQ 3

enum usb_request_code {
	USB_GET_STATUS = 0,
	USB_CLEAR_FEATURE = 1,
	USB_SET_FEATURE = 3,
	USB_SET_ADDRESS = 5,
	USB_GET_DESCRIPTOR = 6,
	USB_SET_DESCRIPTOR = 7,
	USB_GET_CONFIGURATION = 8,
	USB_SET_CONFIGURATION = 9,
	USB_GET_INTERFACE = 10,
	USB_SET_INTERFACE = 11,
	USB_SYNCH_FRAME = 12,
};

enum usb_speed {
	USB_LOW_SPEED = 0,
	USB_FULL_SPEED,
	USB_HIGH_SPEED,
	USB_SUPER_SPEED,
	USB_SUPER_SPEED_PLUS,
	NUM_USB_SPEED
};

static const uint16_t usb_max_packet_size[NUM_USB_SPEED] = {
	[USB_LOW_SPEED] = 8,
	[USB_FULL_SPEED] = 64,
	[USB_HIGH_SPEED] = 512,
	[USB_SUPER_SPEED] = 1024,
	[USB_SUPER_SPEED_PLUS] = 1024,
};

struct usb_setup {
	uint8_t bRequestType;
	uint8_t bRequest;
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;
};
