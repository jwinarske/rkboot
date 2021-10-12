/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

struct sched_runnable {
	struct sched_runnable *next;
};

/* the entries in a runnable_list are usually stored in reverse, because adding at the beginning is simple to do atomically */
struct sched_runnable_list {
	_Atomic(struct sched_runnable *) head;
};

static struct sched_runnable_list UNUSED *const CURRENT_RUNQUEUE = 0;

struct sched_runqueue {
	struct sched_runnable_list fresh;
	struct sched_runnable *head;
	struct sched_runnable **tail;
};

void sched_queue_single(struct sched_runnable_list *, struct sched_runnable *);
/* keep in mind the reversed nature of runnable_lists */
void sched_queue_many(struct sched_runnable_list *, struct sched_runnable *first, struct sched_runnable *last);
/** atomically takes from src (which must not be CURRENT_RUNQUEUE) and then atomically adds the contents to dest (which may be CURRENT_RUNQUEUE) */
void sched_queue_list(struct sched_runnable_list *dest, struct sched_runnable_list *src);

/// Tries to pop a thread off the runqueue.
/// Returns null if none found.
/// Caller must own the non-atomic parts of the runqueue
struct sched_runnable *sched_unqueue(struct sched_runqueue *rq);

/// takes the current thread off-CPU, while keeping it runnable
void sched_yield();

void sched_finish_u32(struct sched_runnable *continuation, volatile void *reg, volatile void *list_, ureg_t clear, ureg_t set);
void sched_finish_u8(struct sched_runnable *continuation, volatile void *reg, volatile void *list_, ureg_t clear, ureg_t set);
void sched_finish_u8ptr(struct sched_runnable *continuation, volatile void *reg, volatile void *list_, ureg_t val);

void usleep(u32 usecs);

// === architecture-level functions
void call_cc(void (*callback)(struct sched_runnable *));
void call_cc_ptr2_int2(void (*callback)(struct sched_runnable *, volatile void *, volatile void *, ureg_t, ureg_t), volatile void *, volatile void *, ureg_t, ureg_t);
void call_cc_ptr2_int1(void (*callback)(struct sched_runnable *, volatile void *, volatile void *, ureg_t), volatile void *, volatile void *, ureg_t);

/// Continues the given thread. Must be run on the CPU stack.
/// Returns whenever the thread goes off-CPU.
void arch_sched_run(struct sched_runnable *);
