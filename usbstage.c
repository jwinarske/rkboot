#include <inttypes.h>
#include <stdlib.h>
#include <stdatomic.h>

#include <byteorder.h>

#include <main.h>
#include <rk3399.h>
#include <stage.h>
#include <mmu.h>
#include <uart.h>
#include <dwc3_regs.h>
#include <dump_mem.h>

const struct mapping initial_mappings[] = {
	{.first = 0, .last = 0xf7ffffff, .flags = MEM_TYPE_NORMAL},
	{.first = 0xf8000000, .last = 0xff8bffff, .flags = MEM_TYPE_DEV_nGnRnE},
	{.first = 0xff8c0000, .last = 0xff8effff, .flags = MEM_TYPE_NORMAL},
	{.first = 0xff8f0000, .last = 0xffffffff, .flags = MEM_TYPE_DEV_nGnRnE},
	{.first = 0, .last = 0, .flags = 0}
};

const struct address_range critical_ranges[] = {
	{.first = (void *)0xff8c0000, .last = __end__ - 1},
	{.first = uart, .last = uart},
	ADDRESS_RANGE_INVALID
};

u32 event_buffer[64];
size_t event_offset = 0;

void post_depcmd(volatile struct dwc3_regs *dwc3, u32 ep, u32 cmd, u32 par0, u32 par1, u32 par2) {
	info("depcmd@%08"PRIx64" 0x%08"PRIx32" %"PRIx32" %"PRIx32" %"PRIx32"\n", (u64)&dwc3->device_ep_cmd[ep].par2, cmd, par0, par1, par2);
	dwc3->device_ep_cmd[ep].par2 = par2;
	dwc3->device_ep_cmd[ep].par1 = par1;
	dwc3->device_ep_cmd[ep].par0 = par0;
	dwc3->device_ep_cmd[ep].cmd = cmd | 1 << 10;
}

void wait_depcmd(volatile struct dwc3_regs *dwc3, u32 ep) {
	while (dwc3->device_ep_cmd[ep].cmd & 1 << 10) {
		__asm__("yield");
	}
}

struct xhci_trb {
	_Alignas(16)
	u64 param;
	u32 status;
	u32 control;
};

static void write_trb(struct xhci_trb *trb, u64 param, u32 status, u32 ctrl) {
	trb->param = to_le64(param);
	trb->status = to_le32(status);
	atomic_thread_fence(memory_order_release);
	trb->control = to_le32(ctrl);
}

void submit_trb(volatile struct dwc3_regs *dwc3, u32 ep, struct xhci_trb *trb, u64 param, u32 status, u32 ctrl) {
	printf("submit TRB: %016"PRIx64" %08"PRIx32" %"PRIx32"\n", param, status, ctrl);
	write_trb(trb, param, status, ctrl | DWC3_TRB_HWO);
	atomic_thread_fence(memory_order_release);
	post_depcmd(dwc3, ep, DWC3_DEPCMD_START_XFER, (u32)((u64)trb >> 32), (u32)(u64)trb, 0);
}

enum usb_descriptor_type {
	USB_DEVICE_DESC = 1,
	USB_CONFIG_DESC = 2,
	/* … */
	USB_INTERFACE_DESC = 4,
	USB_EP_DESC = 5,
	/* … */
};

enum usb_transfer_type {
	USB_CONTROL = 0,
	USB_ISOCHRONOUS = 1,
	USB_BULK = 2,
	USB_INTERRUPT = 3,
};

struct usb_setup {
	u8 bRequestType;
	u8 bRequest;
	u16 wValue;
	u16 wIndex;
	u16 wLength;
};

static const u32 num_ep = 13;

static void dwc3_write_dctl(volatile struct dwc3_regs *dwc3, u32 val) {
	dwc3->device_control = val & ~(u32)DWC3_DCTL_LST_CHANGE_REQ_MASK;
}

static void configure_ep(volatile struct dwc3_regs *dwc3, u32 ep, u32 cfg0, u32 cfg1) {
	cfg1 |= 0;	/*interrupt number*/
	cfg1 |= ep << 25 & 0x3e000000;	/*endpoint number*/
	if (ep & 1) {
		cfg0 |= ep << 16 & 0x3e0000;	/* FIFO number */
	}
	post_depcmd(dwc3, ep, DWC3_DEPCMD_SET_EP_CONFIG, cfg0, cfg1, 0);
}

