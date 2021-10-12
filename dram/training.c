/* SPDX-License-Identifier: CC0-1.0 */
#include <inttypes.h>
#include <stdatomic.h>

#include <die.h>
#include <log.h>
#include <runqueue.h>

#include <arch/context.h>

#include <rk3399.h>
#include <rk3399/sramstage.h>
#include "rk3399-dmc.h"
#include "ddrinit.h"

static void set_per_cs_training_index(volatile struct phy_regs *phy, u32 rank) {
	debug("training idx %"PRIu32"\n", rank);
	if (phy->dslice[0][84] & (1 << 16)) {
		debugs("set per-cs training\n");
		for_dslice(i) {apply32v(&phy->dslice[i][8], SET_BITS32(1, rank) << 24);}
	}
}

static _Bool UNUSED ca_training(u32 idx, volatile u32 *pi, volatile struct phy_regs *phy) {
	apply32v(pi + 100, SET_BITS32(2, 2) << 8);
	apply32v(pi + 92, (SET_BITS32(1, 1) << 16) | (SET_BITS32(2, idx) << 24));
	while (1) {
		u32 status = pi[174];
		u32 obs0 = phy->aslice[0][20];
		u32 obs1 = phy->aslice[1][20];
		u32 obs2 = phy->aslice[2][20];
		if (((obs0 | obs1 | obs2) >> 30) != 0) {puts("obs error");return 0;}
		if (status & (1 << 13)) {puts("flag error");return 0;}
		if ((status & (1 << 19)) && (status & (1 << 21))) {break;}
		sched_yield(CURRENT_RUNQUEUE);
	}
	pi[175] = 0x3f7c;
	return 1;
}

static _Bool write_leveling(u32 idx, volatile u32 *pi, volatile struct phy_regs *phy) {
	apply32v(pi + 60, SET_BITS32(2, 2) << 8);
	apply32v(pi + 59, (SET_BITS32(1, 1) << 8) | (SET_BITS32(2, idx) << 16));
	while (1) {
		u32 status = pi[174];
		for_dslice(i) {if (phy->dslice[i][40] & (1 << 12)) {puts("obs error");return 0;}}
		if (status & (1 << 12)) {puts("flag error");return 0;}
		if ((status & (1 << 18)) && (status & (1 << 21))) {break;}
		sched_yield(CURRENT_RUNQUEUE);
	}
	pi[175] = 0x3f7c;
	return 1;
}

static _Bool read_gate_training(u32 idx, volatile u32 *pi, volatile struct phy_regs *phy) {
	apply32v(pi + 80, SET_BITS32(2, 2) << 24);
	apply32v(pi + 74, (SET_BITS32(1, 1) << 16) | (SET_BITS32(2, idx) << 24));
	while (1) {
		u32 status = pi[174];
		for_dslice(i) {if (phy->dslice[i][43] & (3 << 22)) {puts("obs error"); return 0;}}
		if (status & (1 << 11)) {puts("flag error");return 0;}
		if ((status & (1 << 17)) && (status & (1 << 21))) {break;}
		sched_yield(CURRENT_RUNQUEUE);
	}
	pi[175] = 0x3f7c;
	return 1;
}

static _Bool read_leveling(u32 idx, volatile u32 *pi) {
	apply32v(pi + 80, SET_BITS32(2, 2) << 16);
	apply32v(pi + 74, (SET_BITS32(1, 1) << 8) | (SET_BITS32(2, idx) << 24));
	while (1) {
		u32 status = pi[174];
		if (status & (1 << 10)) {puts("flag error");return 0;}
		if ((status & (1 << 16)) && (status & (1 << 21))) {break;}
		sched_yield(CURRENT_RUNQUEUE);
	}
	pi[175] = 0x3f7c;
	return 1;
}

static _Bool wdq_leveling(u32 idx, volatile u32 *pi) {
	pi[117] |= 1 << 8;
	apply32v(pi + 124, SET_BITS32(2, 2) << 16);
	apply32v(pi + 121, (SET_BITS32(1, 1) << 8) | (SET_BITS32(2, idx) << 16));
	while (1) {
		u32 status = pi[174];
		if (status & (1 << 14)) {puts("flag error");return 0;}
		if ((status & (1 << 20)) && (status & (1 << 21))) {break;}
		sched_yield(CURRENT_RUNQUEUE);
	}
	pi[175] = 0x3f7c;
	return 1;
}

