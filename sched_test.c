/* SPDX-License-Identifier: CC0-1.0 */
/* this code tests sched_aarch64 code and thus must be built for aarch64 */
#include <sched_aarch64.h>
#include <stdio.h>

void thread() {
	puts("thread\n");
	sched_yield(CURRENT_RUNQUEUE);
	puts("thread after yield\n");
}

_Alignas(4096) char stack[4096];

struct sched_runqueue rq = {};
struct sched_runqueue *get_runqueue() {return &rq;}

int main() {
	puts("main\n");
	struct sched_thread_start thread_start = {
		.runnable = {.next = 0, .run = sched_start_thread},
		.pc = (u64)thread,
		.pad = 0,
		.args = {},
	}, *runnable = (struct sched_thread_start *)(stack + sizeof(stack) - sizeof(struct sched_thread_start));
	*runnable = thread_start;
	sched_queue(CURRENT_RUNQUEUE, (struct sched_runnable *)runnable);
	sched_yield(CURRENT_RUNQUEUE);
	puts("main after yield");
	sched_yield(CURRENT_RUNQUEUE);
	puts("main end");
	return 0;
}
