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
	volatile u32 *evtbuf = (u32 *)0xff8c0210;
	const u32 evt_slots = 64;
	volatile u32 *trb = (u32 *)0xff8c03f0;
	for_range(i, 0, evt_slots) {evtbuf[i] = 0;}
	for (volatile const void *ptr2 = evtbuf; ptr2 < (void*)(evtbuf + evt_slots); ptr2 += 64) {
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

	submit_trb(dwc3, 0, trb, 0xff8c03f0, 8, DWC3_TRB_TYPE_CONTROL_SETUP | DWC3_TRB_ISP_IMI | DWC3_TRB_IOC | DWC3_TRB_LAST);
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
	u32 ep0status = 2;
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
		for (volatile const void *ptr2 = evtbuf; ptr2 < (void*)(evtbuf + evt_slots); ptr2 += 64) {
			__asm__ volatile("dc ivac, %0" : : "r"(ptr2) : "memory");
		}
		__asm__("dsb sy");
		dump_mem(evtbuf, 256);
		for_range(i, 0, evtcount) {
			u32 event = evtbuf[evt_pos];
			evt_pos = (evt_pos + 1) % evt_slots;
			printf("0x%08"PRIx32, event);
			if (event & 1) { /* generic event */
				assert((event >> 1 & 0x7f) == 0);
				switch (event >> 8 & 15) {
				case DWC3_DEVT_DISCONNECT:
					puts(" disconnect");
					dwc3_write_dctl(dwc3, dwc3->device_control & ~(u32)DWC3_DCTL_INIT_U1 & ~(u32)DWC3_DCTL_INIT_U2);
					break;
				case DWC3_DEVT_RESET:
					dwc3_write_dctl(dwc3, dwc3->device_control & ~(u32)DWC3_DCTL_TEST_CONTROL_MASK);
					for_range(ep, 1, num_ep) {	/* don't clear DEP0 */
						post_depcmd(dwc3, ep, DWC3_DEPCMD_CLEAR_STALL, 0, 0, 0);
					}
					dwc3->device_config &= ~(u32)DWC3_DCFG_DEVADDR_MASK;
					puts(" USB reset");
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
				u32 ep = event >> 1 & 0x1f, type = event >> 6 & 15, status = event >> 12 & 15;
				if (ep <= 1) {
					printf(" ep%"PRIu32" type%"PRIu32" status%"PRIu32, ep, type, status);
					if (type == DWC3_DEPEVT_XFER_NOT_READY) {
						ep0status = status;
						if (status == 2) {
							submit_trb(dwc3, ep, trb, 0xff8c03f0, 0, DWC3_TRB_TYPE_CONTROL_STATUS3 | DWC3_TRB_ISP_IMI | DWC3_TRB_IOC | DWC3_TRB_LAST);
							puts(" sending status");
						} else if (status == 1) {
							puts(" setup started");
						}
					} else if (type == DWC3_DEPEVT_XFER_COMPLETE) {
						if (ep0status == 2) {
							puts(" setup complete\n");
							dump_mem(trb, 16);
						}
					}
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
