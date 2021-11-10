/* SPDX-License-Identifier: CC0-1.0 */
#include <rk3399/usbstage.h>
#include <inttypes.h>
#include <stdlib.h>

#include <byteorder.h>
#include <memaccess.h>
#include <runqueue.h>
#include <usb.h>

#include <arch/context.h>
#include <cache.h>
#include <mmu.h>

#include <gic.h>
#include <gic_regs.h>

#include <dwc3_regs.h>
#include <dwc3.h>
#include <xhci_regs.h>

#include <rk3399.h>
#include <stage.h>
#include <dump_mem.h>

static UNINITIALIZED _Alignas(4096) u8 vstack_frames[NUM_VSTACK][VSTACK_DEPTH];
void *const boot_stack_end = (void*)VSTACK_BASE(VSTACK_CPU0);

volatile struct uart *const console_uart = regmap_uart;

const struct mmu_multimap initial_mappings[] = {
#include <rk3399/base_mappings.inc.c>
	{.addr = 0, MMU_MAPPING(NORMAL, 0)},
	{.addr = 0xf8000000, .desc = 0},
	{.addr = 0xff8c0000, .desc =  MMU_MAPPING(UNCACHED, 0xff8c0000)},
	{.addr = 0xff8c1000, .desc = 0},
	VSTACK_MULTIMAP(CPU0),
	{}
};

enum {PHASE_HEADER, PHASE_DATA, PHASE_HANDOFF};

struct usbstage_state {
	struct dwc3_state st;
	_Atomic(u8) phase;
};
struct usbstage_bufs {
	struct dwc3_bufs bufs;
	_Alignas(16) u8 desc[32];
	struct xhci_trb ep4_trb;
	_Alignas(16) u8 header[512];
};

static struct usbstage_state st;

void plat_handler_fiq() {
	u64 grp0_intid;
	__asm__ volatile("mrs %0, "ICC_IAR0_EL1";msr DAIFClr, #0xf" : "=r"(grp0_intid));
	atomic_signal_fence(memory_order_acquire);
	if (grp0_intid) {
		dwc3_irq(&st.st);
	} else {
		printf("spurious interrupt\n");
	}
	atomic_signal_fence(memory_order_release);
	__asm__ volatile(
		"msr DAIFSet, #0xf;"
		"msr "ICC_EOIR0_EL1", %0;"
		"msr "ICC_DIR_EL1", %0"
	: : "r"(grp0_intid));
}
void plat_handler_irq() {
	die("unexpected IRQ");
}

static struct sched_runqueue runqueue = {.head = 0, .tail = &runqueue.head};
struct sched_runqueue *get_runqueue() {return &runqueue;}
static u64 _Alignas(4096) UNINITIALIZED pagetable_frames[11][512];
u64 (*const pagetables)[512] = pagetable_frames;
const size_t num_pagetables = ARRAY_SIZE(pagetable_frames);

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

const u8 _Alignas(64) device_qualifier[10] = {
	0x0a, USB_DEVICE_DESC,	/* length, descriptor type (device) */
	0x00, 0x02,	/* USB version: 2.0 */
	0x00, 0x00, 0x00,	/* device class, subclass, protocol: defined by interface */
	0x40,	/* max packet size for EP0: 64 */
	01,	/* number of configurations */
	0,	/* reserved byte */
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
		0x00, 0x04, /* max. packet size: 1024 */
		0,	/* interval value 0 */
	7, USB_EP_DESC,	/* 7-byte endpoint descriptor */
		0x02,	/* address 2 (host-to-device) */
		0x02,	/* attributes: bulk endpoint */
		0x00, 0x04,	/* max. packet size: 1024 */
		0,	/* interval value 0 */
};

static buf_id_t prepare_descriptor(struct dwc3_state *st, const struct usb_setup *req) {
	u8 *buf = ((struct usbstage_bufs *)st->bufs)->desc;
	const u8 descriptor_type = from_le16(req->wValue) >> 8;
	st->ep0_buf_addr = (u64)buf;
	u16 max_packet_size = usb_max_packet_size[st->speed];

	if (descriptor_type == USB_DEVICE_DESC) {
		for_array(i, device_desc) {buf[i] = device_desc[i];}
		if (max_packet_size < 64) {buf[7] = max_packet_size;}
		st->ep0_buf_size = device_desc[0];
		return 1;
	} else if (descriptor_type == USB_DEVICE_QUALIFIER) {
		for_array(i, device_qualifier) {buf[i] = device_qualifier[i];}
		st->ep0_buf_size = device_qualifier[0];
		return 1;
	} else if (descriptor_type == USB_CONFIG_DESC) {
		for_array(i, conf_desc) {buf[i] = conf_desc[i];}
		if (max_packet_size < 1024) {
			buf[22] = max_packet_size & 0xff;
			buf[23] = max_packet_size >> 8;
			buf[29] = max_packet_size & 0xff;
			buf[30] = max_packet_size >> 8;
		}
		st->ep0_buf_size = from_le16(*(u16 *)(conf_desc + 2));
		return 1;
	} else {return 0;}
}