static void configure_control_ep(volatile struct dwc3_regs *dwc3, u32 ep, u32 action, u32 max_packet_size) {
	u32 cfg0 = action;
	cfg0 |= max_packet_size << 3 | USB_CONTROL << 1;
	u32 cfg1 = DWC3_DEPCFG1_XFER_COMPLETE_EN | DWC3_DEPCFG1_XFER_IN_PROGRESS_EN | DWC3_DEPCFG1_XFER_NOT_READY_EN;
	configure_ep(dwc3, ep, cfg0, cfg1);
}
static void configure_bulk_ep(volatile struct dwc3_regs *dwc3, u32 ep, u32 action, u32 max_packet_size) {
	u32 cfg0 = action | max_packet_size << 3 | USB_BULK << 1;
	u32 cfg1 = DWC3_DEPCFG1_XFER_IN_PROGRESS_EN | DWC3_DEPCFG1_XFER_NOT_READY_EN | DWC3_DEPCFG1_XFER_COMPLETE_EN;
	configure_ep(dwc3, ep, cfg0, cfg1);
}

static void dwc3_new_configuration(volatile struct dwc3_regs *dwc3, u32 max_packet_size) {
	post_depcmd(dwc3, 0, DWC3_DEPCMD_START_NEW_CONFIG, 0, 0, 0);
	wait_depcmd(dwc3, 0);
	printf("GSTS: %08"PRIx32" DSTS: %08"PRIx32"\n", dwc3->global_status, dwc3->device_status);
	for_range(ep, 0, 2) {
		configure_control_ep(dwc3, ep, DWC3_DEPCFG0_INIT, max_packet_size);
	}
	for_range(ep, 0, num_ep) {
		wait_depcmd(dwc3, ep);
		post_depcmd(dwc3, ep, DWC3_DEPCMD_SET_XFER_RSC_CONFIG, 1, 0, 0);
	}
	dwc3->device_endpoint_enable = 3;
	for_range(ep, 0, num_ep) {
		wait_depcmd(dwc3, ep);
		printf("%"PRIu32": %08"PRIx32"\n", ep, dwc3->device_ep_cmd[ep].cmd);
	}
}

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

struct dwc3_state {
	enum dwc3_ep0phase ep0phase;
	u32 dcfg;
};

const u8 _Alignas(64) device_desc[18] = {
	0x12, USB_DEVICE_DESC,	/* length, descriptor type (device) */
	0x00, 0x02,	/* USB version: 2.0 */
	0x00, 0x00, 0x00,	/* device class, subclass, protocol: defined by interface */
	0x40,	/* max packet size for EP0: 64 */
	0x07, 0x22, 0x0c, 0x33,	/* vendor (2270: Rockchip) and product (330c: RK3399) */
	0x00, 0x01,	/* device version: 1.0 */
	0x00, 0x00, 0x00,	/* manufacturer, product, serial number */
	01,	/* number of configurations */
};

const u8 _Alignas(64) conf_desc[32] = {
	9, USB_CONFIG_DESC,	/* 9-byte configuration descriptor */
		0x20, 0x00,	/* total length 32 bytes */
		1,	/* 1 interface */
		1,	/* configuration value 1 */
		0,	/* configuration 0 */
		0x80,	/* no special attributes */
		0xc8,	/* max. power 200 mA */
	9, USB_INTERFACE_DESC,	/* 9-byte interface descriptor */
		0,	/* interface number 0 */
		0,	/* alternate setting */
		2,	/* 2 endpoints */
		0xff, 6, 5,	/* class: vendor specified */
		0,	/* interface 0 */
	7, USB_EP_DESC,	/* 7-byte endpoint descriptor */
		0x81,	/* address 81 (device-to-host) */
		0x02,	/* attributes: bulk endpoint */
		0x40, 0x00, /* max. packet size: 64 */
		0,	/* interval value 0 */
	7, USB_EP_DESC,	/* 7-byte endpoint descriptor */
		0x02,	/* address 2 (host-to-device) */
		0x02,	/* attributes: bulk endpoint */
		0x40, 0x0,	/* max. packet size: 64 */
		0,	/* interval value 0 */
};

