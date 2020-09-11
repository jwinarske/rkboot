/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

struct sdhci_regs {
	union {
		u32 system_addr;
		u32 arg2;
	};
	u16 block_size;
	u16 block_count;
	u32 arg;
	u16 transfer_mode;
	u16 cmd;
	u32 resp[4];
	u32 fifo;
	u32 present_state;
	u8 host_control1;
	u8 power_control;
	u8 block_gap_control;
	u8 padding0;
	u16 clock_control;
	u8 timeout;
	u8 swreset;
	union {
		struct {
			u16 normal_int_st;
			u16 error_int_st;
		};
		u32 int_st;
	};
	union {
		struct {
			u16 normal_int_st_enable;
			u16 error_int_st_enable;
		};
		u32 int_st_enable;
	};
	union {
		struct {
			u16 normal_int_signal_enable;
			u16 error_int_signal_enable;
		};
		u32 int_signal_enable;
	};
	u16 auto_cmd_error_st;
	u16 host_control2;
	u32 capabilities[2];
	u64 padding1;
	u16 force_event_auto_cmd;
	u16 force_event;
	u16 adma_error_st;
	u64 adma_addr;
	u16 preset_value[7];
	u32 boot_timeout;
	u16 preset_value_hs400;
	u16 padding2;
	u16 vendor;
	u8 padding3[0x82];
	u16 slot_int_st;
	union {
		struct {
			u8 sdhci_version;
			u8 vendor_version;
		};
		u16 version;
	};
};
CHECK_OFFSET(sdhci_regs, swreset, 0x2f);
CHECK_OFFSET(sdhci_regs, capabilities, 0x40);
CHECK_OFFSET(sdhci_regs, force_event_auto_cmd, 0x50);
CHECK_OFFSET(sdhci_regs, preset_value, 0x60);
CHECK_OFFSET(sdhci_regs, boot_timeout, 0x70);
CHECK_OFFSET(sdhci_regs, version, 0xfe);

enum {
	SDHCI_TRANSMOD_MULTIBLOCK = 32,
	SDHCI_TRANSMOD_READ = 16,
	SDHCI_TRANSMOD_WRITE = 8,
	SDHCI_TRANSMOD_NO_AUTO_CMD = 0,
	SDHCI_TRANSMOD_AUTO_CMD12 = 1 << 2,
	SDHCI_TRANSMOD_AUTO_CMD23 = 2 << 2,
	SDHCI_TRANSMOD_BLOCK_COUNT = 2,
	SDHCI_TRANSMOD_DMA = 1,
};

enum {
	SDHCI_CMD_NORESP = 0,
	SDHCI_CMD_RESP136 = 1,
	SDHCI_CMD_RESP48 = 2,
	SDHCI_CMD_RESP48BUSY = 3,
	/* bit 2 reserved */
	SDHCI_CMD_CRC = 8,
	SDHCI_CMD_RESPIDX = 16,
	SDHCI_CMD_DATA = 32,
	SDHCI_CMD_NORMAL = 0,
	SDHCI_CMD_SUSPEND = 1 << 6,
	SDHCI_CMD_RESUME = 2 << 6,
	SDHCI_CMD_ABORT = 3 << 6,

	SDHCI_R1 = SDHCI_CMD_RESP48 | SDHCI_CMD_CRC | SDHCI_CMD_RESPIDX,
	SDHCI_R1b = SDHCI_CMD_RESP48BUSY | SDHCI_CMD_CRC | SDHCI_CMD_RESPIDX,
	SDHCI_R2 = SDHCI_CMD_RESP136 | SDHCI_CMD_CRC,
	SDHCI_R3 = SDHCI_CMD_RESP48,
};
#define SDHCI_CMD(i) ((u16)(i) << 8)

enum {
	SDHCI_PRESTS_READ_READY = 1 << 11,
	SDHCI_PRESTS_WRITE_READY = 1 << 10,
	SDHCI_PRESTS_READ_ACTIVE = 1 << 9,
	SDHCI_PRESTS_WRITE_ACTIVE = 1 << 8,
	/* bits 4:7 reserved */
	SDHCI_PRESTS_RETUNE = 4,
	SDHCI_PRESTS_DAT_INHIBIT = 2,
	SDHCI_PRESTS_CMD_INHIBIT = 1
};

