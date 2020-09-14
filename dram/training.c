/* SPDX-License-Identifier: CC0-1.0 */
#include <stdatomic.h>
#include <inttypes.h>

#include <main.h>
#include <rk3399.h>
#include <runqueue.h>
#include <sched_aarch64.h>
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
		if (((obs0 | obs1 | obs2) >> 30) != 0) {puts("obs error\n");return 0;}
		if (status & (1 << 13)) {puts("flag error\n");return 0;}
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
		if (status & (1 << 12)) {puts("flag error\n");return 0;}
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
		for_dslice(i) {if (phy->dslice[i][43] & (3 << 22)) {puts("obs error\n"); return 0;}}
		if (status & (1 << 11)) {puts("flag error\n");return 0;}
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
		if (status & (1 << 10)) {puts("flag error\n");return 0;}
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
		if (status & (1 << 14)) {puts("flag error\n");return 0;}
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
	udelay(100);
	puts("test\n");
	udelay(100);
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
	puts("training started\n");
	assert_msg(train_channel(ch, st->geo[ch].csmask, pctl_base_for(ch), pi_base_for(ch), phy_for(ch)), "training failed\n");
	size_t bit = 1 << ch;
	size_t res = atomic_fetch_or_explicit(&st->sync, bit, memory_order_seq_cst) | bit;
	printf("sync%zu\n", res);
	if (res != 3) {return;}
	logs("finished.\n");
	/* 256B interleaving */
	ddrinit_set_channel_stride(0xd);
	__asm__ volatile("dsb ish");
	mmu_unmap_range(0xffa80000, 0xffa8ffff);
	for_range(bit, 10, 32) {
		if (test_mirror(MIRROR_TEST_ADDR, bit)) {die("mirroring detected\n");}
	}
	mmu_unmap_range(0, 0xf7ffffff);
	rk3399_set_init_flags(RK3399_INIT_DRAM_READY);
}

void ddrinit_train(struct ddrinit_state *st) {
	struct sched_thread_start thread_start = {
		.runnable = {.next = 0, .run = sched_start_thread},
		.pc = (u64)training_task,
		.pad = 0,
		.args = {(u64)st, 1},
	}, *runnable = (struct sched_thread_start *)(vstack_base(SRAMSTAGE_VSTACK_DDRC1) - sizeof(struct sched_thread_start));
	*runnable = thread_start;
	sched_queue_single(CURRENT_RUNQUEUE, &runnable->runnable);
	training_task(st, 0);
}