enum {LAST_TRB = DWC3_TRB_ISP_IMI | DWC3_TRB_IOC | DWC3_TRB_LAST};

static void process_ep0_event(const struct dwc3_setup *setup, struct dwc3_state *st, u32 event) {
	volatile struct dwc3_regs *dwc3 = setup->dwc3;
	struct dwc3_bufs *bufs = setup->bufs;
	u32 UNUSED ep = event >> 1 & 0x1f, type = event >> 6 & 15, status = event >> 12 & 15;
	printf(" type%"PRIu32" status%"PRIu32, type, status);
	const enum dwc3_ep0phase phase = st->ep0phase;
	if (type == DWC3_DEPEVT_XFER_COMPLETE) {
		if (phase == DWC3_EP0_SETUP) {
			puts(" setup complete\n");
			atomic_thread_fence(memory_order_acquire);
			struct usb_setup *req = &bufs->setup_packet;
			dump_mem(req, 16);
			u16 req_header = (u16)req->bRequestType << 8 | req->bRequest;
			const u16 wLength = from_le16(req->wLength);
			const u16 wValue = from_le16(req->wValue);
			if ((req_header & 0xe0ff) == 0x8006) { /* GET_DESCRIPTOR */
				assert(wLength);
				const u8 descriptor_type = wValue >> 8;
				const u8 *desc;
				u16 transfer_length = wLength;
				if (descriptor_type == USB_DEVICE_DESC) {
					desc = device_desc;
					if (wLength > 18) {transfer_length = 18;}
				} else {
					assert(descriptor_type == USB_CONFIG_DESC);
					desc = conf_desc;
					if (wLength > 32) {transfer_length = 32;}
				}
				submit_trb(dwc3, 1, &bufs->ep0_trb, (u64)desc, transfer_length, DWC3_TRB_TYPE_CONTROL_DATA | LAST_TRB);
				st->ep0phase = DWC3_EP0_DATA;
			} else if ((req_header & 0xe0ff) == 0x0005) { /* SET_ADDRESS */
				assert(!wLength);
				assert(wValue < 0x80);
				u32 tmp = st->dcfg & ~(u32)DWC3_DCFG_DEVADDR_MASK;
				dwc3->device_config = st->dcfg = tmp | wValue << 3;
				st->ep0phase = DWC3_EP0_STATUS2;
			} else if ((req_header & 0xe0ff) == 0x0009) { /* SET_CONFIGURATION */
				assert(!wLength);
				assert(wValue == 1);
				dwc3_write_dctl(dwc3, dwc3->device_control | DWC3_DCTL_ACCEPT_U1 | DWC3_DCTL_ACCEPT_U2);

				struct xhci_trb *ep4_trb = (struct xhci_trb *)0xff8c0210;
				write_trb(ep4_trb, 0xff8c0400, 64, DWC3_TRB_TYPE_NORMAL | DWC3_TRB_CSP | LAST_TRB | DWC3_TRB_HWO);
				atomic_thread_fence(memory_order_release);
				wait_depcmd(dwc3, 4);
				post_depcmd(dwc3, 4, DWC3_DEPCMD_START_XFER | DWC3_DEPCMD_CMDIOC, (u32)((u64)ep4_trb >> 32), (u32)(u64)ep4_trb, 0);
				wait_depcmd(dwc3, 4);
				st->ep0phase = DWC3_EP0_STATUS2;
			} else {die("unexpected control request: %04"PRIx16"\n", req_header);}
		} else if (phase == DWC3_EP0_DATA) {
			puts(" data complete");
			st->ep0phase = DWC3_EP0_STATUS3;
		} else if (phase == DWC3_EP0_STATUS2 || phase == DWC3_EP0_STATUS3) {
			puts(" restarting EP0 cycle");
			submit_trb(dwc3, 0, &bufs->ep0_trb, (u64)&bufs->setup_packet, 8, DWC3_TRB_TYPE_CONTROL_SETUP | LAST_TRB);
			st->ep0phase = DWC3_EP0_SETUP;
		} else {die("XferComplete in unexpected EP0 phase");}
		assert(st->ep0phase != phase);
	} else if (type == DWC3_DEPEVT_XFER_NOT_READY) {
		u32 state = status & 3;
		if (state == 1 && phase == DWC3_EP0_DATA) {
			puts(" data not ready");
		} else if (state == 2 && (phase == DWC3_EP0_STATUS2 || phase == DWC3_EP0_STATUS3)) {
			puts(" status not ready");
			assert(phase == DWC3_EP0_STATUS3 || ep == 1);
			u32 cmd = phase == DWC3_EP0_STATUS2
				? DWC3_TRB_TYPE_CONTROL_STATUS2
				: DWC3_TRB_TYPE_CONTROL_STATUS3;
			submit_trb(dwc3, ep, &bufs->ep0_trb, (u64)&bufs->setup_packet, 0, cmd | LAST_TRB);
		} else {die(" unexpected XferNotReady");}
	} else {die(" unexpected ep0 event\n");}
}

