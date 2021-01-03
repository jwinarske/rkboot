/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>
#include <async.h>
#include <runqueue.h>

struct rkspi_xfer_state {
	u16 this_xfer_items;
	_Atomic(void *) buf;
	void *end;
	struct sched_runnable_list waiters;
};

struct rkspi_regs;
void rkspi_recv_fast(volatile struct rkspi_regs *spi, u8 *buf, u32 buf_size);
void rkspi_read_flash_poll(volatile struct rkspi_regs *spi, u8 *buf, size_t buf_size, u32 addr);
void rkspi_handle_interrupt(struct rkspi_xfer_state *state, volatile struct rkspi_regs *spi);
void rkspi_start_rx_xfer(struct rkspi_xfer_state *state, volatile struct rkspi_regs *spi, size_t bytes);
void rkspi_tx_cmd4_dummy1(volatile struct rkspi_regs *spi, u32 cmd);
void rkspi_tx_fast_read_cmd(volatile struct rkspi_regs *spi, u32 addr);
