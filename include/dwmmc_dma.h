/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <dwmmc_regs.h>

struct dwmmc_dma_state {
	u32 desc_written, desc_completed;
	void *buf;
	size_t bytes_left, bytes_transferred;
	struct {_Alignas(64) struct dwmmc_idmac_desc desc;} desc[4];
};

void dwmmc_setup_dma(volatile struct dwmmc_regs *dwmmc);
void dwmmc_init_dma_state(struct dwmmc_dma_state *state);
void dwmmc_handle_dma_interrupt(volatile struct dwmmc_regs *dwmmc, struct dwmmc_dma_state *state);
void dwmmc_read_poll_dma(volatile struct dwmmc_regs *dwmmc, u32 sector, void *buf, size_t total_bytes);
