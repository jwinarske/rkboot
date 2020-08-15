/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

struct sched_runqueue;
struct sched_runnable {
	struct sched_runnable *next;
	NORETURN_ATTR void (*run)(struct sched_runnable*);
};

struct sched_runnable_list {
	_Atomic(struct sched_runnable *) head;
};

static struct sched_runnable_list UNUSED *const CURRENT_RUNQUEUE = 0;

struct sched_runqueue {
	struct sched_runnable_list fresh;
	struct sched_runnable *head;
	struct sched_runnable **tail;
};

void sched_queue(struct sched_runnable_list *, struct sched_runnable *);
_Noreturn void sched_next();
void sched_yield(struct sched_runnable_list *);
