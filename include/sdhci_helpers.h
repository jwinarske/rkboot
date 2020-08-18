/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>
#include <plat.h>

struct sdhci_regs;
struct sdhci_state;
struct sched_runnable_list;
_Bool sdhci_wait_state_clear(volatile struct sdhci_regs *sdhci, struct sdhci_state *st, u32 mask, timestamp_t timeout, const char *name);
_Bool sdhci_submit_cmd(volatile struct sdhci_regs *sdhci, struct sdhci_state *st, u32 cmd, u32 arg);
