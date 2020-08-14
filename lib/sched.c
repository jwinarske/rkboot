/* SPDX-License-Identifier: CC0-1.0 */
#include <irq.h>
#include <runqueue.h>
#include <log.h>
#include <plat/sched.h>

void sched_queue(struct sched_runnable *runnable) {
	runnable->next = 0;
	{irq_save_t irq = irq_save_mask();
		struct sched_runqueue *rq = get_runqueue();
		*rq->tail = runnable;
		rq->tail = &runnable->next;
	irq_restore(irq);}
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

_Noreturn void sched_next(struct sched_runqueue *rq) {
	struct sched_runnable *runnable;
	do {
		{irq_save_t irq = irq_save_mask();
			runnable = rq->head;
			if (runnable) {
				if (!(rq->head = runnable->next)) {
					rq->tail = &rq->head;
				}
			} else {
				cpuidle();
			}
		irq_restore(irq);}
	} while (!runnable);
	runnable->run(rq, runnable);
}