static void process_device_event(const struct dwc3_setup *setup, struct dwc3_state *st, u32 event) {
	volatile struct dwc3_regs *dwc3 = setup->dwc3;
	switch (event >> 8 & 15) {
	case DWC3_DEVT_DISCONNECT:
		puts(" disconnect");
		dwc3_write_dctl(dwc3, dwc3->device_control & ~(u32)DWC3_DCTL_INIT_U1 & ~(u32)DWC3_DCTL_INIT_U2);
		st->ep0phase = DWC3_EP0_DISCONNECTED;
		break;
	case DWC3_DEVT_RESET:
		dwc3_write_dctl(dwc3, dwc3->device_control & ~(u32)DWC3_DCTL_TEST_CONTROL_MASK);
		for_range(ep, 1, num_ep) {	/* don't clear DEP0 */
			post_depcmd(dwc3, ep, DWC3_DEPCMD_CLEAR_STALL, 0, 0, 0);
		}
		dwc3->device_config = st->dcfg &= ~(u32)DWC3_DCFG_DEVADDR_MASK;
		puts(" USB reset");
		st->ep0phase = DWC3_EP0_SETUP;
		break;
	case DWC3_DEVT_CONNECTION_DONE:
		printf(" connection done, status 0x%08"PRIx32, dwc3->device_status);
		assert((dwc3->device_status & 7) == 0);
		dwc3_write_dctl(dwc3, dwc3->device_control & ~(u32)DWC3_DCTL_HIRDTHRES_MASK & ~(u32)DWC3_DCTL_L1_HIBERNATION_EN);
		dwc3->device_endpoint_enable |= 1 << 4;
		break;
	case DWC3_DEVT_LINK_STATE_CHANGE:
		puts(" link status change");
		break;
	default: abort();
	}
}

enum {MIN_CACHELINE_SIZE = 32, MAX_CACHELINE_SIZE = 128};

static void flush_range(void *ptr, size_t size) {
	atomic_thread_fence(memory_order_acq_rel);	/* as per ARMv8 ARM, cache maintenance is only ordered by barriers that order both loads and stores */
	void *end = ptr + size;
	while (ptr < end) {
		__asm__ volatile("dc civac, %0" : : "r"(ptr) : "memory");
		ptr += MIN_CACHELINE_SIZE;
	}
}

static void invalidate_range(void *ptr, size_t size) {
	void *end = ptr + size;
	while (ptr < end) {
		__asm__ volatile("dc ivac, %0" : : "r"(ptr) : "memory");
		ptr += MIN_CACHELINE_SIZE;
	}
}

