/* SPDX-License-Identifier: CC0-1.0 */
#include <stdatomic.h>
#include <irq.h>
#include <runqueue.h>
#include <log.h>
#include <plat/sched.h>

#ifndef ATOMIC_POINTER_LOCK_FREE
#error atomic pointers are not lock-free
#endif
_Static_assert(sizeof(void *) == sizeof(_Atomic(void*)), "atomic pointers have different size");

void sched_queue(struct sched_runnable_list *list, struct sched_runnable *runnable) {
	if (list == CURRENT_RUNQUEUE) {
		irq_save_t irq = irq_save_mask();
		list = &get_runqueue()->fresh;
		irq_restore(irq);
	}
	struct sched_runnable *next = atomic_load_explicit(&list->head, memory_order_acquire);
	do {
		runnable->next = next;
	} while(!atomic_compare_exchange_weak_explicit(&list->head, &next, runnable, memory_order_release, memory_order_acquire));
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
