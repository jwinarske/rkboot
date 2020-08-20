/* SPDX-License-Identifier: CC0-1.0 */
#include <stdatomic.h>
#include <irq.h>
#include <runqueue.h>
#include <log.h>
#include <plat/sched.h>
#include <timer.h>

_Static_assert(ATOMIC_POINTER_LOCK_FREE == 2, "atomic pointers are not lock-free");
_Static_assert(sizeof(void *) == sizeof(_Atomic(void*)), "atomic pointers have different size");

void sched_queue_many(struct sched_runnable_list *list, struct sched_runnable *first, struct sched_runnable *last) {
	if (list == CURRENT_RUNQUEUE) {
		irq_save_t irq = irq_save_mask();
		list = &get_runqueue()->fresh;
		irq_restore(irq);
	}
	struct sched_runnable *next = atomic_load_explicit(&list->head, memory_order_acquire);
	do {
		last->next = next;
	} while(!atomic_compare_exchange_weak_explicit(&list->head, &next, first, memory_order_release, memory_order_acquire));
}

void sched_queue_single(struct sched_runnable_list *list, struct sched_runnable *runnable) {
	sched_queue_many(list, runnable, runnable);
}

void sched_queue_list(struct sched_runnable_list *dest, struct sched_runnable_list *src) {
	/* we use memory_order_acq_rel here (instead of just memory_order_acquire, which is all that is required to guarantee the list's integrity) to allow for sound locks/condvars using runnable_lists: in case that the waiting thread reads the null written in this atomic_exchange by the notifying thread, the waiting thread will have synchronized-with the notifying thread, guaranteeing the visibility of the lock/condvar value that was written. */
	struct sched_runnable *head = atomic_exchange_explicit(&src->head, 0, memory_order_acq_rel);
	if (!head) {return;}
	struct sched_runnable *last = head;
	while (last->next) {last = last->next;}
	sched_queue_many(dest, head, last);
}

/* this function is entered in an atomic context */
static void cpuidle() {
#ifdef __aarch64__
	debugs("idling\n");
	/* according to the ARMv8-A ARM, WFI works for physical interrupts regardless of DAIF state (which we need to prevent deadlocking races) */
	__asm__("wfi");
#else
	/* do some dummy IO */
	puts("");
#endif
}

_Noreturn void sched_next() {
	struct sched_runnable *runnable;
	do {
		{irq_mask();
			struct sched_runqueue *rq = get_runqueue();
			struct sched_runnable *fresh = atomic_exchange_explicit(&rq->fresh.head, 0, memory_order_acquire);
			if (fresh) {	/* add fresh entries in reverse order */
				struct sched_runnable **old_tail = rq->tail;
				if (!rq->tail) {old_tail = &rq->head;}
				rq->tail = &fresh->next;
				struct sched_runnable *next = 0;
				do {
					struct sched_runnable *tmp = fresh->next;
					fresh->next = next;
					next = fresh;
					fresh = tmp;
				} while (fresh);
				*old_tail = next;
			}
			runnable = rq->head;
			if (runnable) {
				if (!(rq->head = runnable->next)) {
					rq->tail = &rq->head;
				}
			} else {
				cpuidle();
			}
		irq_unmask();}
	} while (!runnable);
	runnable->run(runnable);
}

static _Noreturn NORETURN_ATTR void yield_finish(struct sched_runnable *continuation) {
	sched_queue_single(CURRENT_RUNQUEUE, continuation);
	sched_next();
}

void sched_yield() {
	call_cc(yield_finish);
}

_Noreturn NORETURN_ATTR void sched_finish_u32(struct sched_runnable *continuation, volatile void *reg, volatile void *list_, ureg_t mask, ureg_t expected) {
	struct sched_runnable_list *list = (struct sched_runnable_list *)list_;
	sched_queue_single(list, continuation);
	/* in the critical case where the notifier just dequeued before we enqueued, we are already synchronized by the dequeue-enqueue, so relaxed is OK */
	u32 val = atomic_load_explicit((volatile _Atomic(u32) *)reg, memory_order_relaxed);
	if ((val & mask) != expected) {
		sched_queue_list(CURRENT_RUNQUEUE, list);
	}
	sched_next();
}

static _Noreturn NORETURN_ATTR void lock_finish(struct sched_runnable *continuation, atomic_flag *flag, struct sched_runnable_list *list) {
	sched_queue_single(list, continuation);
	if (!atomic_flag_test_and_set_explicit(flag, memory_order_acquire)) {
		sched_queue_list(CURRENT_RUNQUEUE, list);
	}
	sched_next();
}

void call_cc_flag_runnablelist(void NORETURN_ATTR (*callback)(struct sched_runnable *, atomic_flag *, struct sched_runnable_list *), atomic_flag *, struct sched_runnable_list *);

void sched_lock(struct sched_runnable_list *list, atomic_flag *flag) {
	while (atomic_flag_test_and_set_explicit(flag, memory_order_acquire)) {
		call_cc_flag_runnablelist(lock_finish, flag, list);
	}
}

/* TODO: make a proper interrupt-driven timer infrastructure */
void usleep(u32 usecs) {
	timestamp_t start = get_timestamp();
	while (get_timestamp() - start < (timestamp_t)usecs*TICKS_PER_MICROSECOND) {
		sched_yield();
	}
}
