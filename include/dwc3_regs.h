/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

struct dwc3_regs {
	u32 bus_config[2];
	u32 tx_threshold;
	u32 rx_threshold;
	u32 global_control;
	u32 power_mgmt_status;
	u32 global_status;
	u32 user_control1;
	u32 identification;
	u32 gpio;
	u32 user_id;
	u32 user_control;
	u32 bus_error_address[2];
	u32 ss_port_bus_mapping;
	u32 padding0;
	u32 hardware_parameters[8];
	u32 debug[8];
	u32 hs_port_bus_mapping;
	u32 fs_port_bus_mapping;
	u32 padding1[30];
	u32 phy_config;
	u32 padding2[47];
	u32 pipe_control;
	u32 padding3[15];
	u32 tx_fifo_size[7];
	u32 padding4[25];
	u32 rx_fifo_size[3];
	u32 padding5[29];
	u32 event_buffer_address[2];
	u32 event_buffer_size;
	_Atomic(uint32_t) event_count;
	u32 padding6[124];
	u32 hardware_parameters8;
	u32 padding7[3];
	u32 device_tx_priority;
	u32 padding8;
	u32 host_tx_priority;
	u32 host_rx_priority;
	u32 host_debug_priority;
	u32 host_priority_ratio;
	u32 padding9[2];
	u32 frame_length_adjustment;
	u32 padding10[51];
	u32 device_config;
	u32 device_control;
	u32 device_event_enable;
	u32 device_status;
	u32 device_cmd_parameter;
	u32 device_cmd;
	u32 padding11[2];
	u32 device_endpoint_enable;
	u32 padding12[55];
	struct {
		u32 par2, par1, par0;
		u32 cmd;
	} device_ep_cmd[13];
};
CHECK_OFFSET(dwc3_regs, hardware_parameters, 0x40);
CHECK_OFFSET(dwc3_regs, event_buffer_address, 0x300);
CHECK_OFFSET(dwc3_regs, hardware_parameters8, 0x500);
CHECK_OFFSET(dwc3_regs, host_priority_ratio, 0x524);
CHECK_OFFSET(dwc3_regs, frame_length_adjustment, 0x530);
CHECK_OFFSET(dwc3_regs, device_config, 0x600);
CHECK_OFFSET(dwc3_regs, device_endpoint_enable, 0x620);
CHECK_OFFSET(dwc3_regs, device_ep_cmd, 0x700);

enum {
	DWC3_HIGH_SPEED = 0,
	DWC3_FULL_SPEED = 1,
	DWC3_LOW_SPEED = 2,
	DWC3_SUPERSPEED = 4,
	DWC3_SUPERSPEED_PLUS = 5,
};

enum {
	DWC3_DCFG_LPM_CAPABLE = 1 << 22,
	DWC3_DCFG_INTRNUM_MASK = 0xf000,
	DWC3_DCFG_DEVADDR_MASK = 0x3f8,
	DWC3_DCFG_DEVSPD_MASK = 7,
};

#define DWC3_DCTL_LST_CHANGE_REQ(v) ((u32)(v) << 5 & 0x1e)
enum {
	DWC3_DCTL_RUN = 1 << 31,
	DWC3_DCTL_SOFT_RESET = 1 << 30,
	/* … */
	DWC3_DCTL_HIRDTHRES_MASK = 31 << 24,
	/* … */
	DWC3_DCTL_KEEP_CONNECT = 1 << 19,
	DWC3_DCTL_L1_HIBERNATION_EN = 1 << 18,
	/* … */
	DWC3_DCTL_INIT_U2 = 1 << 12,
	DWC3_DCTL_ACCEPT_U2 = 1 << 11,
	DWC3_DCTL_INIT_U1 = 1 << 10,
	DWC3_DCTL_ACCEPT_U1 = 1 << 9,
	DWC3_DCTL_LST_CHANGE_REQ_MASK = DWC3_DCTL_LST_CHANGE_REQ(15),
	DWC3_DCTL_TEST_CONTROL_MASK = 0xe,
	/* … */
};

enum {
	/* … */
	DWC3_DSTS_HALTED = 1 << 22,
	/* … */
};

#define DWC3_GUSB2PHYCFG_TURNAROUND(v) ((u32)(v) << 10 & 0x3c00)
enum {
	DWC3_GUSB2PHYCFG_PHY_SOFT_RESET = 1 << 31,
	DWC3_GUSB2PHYCFG_USB2_FREE_CLOCK = 1 << 30,
	DWC3_GUSB2PHYCFG_TURNAROUND_MASK = DWC3_GUSB2PHYCFG_TURNAROUND(15),
	DWC3_GUSB2PHYCFG_ENABLE_SLEEP = 1 << 8,
	DWC3_GUSB2PHYCFG_SUSPEND_PHY = 1 << 6,
	DWC3_GUSB2PHYCFG_PHY_INTERFACE = 1 << 3,
	/* … */
};
enum {
	DWC3_GUSB3PIPECTL_PHY_SOFT_RESET = 1 << 31,
	DWC3_GUSB3PIPECTL_SUSPEND_ENABLE = 1 << 17,
	/* … */
};
enum {
	DWC3_GCTL_PORT_CAP_DEVICE = 1 << 13,
	DWC3_GCTL_PORT_CAP_HOST = 1 << 12,
	DWC3_GCTL_CORE_SOFT_RESET = 1 << 11,
	DWC3_GCTL_SCALEDOWN_MASK = 3 << 4,
	DWC3_GCTL_DISABLE_SCRAMBLING = 1 << 3,
	/* … */
};
enum {
	DWC3_GUCTL1_USB3_USE_USB2_CLOCK = 1 << 26,
	/* … */
};

