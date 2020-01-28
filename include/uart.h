#pragma once

#include <defs.h>

struct uart {
	union {
		u32 tx; /* write-only, available if LCR & UART_LCR_DIVISOR_ACCESS == 0 */
		u32 rx; /* read-only, available if LCR & UART_LCR_DIVISOR_ACCESS == 0 */
		u32 divisor_low; /* available if LCR & UART_LCR_DIVISOR_ACCESS == UART_LCR_DIVISOR_ACCESS */
	};
	union {
		u32 divisor_high; /* available if LCR & UART_LCR_DIVISOR_ACCESS == UART_LCR_DIVISOR_ACCESS */
		u32 interrupt_enable; /* available if LCR & UART_LCR_DIVISOR_ACCESS == 0 */
	};
	union {
		u32 interrupt_identification;
		u32 fifo_control;
	};
	u32 line_control;
	u32 modem_control;
	u32 line_status;
	u32 modem_status;
	u32 scratchpad;
	u32 padding1[4];
	u32 shadow_rx;
	u32 padding2[14];
	u32 shadow_tx;
	u32 fifo_access;
	u32 tx_fifo_read;
	u32 rx_fifo_write;
	u32 uart_status;
	u32 tx_level;
	u32 rx_level;
	u32 software_reset;
	u32 shadow_rts;
	u32 shadow_break_control;
	u32 shadow_dma_mode;
	u32 shadow_fifo_enable;
	u32 shadow_rcvr_trigger;
	u32 shadow_tx_empty_trigger;
	u32 halt_tx;
	u32 dma_software_ack;
	u32 padding3[18];
	u32 component_param;
	u32 component_version;
	u32 component_type;
};
CHECK_OFFSET(uart, line_control, 0xc);
CHECK_OFFSET(uart, shadow_rx, 0x30);
CHECK_OFFSET(uart, shadow_tx, 0x6c);
CHECK_OFFSET(uart, tx_level, 0x80);
_Static_assert(sizeof(struct uart) == 0x100, "wrong size for UART register struct");

enum {UART_FIFO_DEPTH = 64};
enum {
	UART_LCR_DIVISOR_ACCESS = 0x80,
	UART_LCR_BREAK_CONTROL = 0x40,
	UART_LCR_EVEN_PARITY = 0x10,
	UART_LCR_PARITY_EN = 0x08,
	UART_LCR_1_5_STOP_BITS = 0x04,
	UART_LCR_5_DATA_BITS = 0x00,
	UART_LCR_6_DATA_BITS = 0x01,
	UART_LCR_7_DATA_BITS = 0x02,
	UART_LCR_8_DATA_BITS = 0x03
};

static volatile struct uart *const uart = (volatile struct uart *)0xff1a0000;