enum {LAST_TRB = DWC3_TRB_ISP_IMI | DWC3_TRB_IOC | DWC3_TRB_LAST};

static _Bool set_configuration(struct dwc3_state *st, const struct usb_setup *req) {
	assert(from_le16(req->wValue) == 1);
	volatile struct dwc3_regs *dwc3 = st->regs;
	struct usbstage_state *ust = (struct usbstage_state *)st;
	release8(&ust->phase, PHASE_HEADER);
	struct usbstage_bufs *bufs = (struct usbstage_bufs *)st->bufs;

	u16 max_packet_size = usb_max_packet_size[st->speed];
	assert(max_packet_size <= sizeof(bufs->header));
	dwc3_configure_bulk_ep(dwc3, 4, DWC3_DEPCFG0_INIT, max_packet_size);
	dwc3_wait_depcmd(dwc3, 4);
	struct xhci_trb *ep4_trb = &bufs->ep4_trb;
	dwc3_submit_trb(dwc3, 4, ep4_trb, (u64)&bufs->header, 512, DWC3_TRB_TYPE_NORMAL | DWC3_TRB_CSP | LAST_TRB | DWC3_TRB_HWO);
	return 1;
}

static void release_buffer(struct dwc3_state UNUSED *st, buf_id_t UNUSED buf) {
	/* do nothing, buffers are statically allocated */
}

enum usbstage_command {
	CMD_LOAD = 0,
	CMD_CALL,
	CMD_START,
	CMD_FLASH,
	NUM_CMD
};

void handoff(u64, u64, u64, u64, u64, u64, u64, u64);
__asm__("handoff: add x8, sp, #0; add sp, x1, #0; stp x30, x8, [sp, #-16]!; blr x0;ldp x30, x8, [sp]; add sp, x8, #0");

void next_stage(u64, u64, u64, u64, u64, u64);

void usbstage_flash_spi(const u8 *buf, u64 start, u64 length);

static void xfer_complete(struct dwc3_state *st, u32 event) {
	assert(event == 0x0000c048);
	volatile struct dwc3_regs *dwc3 = st->regs;
	struct usbstage_bufs *bufs = (struct usbstage_bufs *)st->bufs;
	struct usbstage_state *st_ = (struct usbstage_state*)st;

	u64 *header = (u64 *)bufs->header;
	u8 phase = acquire8(&st_->phase);
	if (phase == PHASE_HEADER) {
		invalidate_range(header, sizeof(bufs->header));
	}

#if DEBUG_MSG
	dump_mem(bufs->header, 64);
#endif
	u64 cmd = header[0];
	if (phase == PHASE_HEADER) {
		switch (cmd) {
		case CMD_FLASH:
		case CMD_LOAD:;
			u64 size = header[1];
			assert((size & 0xff0001ff) == 0);
			u64 addr = cmd == CMD_LOAD ?  header[2] : 0x100000;
			dwc3_submit_trb(dwc3, 4, &bufs->ep4_trb, addr, size, DWC3_TRB_TYPE_NORMAL | DWC3_TRB_CSP | LAST_TRB | DWC3_TRB_HWO);
			release8(&st_->phase, PHASE_DATA);
			return;
		case CMD_START:;
			printf("halting");
			release8(&st_->phase, PHASE_HANDOFF);
			return;
		case CMD_CALL:;
			handoff(header[1], header[2], header[3], header[4], header[5], header[6], header[7], header[8]);
			break;
		default:assert(UNREACHABLE);
		}
	} else {
		switch (cmd) {
		case CMD_LOAD: break;
		case CMD_FLASH:
			usbstage_flash_spi((const u8 *)0x100000, header[2], header[1]);
			break;
		default: assert(UNREACHABLE);
		}
	}
	flush_range(header, sizeof(bufs->header));
	dwc3_submit_trb(dwc3, 4, &bufs->ep4_trb, (u64)&bufs->header, 512, DWC3_TRB_TYPE_NORMAL | DWC3_TRB_CSP | LAST_TRB | DWC3_TRB_HWO);
	release8(&st_->phase, PHASE_HEADER);
}

