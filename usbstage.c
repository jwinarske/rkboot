#include <main.h>
#include <rk3399.h>
#include <stage.h>
#include <mmu.h>
#include <uart.h>
#include <inttypes.h>
#include <stdlib.h>
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

void submit_trb(volatile struct dwc3_regs *dwc3, u32 ep, volatile u32 *trb, u64 param, u32 status, u32 ctrl) {
	printf("submit TRB: %016"PRIx64" %08"PRIx32" %"PRIx32"\n", param, status, ctrl);
	trb[0] = (u32)param;
	trb[1] = (u32)(param >> 32);
	trb[2] = status;
	trb[3] = ctrl | DWC3_TRB_HWO;
	post_depcmd(dwc3, ep, DWC3_DEPCMD_START_XFER, (u32)((u64)trb >> 32), (u32)(u64)trb, 0);
	//wait_depcmd(dwc3, ep);
}

enum {
	USB_EP_TYPE_CONTROL = 0,
	USB_EP_TYPE_ISOCHRONOUS = 1,
	USB_EP_TYPE_BULK = 2,
	USB_EP_TYPE_INTERRUPT = 3,
};

static const u32 num_ep = 13;

static void dwc3_write_dctl(volatile struct dwc3_regs *dwc3, u32 val) {
	dwc3->device_control = val & ~(u32)DWC3_DCTL_LST_CHANGE_REQ_MASK;
}

static void configure_ep(volatile struct dwc3_regs *dwc3, u32 ep, u32 action, u32 max_packet_size) {
	u32 cfg0 = action;
	cfg0 |= max_packet_size << 3 | USB_EP_TYPE_CONTROL << 1;
	u32 cfg1 = DWC3_DEPCFG1_XFER_COMPLETE_EN | DWC3_DEPCFG1_XFER_NOT_READY_EN;
	cfg1 |= 0;	/*interrupt number*/
	cfg1 |= ep << 25 & 0x3e000000;	/*endpoint number*/
	if (ep & 1) {
		cfg0 |= ep << 16 & 0x3e0000;	/* FIFO number */
	}
	post_depcmd(dwc3, ep, DWC3_DEPCMD_SET_EP_CONFIG, cfg0, cfg1, 0);
	wait_depcmd(dwc3, ep);
	dwc3->device_endpoint_enable |= 1 << ep;
}

static void configure_all_eps(volatile struct dwc3_regs *dwc3, u32 max_packet_size) {
	post_depcmd(dwc3, 0, DWC3_DEPCMD_START_NEW_CONFIG, 0, 0, 0);
	wait_depcmd(dwc3, 0);
	printf("GSTS: %08"PRIx32" DSTS: %08"PRIx32"\n", dwc3->global_status, dwc3->device_status);
	for_range(ep, 0, 2) {
		configure_ep(dwc3, ep, DWC3_DEPCFG0_INIT, max_packet_size);
	}
	for_range(ep, 0, num_ep) {
		post_depcmd(dwc3, ep, DWC3_DEPCMD_SET_XFER_RSC_CONFIG, 1, 0, 0);
		wait_depcmd(dwc3, ep);
	}
}

struct dwc3_setup {
	volatile struct dwc3_regs *dwc3;
	volatile u32 *evtbuf;
	volatile void *trb;
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
};

const u8 _Alignas(64) device_desc[18] = {
	0x12, 0x01,	/* length, descriptor type (device) */
	0x00, 0x02,	/* USB version: 2.0 */
	0x00, 0x00, 0x00,	/* device class, subclass, protocol: defined by interface */
	0x40,	/* max packet size for EP0: 64 */
	0x07, 0x22, 0x0c, 0x33,	/* vendor (2270: Rockchip) and product (330c: RK3399) */
	0x00, 0x01,	/* device version: 1.0 */
	0x00, 0x00, 0x00,	/* manufacturer, product, serial number */
	01,	/* number of configurations */
};

