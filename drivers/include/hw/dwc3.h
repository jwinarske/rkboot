// SPDX-License-Identifier: CC0-1.0
#pragma once

// register offsets
#define DWC3_GEVNT 0x400
#define DWC3_DCFG 0x700
#define DWC3_DALEPENA 0x720
#define DWC3_DEPCMD 0x800

// register bits
#define DWC3_DEPCMD_SET_EP_CFG 1
#define DWC3_DEPCMD_SET_XFER_RSC_CFG 2
#define DWC3_DEPCMD_GET_ST 3
#define DWC3_DEPCMD_SET_STALL 4
#define DWC3_DEPCMD_CLEAR_STALL 5
#define DWC3_DEPCMD_START_XFER 6
#define DWC3_DEPCMD_UPD_XFER 7
#define DWC3_DEPCMD_END_XFER 8
#define DWC3_DEPCMD_IOC 0x100
#define DWC3_DEPCMD_ACT 0x400

#define DWC3_DEPCFG0(mps, fifonum, burstsize, seq_num)\
	(((mps) << 3 & 0x3ff8)\
	| ((fifonum) << 17 & 0x003e0000)\
	| ((burstsize) << 22 & 0x03c00000)\
	| ((seq_num) << 26 & 0x3c000000))
#define DWC3_DEPCFG0_CTRL_EP 0
#define DWC3_DEPCFG0_ISOC_EP 2
#define DWC3_DEPCFG0_BULK_EP 4
#define DWC3_DEPCFG0_INTR_EP 6
#define DWC3_DEPCFG0_INIT 0
#define DWC3_DEPCFG0_RESTORE 0x40000000
#define DWC3_DEPCFG0_MODIFY 0x80000000
#define DWC3_DEPCFG1(intnum, interval, epnum, in)\
	((intnum) & 0x1f\
	| ((interval) << 16 & 0x00ff0000)\
	| ((epnum) << 26 & 0x3c000000)\
	| ((in) << 25 & 0x02000000))
#define DWC3_DEPCFG1_INTEN_XFER_COMPLETE 0x100
#define DWC3_DEPCFG1_INTEN_XFER_PROGRESS 0x200
#define DWC3_DEPCFG1_INTEN_XFER_NRDY 0x400
#define DWC3_DEPCFG1_INTEN_FIFO_ERR 0x800
#define DWC3_DEPCFG1_INTEN_STREAM_EVT 0x2000
#define DWC3_DEPCFG1_STREAM_CAP 0x01000000
#define DWC3_DEPCFG1_BULK_BASED 0x40000000
#define DWC3_DEPCFG1_FIFO_BASED 0x80000000

// device-mode TRB bits
#define DWC3_DTRB_HWO 1
#define DWC3_DTRB_LAST 2
#define DWC3_DTRB_CHAIN 4
#define DWC3_DTRB_CSP 8
#define DWC3_DTRB_NORMAL 0x10
#define DWC3_DTRB_SETUP 0x20
#define DWC3_DTRB_STATUS2 0x30
#define DWC3_DTRB_STATUS3 0x40
#define DWC3_DTRB_CTL_DATA 0x50
#define DWC3_DTRB_ISOC_FIRST 0x60
#define DWC3_DTRB_ISOC 0x70
#define DWC3_DTRB_LINK 0x80
#define DWC3_DTRB_ISP 0x400
#define DWC3_DTRB_IOC 0x800

// Events format:
// [0] 0: EP event, 1: device event
// For EP events:
// [1] 0: OUT, 1: IN
// [2..6] EP number
// [6..10] Event code:
// 1: transfer complete
// 2: transfer progress
// 3: transfer not ready (not usable for Control-SETUP and Control-Data phases)
// 4: FIFO over-/underrun
// 6: stream
// 7: command completion
// [12..16] Status bits
