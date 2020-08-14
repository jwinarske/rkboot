/* SPDX-License-Identifier: CC0-1.0 */
/* this code tests sched_aarch64 code and thus must be built for aarch64 */
#include <sched_aarch64.h>
#include <stdio.h>

void thread(struct sched_runqueue *rq) {
	puts("thread\n");
	sched_yield(rq);
	puts("thread after yield\n");
}

_Alignas(4096) char stack[4096];

int main() {
	struct sched_runqueue rq = {.head = 0, .tail = &rq.head};
	puts("main\n");
	struct sched_thread_start thread_start = {
		.runnable = {.next = 0, .run = sched_start_thread},
		.pc = (u64)thread,
		.pad = 0,
		.args = {(u64)&rq, },
	}, *runnable = (struct sched_thread_start *)(stack + sizeof(stack) - sizeof(struct sched_thread_start));
	*runnable = thread_start;
	sched_yield(sched_queue(&rq, (struct sched_runnable *)runnable));
	puts("main after yield");
	sched_yield(&rq);
	puts("main end");
	return 0;
}