static const struct dwc3_gadget_ops usbstage_ops = {
	.prepare_descriptor = prepare_descriptor,
	.set_configuration = set_configuration,
	.release_buffer = release_buffer,
	.ep_event = xfer_complete,
};

static void reinit_dwc3(struct usbstage_state *st) {
	volatile struct dwc3_regs *const dwc3 = st->st.regs;
	info("soft-resetting the USB controller\n");
	dwc3->device_control = DWC3_DCTL_SOFT_RESET;
	u64 softreset_start = get_timestamp();
	while (dwc3->device_control & DWC3_DCTL_SOFT_RESET) {
		if (get_timestamp() - softreset_start > 100000 * CYCLES_PER_MICROSECOND) {
			die("USB softreset timeout\n");
		}
		udelay(1000);
	}
	dwc3->global_control |= DWC3_GCTL_CORE_SOFT_RESET;
	udelay(10);
	dwc3->phy_config |= DWC3_GUSB2PHYCFG_PHY_SOFT_RESET;
	dwc3->pipe_control |= DWC3_GUSB2PHYCFG_PHY_SOFT_RESET;
	udelay(1000);
	dwc3->phy_config &= ~(u32)DWC3_GUSB2PHYCFG_PHY_SOFT_RESET;
	dwc3->pipe_control &= ~(u32)DWC3_GUSB3PIPECTL_PHY_SOFT_RESET;
	udelay(1000);
	dwc3->global_status = 0x30;
	dwc3->global_control &= ~(u32)DWC3_GCTL_CORE_SOFT_RESET
		& ~(u32)DWC3_GCTL_DISABLE_SCRAMBLING
		& ~(u32)DWC3_GCTL_SCALEDOWN_MASK;
	debug("soft reset finished\n");
	dwc3->user_control1 |= DWC3_GUCTL1_USB3_USE_USB2_CLOCK;
	dwc3->pipe_control |= DWC3_GUSB3PIPECTL_SUSPEND_ENABLE;
	udelay(10000);
	u32 val = dwc3->phy_config;
	val &= ~(u32)DWC3_GUSB2PHYCFG_ENABLE_SLEEP
		& ~(u32)DWC3_GUSB2PHYCFG_SUSPEND_PHY
		& ~(u32)DWC3_GUSB2PHYCFG_USB2_FREE_CLOCK
		& ~(u32)DWC3_GUSB2PHYCFG_TURNAROUND_MASK;
	val |= DWC3_GUSB2PHYCFG_PHY_INTERFACE
		| DWC3_GUSB2PHYCFG_TURNAROUND(5);
	dwc3->phy_config = val;
	udelay(60000);
	printf("GSTS: %08"PRIx32" DSTS: %08"PRIx32"\n", dwc3->global_status, dwc3->device_status);

	dwc3->global_control |= DWC3_GCTL_PORT_CAP_DEVICE;
	/* HiSpeed */
	dwc3->device_config = st->st.dcfg;
	printf("DCFG: %08"PRIx32"\n", dwc3->device_config);
	udelay(10000);

	struct dwc3_bufs *bufs = st->st.bufs;
	dwc3->event_buffer_address[0] = (u32)(u64)&bufs->event_buffer;
	dwc3->event_buffer_address[1] = (u32)((u64)&bufs->event_buffer >> 32);
	atomic_thread_fence(memory_order_release);
	dwc3->event_buffer_size = 256;
	dwc3->device_event_enable = 0x1e1f;

	for_range(i, 0, st->st.evt_slots) {bufs->event_buffer[i] = 0;}
}

