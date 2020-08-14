/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <runqueue.h>

struct sched_thread_start {
	_Alignas(16)
	struct sched_runnable runnable;
	u64 pc;
	u64 pad;
	u64 args[8];
};

_Noreturn NORETURN_ATTR void sched_start_thread(struct sched_runqueue *, struct sched_runnable *);