const u8 _Alignas(64) conf_desc[32] = {
	9, 2,	/* 9-byte configuration descriptor */
		0x20, 0x00,	/* total length 32 bytes */
		1,	/* 1 interface */
		1,	/* configuration value 1 */
		0,	/* configuration 0 */
		0x80,	/* no special attributes */
		0xc8,	/* max. power 200 mA */
	9, 4,	/* 9-byte interface descriptor */
		0,	/* interface number 0 */
		0,	/* alternate setting */
		2,	/* 2 endpoints */
		0xff, 6, 5,	/* class: vendor specified */
		0,	/* interface 0 */
	7, 5,	/* 7-byte endpoint descriptor */
		0x81,	/* address 81 (device-to-host) */
		0x02,	/* attributes: bulk endpoint */
		0x00, 0x02, /* max. packet size: 512 */
		0,	/* interval value 0 */
	7, 5,	/* 7-byte endpoint descriptor */
		0x02,	/* address 2 (host-to-device) */
		0x02,	/* attributes: bulk endpoint */
		0x00, 0x02,	/* max. packet size: 512 */
		0,	/* interval value 0 */
};

enum {LAST_TRB = DWC3_TRB_ISP_IMI | DWC3_TRB_IOC | DWC3_TRB_LAST};

static void process_ep0_event(const struct dwc3_setup *setup, struct dwc3_state *st, u32 event) {
	volatile struct dwc3_regs *dwc3 = setup->dwc3;
	u32 UNUSED ep = event >> 1 & 0x1f, type = event >> 6 & 15, status = event >> 12 & 15;
	printf(" type%"PRIu32" status%"PRIu32, type, status);
	const enum dwc3_ep0phase phase = st->ep0phase;
	if (type == DWC3_DEPEVT_XFER_COMPLETE) {
		if (phase == DWC3_EP0_SETUP) {
			puts(" setup complete\n");
			u8 *req = (u8 *)setup->trb;
			dump_mem(req, 16);
			u16 req_header = (u16)req[0] << 8 | req[1];	/* bRequestType and bRequest */
			u16 wLength = req[6] | (u16)req[7] << 8;
			if ((req_header & 0xe0ff) == 0x8006) { /* GET_DESCRIPTOR */
				assert(wLength);
				const u8 *desc;
				if (req[3] == 1) {
					desc = device_desc;
					if (wLength > 18) {wLength = 18;}
				} else {
					assert(req[3] == 2);
					desc = conf_desc;
					if (wLength > 32) {wLength = 32;}
				}
				submit_trb(dwc3, 1, setup->trb, (u64)desc, wLength, DWC3_TRB_TYPE_CONTROL_DATA | LAST_TRB);
				st->ep0phase = DWC3_EP0_DATA;
			} else if ((req_header & 0xe0ff) == 0x0005) { /* SET_ADDRESS */
				assert(!wLength);
				u16 wValue = req[2] | (u16)req[3] << 8;
				assert(wValue < 0x80);
				u32 tmp = dwc3->device_config & ~(u32)DWC3_DCFG_DEVADDR_MASK;
				dwc3->device_config = tmp | wValue << 3;
				st->ep0phase = DWC3_EP0_STATUS2;
			} else if ((req_header & 0xe0ff) == 0x0009) { /* SET_CONFIGURATION */
				assert(!wLength);
				u16 wValue = req[2] | (u16)req[3] << 8;
				assert(wValue == 1);
				dwc3_write_dctl(dwc3, dwc3->device_control | DWC3_DCTL_ACCEPT_U1 | DWC3_DCTL_ACCEPT_U2);
				st->ep0phase = DWC3_EP0_STATUS2;
			} else {die("unexpected control request: %04"PRIx16"\n", req_header);}
		} else if (phase == DWC3_EP0_DATA) {
			puts(" data complete");
			st->ep0phase = DWC3_EP0_STATUS3;
		} else if (phase == DWC3_EP0_STATUS2 || phase == DWC3_EP0_STATUS3) {
			puts(" restarting EP0 cycle");
			submit_trb(dwc3, 0, setup->trb, (u64)setup->trb, 8, DWC3_TRB_TYPE_CONTROL_SETUP | LAST_TRB);
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
			submit_trb(dwc3, ep, setup->trb, (u64)setup->trb, 0, cmd | LAST_TRB);
		} else {die(" unexpected XferNotReady");}
	} else {die(" unexpected ep0 event\n");}
}

