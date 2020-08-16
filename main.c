/* SPDX-License-Identifier: CC0-1.0 */
#include <stdatomic.h>
#include <inttypes.h>

#include <main.h>
#include <uart.h>
#include <rk3399.h>
#include <rk3399/sramstage.h>
#include <stage.h>
#include <compression.h>
#include <exc_handler.h>
#include <rkpll.h>
#include <rksaradc_regs.h>
#include <rkgpio_regs.h>
#include <gic.h>
#include <gic_regs.h>
#include <rktimer_regs.h>
#include <irq.h>
#include "dram/ddrinit.h"
#include <aarch64.h>
#include <sched_aarch64.h>

static const struct mapping initial_mappings[] = {
	MAPPING_BINARY_SRAM,
	{.first = (u64)uart, .last = (u64)uart + 0xfff, .flags = MEM_TYPE_DEV_nGnRnE},
	{.first = 0xff8c0000, .last = 0xff8c1fff, .flags = MEM_TYPE_NORMAL},
	{.first = 0, .last = 0, .flags = 0}
};

static const struct address_range critical_ranges[] = {
	{.first = __start__, .last = __end__ - 1},
	{.first = uart, .last = uart},
	ADDRESS_RANGE_INVALID
};

void sync_exc_handler() {
	u64 esr, far;
	__asm__("mrs %0, esr_el3; mrs %1, far_el3" : "=r"(esr), "=r"(far));
	die("sync exc: ESR_EL3=0x%"PRIx64", FAR_EL3=0x%"PRIx64"\n", esr, far);
}

static struct ddrinit_state ddrinit_st;

void irq_handler(struct exc_state_save UNUSED *save) {
	u64 grp0_intid;
	__asm__ volatile("mrs %0, "ICC_IAR0_EL1";msr DAIFClr, #0xf" : "=r"(grp0_intid));
	atomic_signal_fence(memory_order_acquire);
	switch (grp0_intid) {
	case 35:
	case 36:
		ddrinit_irq(&ddrinit_st, grp0_intid - 35);
		break;
	case 101:	/* stimer0 */
		stimer0[0].interrupt_status = 1;
#if DEBUG_MSG
		if (get_timestamp() < 2400000) {logs("tick\n");}
#else
		dsb_st();	/* apparently the interrupt clear isn't fast enough, wait for completion */
#endif
		break;	/* do nothing, just want to wake the main loop */
	default:
		die("unexpected intid%"PRIu64"\n", grp0_intid);
	}
	atomic_signal_fence(memory_order_release);
	__asm__ volatile("msr DAIFSet, #0xf;msr "ICC_EOIR0_EL1", %0" : : "r"(grp0_intid));
}

static const size_t start_flags = 0
#if !CONFIG_SD
	| RK3399_INIT_SD_INIT
#endif
;

static const size_t root_flags = RK3399_INIT_DRAM_READY | RK3399_INIT_SD_INIT;

_Atomic(size_t) rk3399_init_flags = start_flags;

UNINITIALIZED _Alignas(16) u8 exc_stack[4096] = {};
static UNINITIALIZED _Alignas(4096) u8 vstack_frames[NUM_SRAMSTAGE_VSTACK][4096];

static struct sched_runqueue runqueue = {};

struct sched_runqueue *get_runqueue() {return &runqueue;}

void rk3399_set_init_flags(size_t flags) {
	atomic_fetch_or_explicit(&rk3399_init_flags, flags, memory_order_release);
}

