/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>
#include <plat.h>

struct sdhci_regs;
struct sdhci_state;
struct sched_runnable_list;
enum iost sdhci_wait_state(struct sdhci_state *st, u32 mask, u32 expected, timestamp_t timeout, const char *name);
enum iost sdhci_submit_cmd(struct sdhci_state *st, u32 cmd, u32 arg);