_Noreturn void ENTRY main() {
	puts("usbstage\n");
	struct stage_store store;
	stage_setup(&store);
	mmu_setup(initial_mappings, critical_ranges);
	assert(sizeof(event_buffer) >= 256 && sizeof(event_buffer) <= 0xfffc && sizeof(event_buffer) % 4 == 0);
	volatile struct dwc3_regs *dwc3 = (volatile struct dwc3_regs *)0xfe80c100;

	mmu_unmap_range(0xff8c0000, 0xff8c0fff);
	mmu_map_mmio_identity(0xff8c0000, 0xff8c0fff);
	dsb_st();
	volatile u32 *evtbuf = (u32 *)0xff8c0100;
	volatile u32 *trb = (u32 *)0xff8c0200;
	const struct dwc3_setup setup = {
		.dwc3 = dwc3,
		.evtbuf = evtbuf,
		.evt_slots = 64,
		.trb = trb,
	};
	for_range(i, 0, setup.evt_slots) {evtbuf[i] = 0;}
	for (volatile const void *ptr2 = evtbuf; ptr2 < (void*)(evtbuf + setup.evt_slots); ptr2 += 64) {
		__asm__ volatile("dc civac, %0" : : "r"(ptr2) : "memory");
	}

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
	dwc3->device_config &= ~(u32)DWC3_DCFG_DEVSPD_MASK & ~(u32)DWC3_DCFG_DEVADDR_MASK;
	printf("DCFG: %08"PRIx32"\n", dwc3->device_config);
	udelay(10000);
	printf("new config GSTS: %08"PRIx32" DSTS: %08"PRIx32"\n", dwc3->global_status, dwc3->device_status);
	u32 max_packet_size = 64;
	configure_all_eps(dwc3, max_packet_size);
	printf("DALEPENA: %"PRIx32"\n", dwc3->device_endpoint_enable);

	dwc3->event_buffer_address[0] = (u32)(u64)evtbuf;
	dwc3->event_buffer_address[1] = (u32)((u64)evtbuf >> 32);
	dwc3->event_buffer_size = 256;
	dwc3->device_event_enable = 0x1e1f;

	submit_trb(dwc3, 0, trb, (u64)setup.trb, 8, DWC3_TRB_TYPE_CONTROL_SETUP | LAST_TRB);
	wait_depcmd(dwc3, 0);
	printf("cmd %"PRIx32"\n", dwc3->device_ep_cmd[0].cmd);
	udelay(100);
	u32 tmp = dwc3->device_control & ~(u32)DWC3_DCTL_KEEP_CONNECT & ~(u32)DWC3_DCTL_LST_CHANGE_REQ_MASK;
	dwc3->device_control = tmp | DWC3_DCTL_RUN;
	printf("GSTS: %08"PRIx32" DSTS: %08"PRIx32"\n", dwc3->global_status, dwc3->device_status);

	{
		u32 evtcount = dwc3->event_count;
		printf("%"PRIu32" events pending, evtsiz0x%"PRIx32"\n", evtcount, dwc3->event_buffer_size);
	}
	/*assert(evtcount == 0);*/
	struct dwc3_state st;
	st.ep0phase = DWC3_EP0_DISCONNECTED;
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
		printf("GSTS: %08"PRIx32" DSTS: %08"PRIx32" evtbuf: %08"PRIx32"\n", dwc3->global_status, dwc3->device_status, dwc3->event_buffer_address[0]);
		dwc3->event_buffer_size = 256 | 1 << 31;
		for (volatile const void *ptr2 = evtbuf; ptr2 < (void*)(evtbuf + setup.evt_slots); ptr2 += 64) {
			__asm__ volatile("dc ivac, %0" : : "r"(ptr2) : "memory");
		}
		__asm__("dsb sy");
		//dump_mem(evtbuf, 256);
		for_range(i, 0, evtcount) {
			u32 event = evtbuf[evt_pos];
			evt_pos = (evt_pos + 1) % setup.evt_slots;
			printf("0x%08"PRIx32, event);
			if (event & 1) { /* generic event */
				assert((event >> 1 & 0x7f) == 0);
				switch (event >> 8 & 15) {
				case DWC3_DEVT_DISCONNECT:
					puts(" disconnect");
					dwc3_write_dctl(dwc3, dwc3->device_control & ~(u32)DWC3_DCTL_INIT_U1 & ~(u32)DWC3_DCTL_INIT_U2);
					st.ep0phase = DWC3_EP0_DISCONNECTED;
					break;
				case DWC3_DEVT_RESET:
					dwc3_write_dctl(dwc3, dwc3->device_control & ~(u32)DWC3_DCTL_TEST_CONTROL_MASK);
					for_range(ep, 1, num_ep) {	/* don't clear DEP0 */
						post_depcmd(dwc3, ep, DWC3_DEPCMD_CLEAR_STALL, 0, 0, 0);
					}
					dwc3->device_config &= ~(u32)DWC3_DCFG_DEVADDR_MASK;
					puts(" USB reset");
					st.ep0phase = DWC3_EP0_SETUP;
					break;
				case DWC3_DEVT_CONNECTION_DONE:
					printf(" connection done, status 0x%08"PRIx32, dwc3->device_status);
					assert((dwc3->device_status & 7) == 0);
					dwc3_write_dctl(dwc3, dwc3->device_control & ~(u32)DWC3_DCTL_HIRDTHRES_MASK & ~(u32)DWC3_DCTL_L1_HIBERNATION_EN);
					//post_depcmd(dwc3, 0, DWC3_DEPCMD_START_NEW_CONFIG, 0, 0, 0);
					//wait_depcmd(dwc3, 0);
					for_range(ep, 0, 2) {
						configure_ep(dwc3, ep, DWC3_DEPCFG0_MODIFY, max_packet_size);
					}
					for_range(ep, 0, num_ep) {
						post_depcmd(dwc3, ep, DWC3_DEPCMD_SET_XFER_RSC_CONFIG, 1, 0, 0);
						wait_depcmd(dwc3, ep);
					}
					break;
				case DWC3_DEVT_LINK_STATE_CHANGE:
					puts(" link status change");
					break;
				default: abort();
				}
			} else { /* endpoint event */
				u32 ep = event >> 1 & 0x1f;
				printf(" ep%"PRIu32, ep);
				if (ep <= 1) {
					process_ep0_event(&setup, &st, event);
				} else {die(" unknown endpoint\n");}
			}
			puts("\n");
			dwc3->event_count = 4;
		}
		//dwc3->event_count = (ptr - evtbuf) * 4;
		dwc3->event_buffer_size = 256;
		puts("\n");
	}
/*
	for_range(i, 0, 8) {
		printf("%2u:", i * 8);
		const u8 *start = (const u8 *)((u64)0xff8c01c0 + 8*i);
		for_range(j, 0, 8) {
			printf(" %02"PRIx8, start[j]);
		}
		puts("  ");
		for_range(j, 0, 8) {
			printf("%c", start[j] < 0x7f && start[j] >= 32 ? start[j] : '.');
		}
		puts("\n");
	}*/
}