_Bool train_channel(u32 ch, u32 csmask, volatile u32 *pctl, volatile u32 *pi, volatile struct phy_regs *phy) {
	u32 mask = csmask | csmask << 2;
	_Bool training_fail = 0;
	phy->PHY_GLOBAL(927) |= 1 << 22;

	/*pi[175] = 0x3f7c;
	for_range(idx, 0, 4) {
		set_per_cs_training_index(phy, idx);
		if (!ca_training(idx, pi, phy)) {
			printf("channel %u CA training failed\n", ch);
			training_fail = 1;
		}
	}
	apply32v(pi + 100, SET_BITS32(2, 0) << 8);*/

	pi[175] = 0x3f7c; /* clear interrupt flags */
	/* FIXME: range should be rank */
	for_range(idx, 0, 4) {
		if (!(mask & 1 << idx)) {continue;}
		set_per_cs_training_index(phy, idx);
		if (!write_leveling(idx, pi, phy)) {
			printf("channel %u write leveling failed\n", ch);
			training_fail = 1;
		} else {
			debug("write leveling finished for channel %u\n", ch);
		}
	}
	/* override write leveling value */
	phy->PHY_GLOBAL(896) |= 1;
	for_dslice(i) {apply32v(&phy->dslice[i][8], SET_BITS32(1, 1) << 16);}
	for_dslice(i) {apply32v(&phy->dslice[i][63], SET_BITS32(16, 0x0200) << 16);}
	phy->PHY_GLOBAL(896) &= ~(u32)1;
	pctl[200] |= 1 << 8;
	apply32v(pi + 60, SET_BITS32(2, 0) << 8);

	pi[175] = 0x3f7c; /* clear interrupt flags */
	for_range(idx, 0, 4) {
		if (!(mask & 1 << idx)) {continue;}
		set_per_cs_training_index(phy, idx);
		if (!read_gate_training(idx, pi, phy)) {
			printf("channel %u read gate training failed\n", ch);
			training_fail = 1;
		} else {
			debug("read gate training finished for channel %u\n", ch);
		}
	}
	apply32v(pi + 80, SET_BITS32(2, 0) << 24);

	pi[175] = 0x3f7c; /* clear interrupt flags */
	for_range(idx, 0, 4) {
		if (!(mask & 1 << idx)) {continue;}
		set_per_cs_training_index(phy, idx);
		if (!read_leveling(idx, pi)) {
			printf("channel %u read leveling failed\n", ch);
			training_fail = 1;
		} else {
			debug("read leveling finished for channel %u\n", ch);
		}
	}
	apply32v(pi + 80, SET_BITS32(2, 0) << 16);

	pi[175] = 0x3f7c; /* clear interrupt flags */
	for_range(idx, 0, 4) {
		if (!(mask & 1 << idx)) {continue;}
		set_per_cs_training_index(phy, idx);
		if (!wdq_leveling(idx, pi)) {
			printf("channel %u wdq leveling failed\n", ch);
			training_fail = 1;
		} else {
			debug("wdq leveling finished for channel %u\n", ch);
		}
	}
	apply32v(pi + 124, SET_BITS32(2, 0) << 16);

	phy->PHY_GLOBAL(927) &= ~(1 << 22);
	return !training_fail;
}

static void training_task(struct ddrinit_state *st, u32 ch) {
	debug("training started\n");
	assert_msg(train_channel(ch, st->geo[ch].csmask, pctl_base_for(ch), pi_base_for(ch), phy_for(ch)), "training failed\n");
}

u8 ddrinit_wait(struct ddrinit_state *st, u8 val) {
	while (1) {
		u8 v = atomic_load_explicit(&st->sync, memory_order_acquire);
		if (val != v) {
			debug("woke up on sync=0x%02"PRIx8"\n", v);
			return v;
		}
		debug("sleeping on sync=0x%02"PRIx8"\n", val);
		call_cc_ptr2_int2(sched_finish_u8, &st->sync, &st->waiters, 0xff, val);
	}
}
void ddrinit_notify(struct ddrinit_state *st, u8 val) {
	debug("sync=0x%"PRIx8"\n", val);
	atomic_store_explicit(&st->sync, val, memory_order_release);
	sched_queue_list(CURRENT_RUNQUEUE, &st->waiters);
}

void ddrinit_secondary(struct ddrinit_state *st) {
	u8 sync = 0;
	puts("ddrinit_secondary started");
	while (1) {
		sync = ddrinit_wait(st, sync);
		if (sync == 3) {return;}
		if (sync != 2) {continue;}
		puts("ddrinit_secondary training");
		training_task(st, 1);
		ddrinit_notify(st, 1);
		sync = 1;
	}
}

void ddrinit_train(struct ddrinit_state *st) {
	ddrinit_notify(st, 2);
	training_task(st, 0);
	if (ddrinit_wait(st, 2) != 1){die("sync error\n");}
}