enum {
	DWC3_DEVT_DISCONNECT = 0,
	DWC3_DEVT_RESET = 1,
	DWC3_DEVT_CONNECTION_DONE = 2,
	DWC3_DEVT_LINK_STATE_CHANGE = 3,
	DWC3_DEVT_WAKEUP = 4,
	DWC3_DEVT_HIBERNATION_REQ = 5,
	DWC3_DEVT_U3L2L1_SUSPEND = 6, DWC3_DEVT_EOPF = 6,
	DWC3_DEVT_SOF = 7,
	/* 8: reserved */
	DWC3_DEVT_ERRATIC_ERROR = 9,
	DWC3_DEVT_COMMAND_COMPLETE = 10,	/* non-maskable */
	DWC3_DEVT_EVENT_OVERFLOW = 11,	/* non-maskable */
	DWC3_DEVT_VENDOR_TEST = 12,
};

enum {
	DWC3_DEPEVT_XFER_COMPLETE = 1,
	DWC3_DEPEVT_XFER_IN_PROGRESS = 2,
	DWC3_DEPEVT_XFER_NOT_READY = 3,
	DWC3_DEPEVT_FIFO = 4,
	/* 5: reserved */
	DWC3_DEPEVT_STREAM = 6,
	DWC3_DEPEVT_CMD_COMPLETE = 7,
};

enum {
	DWC3_DEPCMD_CMDIOC = 0x100,
	/* … */
	DWC3_DEPCMD_START_NEW_CONFIG = 9,
	/* … */
	DWC3_DEPCMD_START_XFER = 6,
	DWC3_DEPCMD_CLEAR_STALL = 5,
	DWC3_DEPCMD_SET_STALL = 4,
	/* … */
	DWC3_DEPCMD_SET_XFER_RSC_CONFIG = 2,
	/* set EP configuration:
	 * [1:2] type: 0 = control, 1 = isochronous, 2 = bulk, 3 = interrupt (as in EP descriptor)
	 * [3:12] max. packet size
	 * [17:21] FIFO number
	 * [22:25] burst size
	 * [26:29?] data sequential number
	 * (since 194a)[30:31] action: 0 = init, 1 = restore, 2 = modify
	 * (before 194a)[31] ignore sequential number
	 * [32:36] interrupt number
	 * [40] XferComplete enable
	 * [41] XferInProgress enable
	 * [42] XferNotReady enable
	 * [43] FifoError enable
	 * [45] StreamEvent enable
	 * [48:55] bInterval–1
	 * [56] stream capable
	 * [57:61] EP number
	 * [62] bulk based
	 * [63] FIFO based
	 * [64:95] state (if action = restore, probably obtained by Get Endpoint State command)
	*/
	DWC3_DEPCMD_SET_EP_CONFIG = 1,
};
enum {
	DWC3_DEPCFG0_INIT = 0 << 30,
	DWC3_DEPCFG0_RESTORE = 1 << 30,
	DWC3_DEPCFG0_MODIFY = 2 << 30,
}; enum {
	DWC3_DEPCFG1_XFER_COMPLETE_EN = 1 << 8,
	DWC3_DEPCFG1_XFER_IN_PROGRESS_EN = 1 << 9,
	DWC3_DEPCFG1_XFER_NOT_READY_EN = 1 << 10,
};

enum {
	/* … */
	DWC3_TRB_IOC = 1 << 11,
	DWC3_TRB_ISP_IMI = 1 << 10,
	/* bits 4–9: type */
	DWC3_TRB_CSP = 8,
	DWC3_TRB_CHAIN = 4,
	DWC3_TRB_LAST = 2,
	DWC3_TRB_HWO = 1,
};

enum {
	DWC3_TRB_TYPE_NORMAL = 1 << 4,
	DWC3_TRB_TYPE_CONTROL_SETUP = 2 << 4,
	DWC3_TRB_TYPE_CONTROL_STATUS2 = 3 << 4,
	DWC3_TRB_TYPE_CONTROL_STATUS3 = 4 << 4,
	DWC3_TRB_TYPE_CONTROL_DATA = 5 << 4,
	DWC3_TRB_TYPE_ISOC_FIRST = 6 << 4,
	DWC3_TRB_TYPE_ISOCHRONOUS = 7 << 4,
	DWC3_TRB_TYPE_LINK = 8 << 4,
};
