// SPDX-License-Identifier: CC0-1.0
#pragma once

#define ENUM_LOADER_MAPPINGS(X)\
	X(uart2, 0xff1a0000, MAP_DEV)\
	X(emmc, 0xfe330000, MAP_DEV)\
	X(sdmmc, 0xfe320000, MAP_DEV)\
	X(spi1, 0xff1d0000, MAP_DEV)\
	X(dmac1, 0xff6f0000, MAP_DEV)\
	X(loader_uncached, 0xff8eb000, MAP_UNCACHED)\
	X(stack, 0xff8e9000, MAP_RW)\
	X(brom_stack, 0xff8c1000, MAP_RO)	/* unmapped later */\
	X(brom_data, 0xff8c0000, MAP_RO)	/* unmapped later */\
	X(loader_code, 0xff8ea000, MAP_RW)	/* remapped to RX later */\
	X(otg0_extra, 0xfe80c000, MAP_DEV)\

#define LOADER_VA_PGTAB 0xff9ff000
#define LOADER_VA_UART2 0xff9fe000
#define LOADER_VA_EMMC 0xff9fd000
#define LOADER_VA_SDMMC 0xff9fc000
#define LOADER_VA_SPI1 0xff9fb000
#define LOADER_VA_DMAC1 0xff9fa000
#define LOADER_VA_LOADER_UNCACHED 0xff9f9000
#define LOADER_VA_BROM_DATA 0xff9f6000
#define LOADER_VA_OTG0 0xff9f4000

#define LOADER_DWC3_GEVNT 0x400

#ifndef __ASSEMBLER__
#include <stdint.h>

struct loader_data {
	/// boot medium indication as provided by BROM
	uint8_t boot_medium, padding[3];
#define LOADER_MEDIUM_EMMC 2
#define LOADER_MEDIUM_SPINOR 3
#define LOADER_MEDIUM_SD 5
#define LOADER_MEDIUM_USB 10
	union {
		struct {
			u8 evtbuf_pos;
		};
	};
};
#endif
