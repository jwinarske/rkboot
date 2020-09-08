/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

struct sched_runqueue;
struct sched_runnable {
	struct sched_runnable *next;
	NORETURN_ATTR void (*run)(struct sched_runnable*);
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

/** abandons the current stack by taking the next entry from the current runqueue and running it */
_Noreturn void sched_next();

/** saves the current execution state and queues it as a runnable, then calls sched_next */
void sched_yield();

void call_cc(void NORETURN_ATTR (*callback)(struct sched_runnable *));
void call_cc_ptr2_int2(void NORETURN_ATTR (*callback)(struct sched_runnable *, volatile void *, volatile void *, ureg_t, ureg_t), volatile void *, volatile void *, ureg_t, ureg_t);
void call_cc_ptr2_int1(void NORETURN_ATTR (*callback)(struct sched_runnable *, volatile void *, volatile void *, ureg_t), volatile void *, volatile void *, ureg_t);

_Noreturn NORETURN_ATTR void sched_finish_u32(struct sched_runnable *continuation, volatile void *reg, volatile void *list_, ureg_t clear, ureg_t set);
_Noreturn NORETURN_ATTR void sched_finish_u8(struct sched_runnable *continuation, volatile void *reg, volatile void *list_, ureg_t clear, ureg_t set);
_Noreturn NORETURN_ATTR void sched_finish_u8ptr(struct sched_runnable *continuation, volatile void *reg, volatile void *list_, ureg_t val);

void usleep(u32 usecs);