_Noreturn void main(u64 sctlr) {
	puts("usbstage\n");
	struct stage_store store;
	store.sctlr = sctlr;
	stage_setup(&store);
	mmu_setup(initial_mappings, critical_ranges);
	assert(sizeof(event_buffer) >= 256 && sizeof(event_buffer) <= 0xfffc && sizeof(event_buffer) % 4 == 0);
	volatile struct dwc3_regs *dwc3 = (volatile struct dwc3_regs *)0xfe80c100;

	mmu_unmap_range(0xff8c0000, 0xff8c0fff);
	mmu_map_range(0xff8c0000, 0xff8c0fff, 0xff8c0000, MEM_TYPE_WRITE_THROUGH);
	dsb_st();
	const struct dwc3_setup setup = {
		.dwc3 = dwc3,
		.bufs = (struct dwc3_bufs *)0xff8c0100,
		.evt_slots = 64,
		.hwparams = {
			dwc3->hardware_parameters[0],
			dwc3->hardware_parameters[1],
			dwc3->hardware_parameters[2],
			dwc3->hardware_parameters[3],
			dwc3->hardware_parameters[4],
			dwc3->hardware_parameters[5],
			dwc3->hardware_parameters[6],
			dwc3->hardware_parameters[7],
			dwc3->hardware_parameters8,
		},
	};
	const u32 mdwidth = setup.hwparams[0] >> 8 & 0xff;
	const u32 ram2_bytes = (setup.hwparams[7] >> 16) * (mdwidth / 8);
	struct dwc3_state st = {
		.ep0phase = DWC3_EP0_DISCONNECTED,
		.dcfg = DWC3_HIGH_SPEED | 0 << 12 | (ram2_bytes / 512) << 17 | DWC3_DCFG_LPM_CAPABLE,
	};

	for_range(i, 0, setup.evt_slots) {setup.bufs->event_buffer[i] = 0;}
	flush_range(setup.bufs->event_buffer, sizeof(setup.bufs->event_buffer));

#if 1
	debug("soft-resetting the USB controller\n");
	dwc3->device_control = DWC3_DCTL_SOFT_RESET;
	u64 softreset_start = get_timestamp();
	while (dwc3->device_control & DWC3_DCTL_SOFT_RESET) {
		if (get_timestamp() - softreset_start > 100000 * CYCLES_PER_MICROSECOND) {
			die("USB softreset timeout\n");
		}
		udelay(1000);
	}
	dwc3->global_control |= DWC3_GCTL_CORE_SOFT_RESET;
	dwc3->phy_config |= DWC3_GUSB2PHYCFG_PHY_SOFT_RESET;
	dwc3->pipe_control |= DWC3_GUSB2PHYCFG_PHY_SOFT_RESET;
	udelay(100000);
	dwc3->phy_config &= ~(u32)DWC3_GUSB2PHYCFG_PHY_SOFT_RESET;
	dwc3->pipe_control &= ~(u32)DWC3_GUSB3PIPECTL_PHY_SOFT_RESET;
	udelay(100000);
	dwc3->global_status = 0x30;
	dwc3->global_control &= ~(u32)DWC3_GCTL_CORE_SOFT_RESET
		& ~(u32)DWC3_GCTL_DISABLE_SCRAMBLING
		& ~(u32)DWC3_GCTL_SCALEDOWN_MASK;
	debug("soft reset finished\n");
	dwc3->user_control1 |= DWC3_GUCTL1_USB3_USE_USB2_CLOCK;
	dwc3->pipe_control |= DWC3_GUSB3PIPECTL_SUSPEND_ENABLE;
	udelay(100000);
	u32 val = dwc3->phy_config;
	val &= ~(u32)DWC3_GUSB2PHYCFG_ENABLE_SLEEP
		& ~(u32)DWC3_GUSB2PHYCFG_SUSPEND_PHY
		& ~(u32)DWC3_GUSB2PHYCFG_USB2_FREE_CLOCK
		& ~(u32)DWC3_GUSB2PHYCFG_TURNAROUND_MASK;
	val |= DWC3_GUSB2PHYCFG_PHY_INTERFACE
		| DWC3_GUSB2PHYCFG_TURNAROUND(5);
	dwc3->phy_config = val;
	udelay(100000);
	printf("GSTS: %08"PRIx32" DSTS: %08"PRIx32"\n", dwc3->global_status, dwc3->device_status);

	dwc3->global_control |= DWC3_GCTL_PORT_CAP_DEVICE;
	/* HiSpeed */
	dwc3->device_config = st.dcfg;
	printf("DCFG: %08"PRIx32"\n", dwc3->device_config);
	udelay(10000);
	printf("new config GSTS: %08"PRIx32" DSTS: %08"PRIx32"\n", dwc3->global_status, dwc3->device_status);
#else
	{
		u32 tmp = dwc3->device_control & ~(u32)DWC3_DCTL_RUN;
		dwc3_write_dctl(dwc3, tmp | DWC3_DCTL_KEEP_CONNECT);
	}
#endif
	u32 max_packet_size = 64;
	dwc3_new_configuration(dwc3, max_packet_size);
	configure_bulk_ep(dwc3, 4, DWC3_DEPCFG0_INIT, 64);
	wait_depcmd(dwc3, 4);
	printf("DALEPENA: %"PRIx32"\n", dwc3->device_endpoint_enable);

	dwc3->event_buffer_address[0] = (u32)(u64)&setup.bufs->event_buffer;
	dwc3->event_buffer_address[1] = (u32)((u64)&setup.bufs->event_buffer >> 32);
	atomic_thread_fence(memory_order_release);
	dwc3->event_buffer_size = 256;
	dwc3->device_event_enable = 0x1e1f;

	submit_trb(dwc3, 0, &setup.bufs->ep0_trb, (u64)&setup.bufs->setup_packet, 8, DWC3_TRB_TYPE_CONTROL_SETUP | LAST_TRB);
	wait_depcmd(dwc3, 0);
	printf("cmd %"PRIx32"\n", dwc3->device_ep_cmd[0].cmd);
	udelay(100);
	dwc3_write_dctl(dwc3, dwc3->device_control | DWC3_DCTL_RUN);
	printf("GSTS: %08"PRIx32" DSTS: %08"PRIx32"\n", dwc3->global_status, dwc3->device_status);

	{
		u32 evtcount = dwc3->event_count;
		printf("%"PRIu32" events pending, evtsiz0x%"PRIx32"\n", evtcount, dwc3->event_buffer_size);
	}
	timestamp_t last_status = get_timestamp();
	u32 evt_pos = 0;
	while (1) {
		u32 evtcount = dwc3->event_count;
		assert(evtcount % 4 == 0);
		evtcount /= 4;
		if (!evtcount) {
			timestamp_t now = get_timestamp();
			if (now - last_status > 300000 * TICKS_PER_MICROSECOND) {
				last_status = now;
				printf("GSTS: %08"PRIx32" DSTS: %08"PRIx32"\n", dwc3->global_status, dwc3->device_status);
			}
			__asm__("yield");
			continue;
		}
		printf("GSTS: %08"PRIx32" DSTS: %08"PRIx32"\n", dwc3->global_status, dwc3->device_status);
		dwc3->event_buffer_size = 256 | 1 << 31;
		atomic_thread_fence(memory_order_acq_rel);	/* prevent reordering of accesses to event buffer to before the flag is set */
		u32 *evtbuf = setup.bufs->event_buffer;
		for_range(i, 0, evtcount) {
			u32 event = evtbuf[evt_pos];
			evt_pos = (evt_pos + 1) % setup.evt_slots;
			printf("0x%08"PRIx32, event);
			if (event & 1) { /* generic event */
				assert((event >> 1 & 0x7f) == 0);
				process_device_event(&setup, &st, event);
			} else { /* endpoint event */
				u32 ep = event >> 1 & 0x1f;
				printf(" ep%"PRIu32, ep);
				if (ep <= 1) {
					process_ep0_event(&setup, &st, event);
				} else {
					u32 type = event >> 6 & 15, status = event >> 12 & 15;
					printf(" type%"PRIu32" status%"PRIx32, type, status);
					printf("DEPCMD: %"PRIx32"\n", dwc3->device_ep_cmd[ep].cmd);
					if (type == DWC3_DEPEVT_XFER_COMPLETE) {
						dump_mem((void*)0xff8c0400, 64);
					}
				}
			}
			puts("\n");
		}
		atomic_thread_fence(memory_order_acquire);
		invalidate_range(evtbuf, sizeof(setup.bufs->event_buffer));
		atomic_thread_fence(memory_order_release);
		dwc3->event_count = evtcount * 4;
		dwc3->event_buffer_size = 256;
		puts("\n");
	}
}