_Noreturn void main() {
	puts("usbstage");

	volatile struct dwc3_regs *const dwc3 = (struct dwc3_regs*)((char *)regmap_otg0 + 0xc100);
	struct usbstage_bufs *bufs = (struct usbstage_bufs *)0xff8c0210;
#ifdef DEBUG_MSG
	dump_mem(&bufs->bufs.event_buffer, sizeof(bufs->bufs.event_buffer));
#endif
	st = (struct usbstage_state){.st = {
		.regs = dwc3,
		.bufs = (struct dwc3_bufs *)bufs,
		.num_ep = 13,
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
		.ops = &usbstage_ops,
		.ep0phase = DWC3_EP0_DISCONNECTED,
	},
		.phase = PHASE_HEADER,
	};
	const u32 mdwidth = st.st.hwparams[0] >> 8 & 0xff;
	const u32 ram2_bytes = (st.st.hwparams[7] >> 16) * (mdwidth / 8);
	st.st.dcfg = DWC3_HIGH_SPEED | 0 << 12 | (ram2_bytes / 512) << 17 | DWC3_DCFG_LPM_CAPABLE;

	u32 brom_evt_pos = *(u32 *)0xff8c041c;
	st.st.evt_pos = brom_evt_pos / 4 % st.st.evt_slots;
	_Bool reinit = 0;
	if ((brom_evt_pos & 0xffffff03) || bufs->bufs.event_buffer[st.st.evt_pos] != 0x20c2) {
		reinit_dwc3(&st);
		st.st.evt_pos = 0;
		reinit = 1;
	} else {
		info("reusing BROM connection\n");
		u32 brom_skipped_event_bytes = *(u32 *)0xff8c0420;
		st.st.evt_pos = (st.st.evt_pos + brom_skipped_event_bytes / 4) % st.st.evt_slots;
		st.st.ep0phase = DWC3_EP0_STATUS3;
		st.st.speed = dwc3_speed_to_standard[dwc3->device_status & 7];
		for_range(ep, 1, st.st.num_ep) {	/* don't clear DEP0 */
			dwc3_post_depcmd(dwc3, ep, DWC3_DEPCMD_CLEAR_STALL, 0, 0, 0);
		}
		for_range(ep, 1, st.st.num_ep) {dwc3_wait_depcmd(dwc3, ep);}
	}

	printf("new config GSTS: %08"PRIx32" DSTS: %08"PRIx32"\n", dwc3->global_status, dwc3->device_status);
	u32 max_packet_size = 64;
	dwc3_new_configuration(dwc3, max_packet_size, st.st.num_ep);
	printf("DALEPENA: %"PRIx32"\n", dwc3->device_endpoint_enable);

	if (!reinit) {
		dwc3->device_endpoint_enable |= 1 << 4;
		struct usb_setup dummy = {
			.wIndex = 0, .bRequestType = 0, .bRequest = 0, .wLength = 0,
			.wValue = 1,
		};
		set_configuration(&st.st, &dummy);
		dwc3->event_buffer_size = 256;
	}


	dwc3_submit_trb(dwc3, 0, &st.st.bufs->ep0_trb, (u64)&st.st.bufs->setup_packet, 8, DWC3_TRB_TYPE_CONTROL_SETUP | LAST_TRB);
	dwc3_wait_depcmd(dwc3, 0);
	printf("cmd %"PRIx32"\n", dwc3->device_ep_cmd[0].cmd);
	udelay(100);
	if (reinit) {
		dwc3_start(dwc3);
		printf("GSTS: %08"PRIx32" DSTS: %08"PRIx32"\n", dwc3->global_status, dwc3->device_status);
	}

	{
		u32 evtcount = dwc3->event_count;
		printf("%"PRIu32" events pending, evtsiz0x%"PRIx32"\n", evtcount, dwc3->event_buffer_size);
	}
	gicv3_per_cpu_setup(regmap_gic500r);
	gicv2_setup_spi(regmap_gic500d, 137, 0x80, 1, IGROUP_0 | INTR_LEVEL);
	timestamp_t last_status = get_timestamp();
	while (acquire8(&st.phase) != PHASE_HANDOFF) {
		timestamp_t now = get_timestamp();
		if (now - last_status > 300000 * TICKS_PER_MICROSECOND) {
			last_status = now;
			printf("GSTS: %08"PRIx32" DSTS: %08"PRIx32"\n", dwc3->global_status, dwc3->device_status);
#ifdef DEBUG_MSG
			dump_mem(&bufs->bufs.event_buffer, sizeof(bufs->bufs.event_buffer));
#endif
		}
		__asm__("yield");
	}

	dwc3_halt(st.st.regs);
	gicv2_disable_spi(regmap_gic500d, 137);
	gicv2_wait_disabled(regmap_gic500d);
	gicv3_per_cpu_teardown(regmap_gic500r);
	info("[%"PRIuTS"] usbstage end\n", get_timestamp());
	u64 *header = (u64*)bufs->header;
	next_stage(header[3], header[4], header[5], header[6], header[1], header[2]);
	assert(UNREACHABLE);
}