enum {
	SDHCI_HOSTCTRL1_CARD_DET_NORMAL = 0,
	SDHCI_HOSTCTRL1_CARD_DET_FORCE_REMOVED = 1 << 7,
	SDHCI_HOSTCTRL1_CARD_DET_FORCE_INSERTED = 1 << 7 | 1 << 6,
	SDHCI_HOSTCTRL1_BUS_WIDTH_8 = 32,
	SDHCI_HOSTCTRL1_SDMA = 0,
	SDHCI_HOSTCTRL1_ADMA1 = 1 << 3,
	SDHCI_HOSTCTRL1_ADMA2_32 = 2 << 3,
	SDHCI_HOSTCTRL1_ADMA2_64 = 3 << 3,
	SDHCI_HOSTCTRL1_DMA_MASK = 3 << 3,
	SDHCI_HOSTCTRL1_HIGH_SPEED_MODE = 4,
	SDHCI_HOSTCTRL1_BUS_WIDTH_4 = 2,
	/* bit 0 reserved */
};

enum {
	SDHCI_PWRCTRL_ON = 1,
	SDHCI_PWRCTRL_1V8 = 0xa,
	SDHCI_PWRCTRL_3V0 = 0xc,
	SDHCI_PWRCTRL_3V3 = 0xe,
};

enum {
	SDHCI_BLKGAPCTRL_STOP_REQ = 1,
};

enum {
	SDHCI_CLKCTRL_SDCLK_EN = 4,
	SDHCI_CLKCTRL_INTCLK_STABLE = 2,
	SDHCI_CLKCTRL_INTCLK_EN = 1
};
#define SDHCI_CLKCTRL_DIV(div) ((u16)(div) >> 1 << 8 | ((u16)(div) >> 3 & 0xc0))

enum {
	SDHCI_SWRST_DAT = 4,
	SDHCI_SWRST_CMD = 2,
	SDHCI_SWRST_ALL = 1,
};

#define DEFINE_SDHCI_NORINT\
	X(CMD_COMPLETE) X(XFER_COMPLETE) X(BLOCK_GAP) X(DMA)\
	X(BUFFER_WRITE_READY) X(BUFFER_READ_READY) X(CARD_INSERTED) X(CARD_REMOVED)\
	X(CARD_INT) X(RESERVED9) X(RESERVED10) X(RESERVED11)\
	X(RETUNE) X(BOOT_ACK) X(BOOT_TERM) X(ERROR)
#define DEFINE_SDHCI_ERRINT\
	X(CMD_TIMEOUT) X(CMD_CRC) X(CMD_END_BIT) X(CMD_INDEX)\
	X(DATA_TIMEOUT) X(DATA_CRC) X(DATA_END_BIT) X(CURRENT_LIMIT)\
	X(AUTO_CMD) X(ADMA) X(RESERVED26) X(RESERVED27)\
	X(TARGET_RESP) X(RESERVED29) X(RESERVED30) X(RESERVED31)

enum {
#define X(name) SDHCI_INT_##name##_BIT,
	DEFINE_SDHCI_NORINT
	DEFINE_SDHCI_ERRINT
#undef X
	NUM_SDHCI_INT
};
_Static_assert(NUM_SDHCI_INT == 32, "miscounted");
enum {
#define X(name) SDHCI_INT_##name = (u32)1 << SDHCI_INT_##name##_BIT,
	DEFINE_SDHCI_NORINT
	DEFINE_SDHCI_ERRINT
#undef X
	SDHCI_INTMASK_ALL = 0x13fff1ff
};

enum {
#define X(name) SDHCI_ERRINT_##name##_BIT,
	DEFINE_SDHCI_ERRINT
#undef X
	NUM_SDHCI_ERRINT
};
_Static_assert(NUM_SDHCI_ERRINT == 16, "miscounted");
enum {
#define X(name) SDHCI_ERRINT_##name = (u32)1 << SDHCI_ERRINT_##name##_BIT,
	DEFINE_SDHCI_ERRINT
#undef X
};

enum {
	SDHCI_HOSTCTRL2_CLOCK_TUNED = 1 << 7,
	SDHCI_HOSTCTRL2_EXECUTE_TUNING = 64,
	SDHCI_HOSTCTRL2_UHS_MASK = 7,
	SDHCI_HOSTCTRL2_SDR12 = 0,
	SDHCI_HOSTCTRL2_SDR25 = 1,
	SDHCI_HOSTCTRL2_SDR50 = 2,
	SDHCI_HOSTCTRL2_SDR104 = 3,
	SDHCI_HOSTCTRL2_DDR50 = 4,
	SDHCI_HOSTCTRL2_HS400 = 5,
};

struct sdhci_cq_regs {
	u32 version;
	u32 capabilities;
	u32 configuration;
	/* … */
};