int32_t NO_ASAN main(u64 sctlr) {
	struct stage_store store;
	store.sctlr = sctlr;
	stage_setup(&store);

	/* GPIO0A2: red LED on RockPro64 and Pinebook Pro, not connected on Rock Pi 4 */
	gpio0->port |= 1 << 2;
	gpio0->direction |= 1 << 2;
	sync_exc_handler_spx = sync_exc_handler_sp0 = sync_exc_handler;
	mmu_setup(initial_mappings, critical_ranges);
	/* map {PMU,}CRU, GRF */
	mmu_map_mmio_identity(0xff750000, 0xff77ffff);
	/* map PMU{,SGRF,GRF} */
	mmu_map_mmio_identity(0xff310000, 0xff33ffff);
	__asm__("dsb ishst");

	pmu_cru_setup();

	mmu_map_mmio_identity(0xfee00000, 0xfeffffff);
	dsb_ishst();
	gicv2_global_setup(gic500d);
	fiq_handler_spx = irq_handler_spx = irq_handler;
	gicv3_per_cpu_setup(gic500r);
	static const struct {
		u16 intid;
		u8 priority;
		u8 targets;
		u32 flags;
	} intids[] = {
		{35, 0x80, 1, IGROUP_0 | INTR_LEVEL},	/* ddrc0 */
		{36, 0x80, 1, IGROUP_0 | INTR_LEVEL},	/* ddrc1 */
		{101, 0x80, 1, IGROUP_0 | INTR_LEVEL},	/* stimer0 */
	};
	for_array(i, intids) {
		gicv2_setup_spi(gic500d, intids[i].intid, intids[i].priority, intids[i].targets, intids[i].flags);
	}

	mmu_map_mmio_identity((u64)stimer0, (u64)stimer0 + 0xfff);
	dsb_ishst();
	stimer0[0].control = 0;
	stimer0[0].load_count2 = stimer0[0].load_count3 = stimer0[0].load_count0 = 0;
	stimer0[0].load_count0 = 24000;
	stimer0[0].interrupt_status = 1;
	stimer0[0].control = RKTIMER_ENABLE | RKTIMER_INT_EN;

	logs("jumping to ddrinit");
	ddrinit_configure(&ddrinit_st);

	for_range(i, 0, NUM_SRAMSTAGE_VSTACK) {
		u64 limit = 0x100005000 + i * 0x2000;
		mmu_map_range(limit, limit + 0xfff, (u64)&vstack_frames[i][0], MEM_TYPE_NORMAL);
	}
	dsb_ishst();

#if CONFIG_SD
	{struct sched_thread_start thread_start = {
		.runnable = {.next = 0, .run = sched_start_thread},
		.pc = (u64)rk3399_init_sdmmc,
		.pad = 0,
		.args = {},
	}, *runnable = (struct sched_thread_start *)(vstack_base(SRAMSTAGE_VSTACK_SDMMC) - sizeof(struct sched_thread_start));
	*runnable = thread_start;
	sched_queue_single(CURRENT_RUNQUEUE, &runnable->runnable);}
#endif

	size_t flags, reported = 0, last = start_flags;
	while (1) {
		flags = atomic_load_explicit(&rk3399_init_flags, memory_order_acquire);
		if ((flags & root_flags) == root_flags) {break;}
		timestamp_t now = get_timestamp();

		static const struct {
			u16 timeout;
			char name[14];
		} flags_data[NUM_RK3399_INIT] = {
#define X(_name, _timeout) {.timeout = _timeout, .name = #_name},
			DEFINE_RK3399_INIT_FLAGS
#undef X
		};
		size_t new_bits = flags & ~last;
		for_array(i, flags_data) {
			u16 timeout = flags_data[i].timeout;
			if ((~(flags | reported) & (size_t)1 << i) && now > (timestamp_t)timeout * 24000) {
				reported |= (size_t)1 << i;
				printf("%s not ready within %"PRIu16"ms\n", flags_data[i].name, timeout);
			}
			if ((new_bits & (size_t)1 << i)) {
				last |= (size_t)1 << i;
				printf("%s ready after %"PRIu64"μs\n", flags_data[i].name, now / TICKS_PER_MICROSECOND);
			}
		}
		sched_yield(CURRENT_RUNQUEUE);
	}

	for_array(i, intids) {gicv2_disable_spi(gic500d, intids[i].intid);}
	gicv3_per_cpu_teardown(gic500r);
	fiq_handler_spx = irq_handler_spx = 0;
	return end_sramstage(&store);
}
