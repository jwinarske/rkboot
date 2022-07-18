/* SPDX-License-Identifier: CC0-1.0 */
#include <stdatomic.h>
#include <stdbool.h>
#include <inttypes.h>

#include <compression.h>

#include <arch/context.h>
#include <irq.h>
#include <mmu.h>

#include <sdhci.h>
#include <dwmmc.h>

#include <aarch64.h>
#include <gic.h>
#include <gic_regs.h>

#include <rkpll.h>
#include <rksaradc_regs.h>
#include <rkgpio_regs.h>
#include <rktimer_regs.h>
#include <uart.h>

#include <rk3399.h>
#include <rk3399/sramstage.h>
#include <stage.h>
#include "dram/ddrinit.h"

static UNINITIALIZED _Alignas(4096) u8 vstack_frames[NUM_VSTACK][VSTACK_DEPTH];
void *const boot_stack_end = (void*)VSTACK_BASE(VSTACK_CPU0);

volatile struct uart *const console_uart = regmap_uart;

const struct mmu_multimap initial_mappings[] = {
#include <rk3399/base_mappings.inc.c>
	VSTACK_MULTIMAP(CPU0),
	{}
};

static struct ddrinit_state ddrinit_st;

#if CONFIG_EMMC
extern struct sdhci_state emmc_state;
#endif
extern struct dwmmc_state sdmmc_state;

void plat_handler_fiq() {
	u64 grp0_intid;
	__asm__ volatile("mrs %0, "ICC_IAR0_EL1";msr DAIFClr, #0xf" : "=r"(grp0_intid));
	atomic_signal_fence(memory_order_acquire);
	switch (grp0_intid) {
	case 35:
	case 36:
		ddrinit_irq(&ddrinit_st, grp0_intid - 35);
		break;
#if CONFIG_EMMC
	case 43:
		sdhci_irq(&emmc_state);
		break;
#endif
#if CONFIG_SD
	case 97:
		dwmmc_irq(&sdmmc_state);
		break;
#endif
	case 101:	/* stimer0 */
		regmap_stimer0[0].interrupt_status = 1;
#ifdef DEBUG_MSG
		if (get_timestamp() < 2400000) {logs("tick\n");}
#else
		dsb_st();	/* apparently the interrupt clear isn't fast enough, wait for completion */
#endif
#if CONFIG_SD
		dwmmc_wake_waiters(&sdmmc_state);
#endif
		struct thread *th;
		asm volatile("mrs %0, TPIDR_EL3" : "=r"(th));
		if (th) {
			atomic_fetch_or_explicit(&th->status, 1 << CTX_STATUS_PREEMPT_REQ_BIT, memory_order_relaxed);
		}
		break;	/* do nothing, just want to wake the main loop */
	default:
		sramstage_late_irq(grp0_intid);
	}
	atomic_signal_fence(memory_order_release);
	__asm__ volatile(
		"msr DAIFSet, #0xf;"
		"msr "ICC_EOIR0_EL1", %0;"
		"msr "ICC_DIR_EL1", %0"
	: : "r"(grp0_intid));
}
void plat_handler_irq() {
	die("unexpected IRQ on EL3");
}

static const size_t start_flags = 0
#if !CONFIG_SD
	| RK3399_INIT_SD_INIT
#endif
#if !CONFIG_EMMC
	| RK3399_INIT_EMMC_INIT
#endif
#if !CONFIG_PCIE
	| RK3399_INIT_PCIE
#endif
;

_Atomic(size_t) rk3399_init_flags = start_flags;

static u64 _Alignas(4096) UNINITIALIZED pagetable_frames[6][512];
u64 (*const pagetables)[512] = pagetable_frames;
const size_t num_pagetables = ARRAY_SIZE(pagetable_frames);

static struct sched_runqueue runqueue = {};

struct sched_runqueue *get_runqueue() {return &runqueue;}

struct thread threads[] = {
	THREAD_START_STATE(VSTACK_BASE(VSTACK_DDRC0), ddrinit_primary, (u64)&ddrinit_st),
	THREAD_START_STATE(VSTACK_BASE(VSTACK_DDRC1), ddrinit_secondary, (u64)&ddrinit_st),
#if CONFIG_SD
	THREAD_START_STATE(VSTACK_BASE(VSTACK_SDMMC), rk3399_init_sdmmc, ),
#endif
#if CONFIG_EMMC
	THREAD_START_STATE(VSTACK_BASE(VSTACK_EMMC), emmc_init, (u64)&emmc_state),
#endif
#if CONFIG_PCIE
	THREAD_START_STATE(VSTACK_BASE(VSTACK_PCIE), pcie_init, ),
#endif
};

void rk3399_set_init_flags(size_t flags) {
	atomic_fetch_or_explicit(&rk3399_init_flags, flags, memory_order_release);
}

_Noreturn void main() {
	/* GPIO0A2: red LED on RockPro64 and Pinebook Pro, not connected on Rock Pi 4 */
	regmap_gpio0->port |= 1 << 2;
	regmap_gpio0->direction |= 1 << 2;

	pmu_cru_setup();

	static volatile struct gic_distributor *const gic500d = regmap_gic500d;
	static volatile struct gic_redistributor *const gic500r = regmap_gic500r;
	gicv2_global_setup(gic500d);
	gicv3_per_cpu_setup(gic500r);
	static const struct {
		u16 intid;
		u8 priority;
		u8 targets;
		u32 flags;
	} intids[] = {
		{35, 0x80, 1, IGROUP_0 | INTR_LEVEL},	/* ddrc0 */
		{36, 0x80, 1, IGROUP_0 | INTR_LEVEL},	/* ddrc1 */
		{43, 0x80, 1, IGROUP_0 | INTR_LEVEL},	/* emmc */
		{97, 0x80, 1, IGROUP_0 | INTR_LEVEL},	/* sd */
		{101, 0x80, 1, IGROUP_0 | INTR_LEVEL},	/* stimer0 */
	};
	for_array(i, intids) {
		gicv2_setup_spi(regmap_gic500d, intids[i].intid, intids[i].priority, intids[i].targets, intids[i].flags);
	}
	regmap_gic500c->control = 0xb;

	ddrinit_configure(&ddrinit_st);

	misc_init();

	for_range(i, VSTACK_CPU0+1, NUM_VSTACK) {
		u64 limit = VSTACK_BASE(i) - VSTACK_DEPTH;
		mmu_map_range(limit, limit + (VSTACK_DEPTH - 1), (u64)&vstack_frames[i][0], MEM_TYPE_NORMAL);
	}
	mmu_flush();

	for_array(i, threads) {
		sched_queue_single(CURRENT_RUNQUEUE, (struct sched_runnable *)(threads + i));
	}

	size_t reported = 0, last = start_flags;
	while (1) {
		size_t flags = atomic_load_explicit(&rk3399_init_flags, memory_order_acquire);
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
		irq_mask();
		struct sched_runnable *r = sched_unqueue(get_runqueue());
		if (r) {
			irq_unmask();
			arch_sched_run(r);
		} else {
			bool quit = true;
			for_array(i, threads) {
				if ((atomic_load_explicit(&threads[i].status, memory_order_acquire) & 0xf) != THREAD_DEAD) {
					quit = false;
					break;
				}
			}
			if (quit) {
				irq_unmask();
				break;
			}
			aarch64_wfi();
			irq_unmask();
		}
	}

	for_array(i, intids) {gicv2_disable_spi(gic500d, intids[i].intid);}
	gicv2_wait_disabled(gic500d);
	info("[%"PRIuTS"] sramstage finish\n", get_timestamp());
	sramstage_late();
}
