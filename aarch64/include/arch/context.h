/* SPDX-License-Identifier: CC0-1.0 */
#pragma once

#define CTX_STATUS_OFF 0x8
#define CTX_STATUS_PREEMPT_REQ_BIT 4
#define CTX_SPSR_OFF 0xc
#define CTX_PC_OFF 0x10
#define CTX_VOLATILES_OFF 0x18
#define CTX_NONVOLATILES_OFF 0xb0

#define THREAD_LOCKED 0
#define THREAD_PREEMPTED 1
#define THREAD_STOPPED 2
#define THREAD_YIELDED 3
#define THREAD_DEAD 4
#define THREAD_WAITING 5

#ifndef __ASSEMBLER__
#include <runqueue.h>

struct thread {
	struct sched_runnable runnable;
	/// bits 0-3: see THREAD_* above
	/// bit 4: preempt request
	_Atomic(u32) status;
	u32 spsr;
	u64 pc;
	union {
		u64 gpr0[19];
	};
	u64 gpr19 [32 - 19];
};
CHECK_OFFSET(thread, status, CTX_STATUS_OFF);
CHECK_OFFSET(thread, pc, CTX_PC_OFF);
CHECK_OFFSET(thread, spsr, CTX_SPSR_OFF);
CHECK_OFFSET(thread, gpr0, CTX_VOLATILES_OFF);
CHECK_OFFSET(thread, gpr19, CTX_NONVOLATILES_OFF);

#define THREAD_START_STATE(sp, fn, ...) (struct thread) {\
	.runnable.next = 0, .status = THREAD_PREEMPTED, .spsr = 0xc,\
	.gpr0 = {__VA_ARGS__},\
	.gpr19 = {[30 - 19] = (u64)aarch64_abandon_thread, [31 - 19] = (sp)},\
	.pc = (u64)(fn)\
}

// === prototypes for platform handlers ===
void plat_handler_sync_thread(u64 elr);
void plat_handler_sync_cpu(u64 elr);
void plat_handler_sync_aarch64(u64 elr);
void plat_handler_sync_aarch32(u64 elr);
void plat_handler_irq();
void plat_handler_fiq();
void plat_handler_serror_same();
void plat_handler_serror_lower();

_Noreturn void aarch64_abandon_thread();

#endif
