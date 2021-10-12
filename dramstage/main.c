/* SPDX-License-Identifier: CC0-1.0 */
#include <rk3399/dramstage.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <inttypes.h>

#include <async.h>
#include <iost.h>
#include <runqueue.h>

#include <irq.h>
#include <mmu.h>
#include <arch/context.h>

#include <dwmmc.h>
#include <sdhci.h>

#include <gic.h>
#include <gic_regs.h>

#include <rkgpio_regs.h>
#include <rki2c.h>
#include <rki2c_regs.h>
#include <rkpll.h>
#include <rkspi.h>
#include <rktimer_regs.h>

#include <rk3399.h>
#include <rk3399/payload.h>
#include <stage.h>

static UNINITIALIZED _Alignas(4096) u8 vstack_frames[NUM_VSTACK][VSTACK_DEPTH];
void *const boot_stack_end = (void*)VSTACK_BASE(VSTACK_CPU0);

volatile struct uart *const console_uart = regmap_uart;

const struct mmu_multimap initial_mappings[] = {
#include <rk3399/base_mappings.inc.c>
	{.addr = 0, MMU_MAPPING(NORMAL, 0)},
	{.addr = (u64)&__start__, .desc = 0},
	{.addr = 0x4100000, MMU_MAPPING(NORMAL, 0x4100000)},
	{.addr = 0xf8000000, .desc = 0},
	{.addr = 0xff8c0000, .desc = MMU_MAPPING(NORMAL, 0xff8c0000)},
	{.addr = 0xff8f0000, .desc = 0},
	{.addr = 0xff3b0000, .desc = MMU_MAPPING(NORMAL, 0xff3b0000)},
	{.addr = 0xff3b2000, .desc = 0},
	VSTACK_MULTIMAP(CPU0),
	{}
};

static void sync_exc_handler() {
	u64 elr, esr, far;
	__asm__("mrs %0, esr_el3; mrs %1, far_el3; mrs %2, elr_el3" : "=r"(esr), "=r"(far), "=r"(elr));
	die("sync exc@0x%"PRIx64": ESR_EL3=0x%"PRIx64", FAR_EL3=0x%"PRIx64"\n", elr, esr, far);
}

extern struct sdhci_state emmc_state;
extern struct async_transfer spi1_async, sdmmc_async;
extern struct rkspi_xfer_state spi1_state;
extern struct dwmmc_state sdmmc_state;

static void irq_handler() {
	u64 grp0_intid;
	__asm__ volatile("mrs %0, "ICC_IAR0_EL1";msr DAIFClr, #0xf" : "=r"(grp0_intid));
	atomic_signal_fence(memory_order_acquire);
	switch (grp0_intid) {
#if CONFIG_EMMC
	case 43:
		sdhci_irq(&emmc_state);
		break;
#endif
#if CONFIG_SPI
	case 85:
		rkspi_handle_interrupt(&spi1_state, regmap_spi1);
		dsb_sy();
		break;
#endif
#if CONFIG_SD
	case 97:
		dwmmc_irq(&sdmmc_state);
		break;
#endif
	case 101:	/* stimer0 */
		regmap_stimer0[0].interrupt_status = 1;
		pull_entropy(1);
#if DEBUG_MSG
		if (get_timestamp() < 12000000) {logs("tick\n");}
#else
		dsb_st();	/* apparently the interrupt clear isn't fast enough, wait for completion */
#endif
#if CONFIG_EMMC
		sdhci_wake_threads(&emmc_state);
#endif
#if CONFIG_SD
		dwmmc_wake_waiters(&sdmmc_state);
#endif
		break;	/* do nothing, just want to wake the main loop */
	default:
		die("unexpected intid%"PRIu64"\n", grp0_intid);
	}
	atomic_signal_fence(memory_order_release);
	__asm__ volatile("msr DAIFSet, #0xf;msr "ICC_EOIR0_EL1", %0" : : "r"(grp0_intid));
}

static struct payload_desc payload_descriptor;

struct payload_desc *get_payload_desc() {
	struct payload_desc *payload = &payload_descriptor;
	payload->elf_start = (u8 *)elf_addr;
	payload->elf_end =  (u8 *)blob_addr;
	payload->fdt_start = (u8 *)fdt_addr;
	payload->fdt_end = (u8 *)fdt_out_addr;
	payload->kernel_start = (u8 *)payload_addr;
	payload->kernel_end = __start__;

#if CONFIG_DRAMSTAGE_INITCPIO
	payload->initcpio_start = (u8 *)initcpio_addr;
	payload->initcpio_end = (u8 *)(DRAM_START + dram_size());
#endif
	return payload;
}

static struct sched_runqueue runqueue = {.head = 0, .tail = &runqueue.head};

struct sched_runqueue *get_runqueue() {return &runqueue;}

_Static_assert(32 >= 3 * NUM_BOOT_MEDIUM, "not enough bits for boot medium");
static const size_t available_boot_media = 0
#if CONFIG_SD
	| 1 << 4*BOOT_MEDIUM_SD
#endif
#if CONFIG_EMMC
	| 1 << 4*BOOT_MEDIUM_EMMC
#endif
#if CONFIG_NVME
	| 1 << 4*BOOT_MEDIUM_NVME
#endif
#if CONFIG_SPI
	| 1 << 4*BOOT_MEDIUM_SPI
#endif
;
static const u32 all_exit_mask = 0
#define X(name) | 1 << 4*BOOT_MEDIUM_##name
	DEFINE_BOOT_MEDIUM
#undef X
;
static _Atomic(u32) boot_state = available_boot_media ^ all_exit_mask;
static _Atomic(u32) current_boot_cue = BOOT_CUE_NONE;
static struct sched_runnable_list boot_monitors;
static struct sched_runnable_list boot_cue_waiters;

static const char boot_medium_names[NUM_BOOT_MEDIUM][8] = {
#define X(name) #name,
	DEFINE_BOOT_MEDIUM
#undef X
};

static u32 monitor_boot_state(u32 state, u32 mask, u32 expected) {
	while ((state & mask) == expected) {
		printf("monitoring boot state mask0x%08"PRIx32" exp0x%08"PRIx32"\n", mask, expected);
		call_cc_ptr2_int2(sched_finish_u32, &boot_state, &boot_monitors, mask, expected);
		state = atomic_load_explicit(&boot_state, memory_order_acquire);
		printf("boot monitor woke up: 0x%08"PRIx32"\n", state);
	}
	return state;
}

void boot_medium_exit(enum boot_medium medium) {
	printf("boot medium exit\n");
	atomic_fetch_or_explicit(&boot_state, 1 << 4*medium, memory_order_release);
	sched_queue_list(CURRENT_RUNQUEUE, &boot_monitors);
}

void boot_medium_loaded(enum boot_medium medium) {
	atomic_fetch_or_explicit(&boot_state, 4 << 4*medium, memory_order_release);
	sched_queue_list(CURRENT_RUNQUEUE, &boot_monitors);
}

_Bool wait_for_boot_cue(enum boot_medium medium) {
	atomic_fetch_or_explicit(&boot_state, 2 << 4*medium, memory_order_release);
	sched_queue_list(CURRENT_RUNQUEUE, &boot_monitors);
	while (1) {
		u32 curr = atomic_load_explicit(&current_boot_cue, memory_order_acquire);
		if (curr == BOOT_CUE_EXIT) {
			return 0;
		} else if (curr == medium) {
			return 1;
		}
		call_cc_ptr2_int2(sched_finish_u32, &current_boot_cue, &boot_cue_waiters, ~(u32)0, curr);
	}
}

static u64 _Alignas(4096) UNINITIALIZED pagetable_frames[20][512];
u64 (*const pagetables)[512] = pagetable_frames;
const size_t num_pagetables = ARRAY_SIZE(pagetable_frames);

static void boot_monitor() {
	u32 state = atomic_load_explicit(&boot_state, memory_order_acquire);
	_Bool payload_loaded = 0;
	for_range(boot_medium, 0, NUM_BOOT_MEDIUM) {
		u32 exit_bit = 1 << (4*boot_medium);
		if (~available_boot_media & exit_bit) {continue;}
		u32 ready_bit = exit_bit << 1;
		state = monitor_boot_state(state, ready_bit | exit_bit, 0);
		if (state & exit_bit) {
			printf("%s failed, going on to next\n", boot_medium_names[boot_medium]);
			continue;
		}
		atomic_store_explicit(&current_boot_cue, boot_medium, memory_order_release);
		sched_queue_list(CURRENT_RUNQUEUE, &boot_cue_waiters);
		printf("cued %s\n", boot_medium_names[boot_medium]);
		u64 xfer_start = get_timestamp();

		u32 loaded_bit = exit_bit << 2;
		state = monitor_boot_state(state, loaded_bit | exit_bit, 0);

		if (state & loaded_bit) {
			u64 xfer_end = get_timestamp();
			printf("[%"PRIuTS"] payload loaded in %"PRIuTS" μs\n", xfer_end, (xfer_end - xfer_start) / CYCLES_PER_MICROSECOND);
			/* leave the user at least 500 ms to let go for each payload  */
			while (get_timestamp() - xfer_start < USECS(500000)) {usleep(100);}
			u32 gpio_bits = regmap_gpio0->read;
			debug("GPIO0: %"PRIx32"\n", gpio_bits);
			if ((~gpio_bits & 32) && available_boot_media >> 1 >> boot_medium != 0) {
				printf("boot overridden\n");
				continue;
			}
			payload_loaded = 1;
			break;
		}
		printf("boot medium failed, going on to next\n");
	}
	if (!payload_loaded) {
		die("no payload available\n");
	}
	atomic_store_explicit(&current_boot_cue, BOOT_CUE_EXIT, memory_order_release);
	sched_queue_list(CURRENT_RUNQUEUE, &boot_cue_waiters);
}

struct thread threads[] = {
	THREAD_START_STATE(VSTACK_BASE(VSTACK_MONITOR), boot_monitor, ),
#if CONFIG_SD
	THREAD_START_STATE(VSTACK_BASE(VSTACK_SD), boot_sd, ),
#endif
#if CONFIG_EMMC
	THREAD_START_STATE(VSTACK_BASE(VSTACK_EMMC), boot_emmc, ),
#endif
#if CONFIG_NVME
	THREAD_START_STATE(VSTACK_BASE(VSTACK_NVME), boot_nvme, ),
#endif
#if CONFIG_SPI
	THREAD_START_STATE(VSTACK_BASE(VSTACK_SPI), boot_spi, ),
#endif
};

_Noreturn void main() {
	sync_exc_handler_spx = sync_exc_handler_sp0 = sync_exc_handler;
	puts("dramstage");

	/* set DRAM as Non-Secure; needed for DMA */
	regmap_pmusgrf[PMUSGRF_DDR_RGN_CON+16] = SET_BITS16(1, 1) << 9;

	/* clk_i2c4 = PPLL/4 = 169 MHz, DTS has 200 */
	regmap_pmucru[PMUCRU_CLKSEL_CON + 3] = SET_BITS16(7, 0);

	volatile u32 *const pmugrf = regmap_pmugrf;
	volatile struct rki2c_regs *const i2c4 = regmap_i2c4;
	printf("RKI2C4_CON: %"PRIx32"\n", i2c4->control);
	struct rki2c_config i2c_cfg = rki2c_calc_config_v1(169, 1000000, 600, 20);
	printf("setup %"PRIx32" %"PRIx32"\n", i2c_cfg.control, i2c_cfg.clkdiv);
	i2c4->clkdiv = i2c_cfg.clkdiv;
	i2c4->control = i2c_cfg.control;
	pmugrf[PMUGRF_GPIO1B_IOMUX] = SET_BITS16(2, 1) << 6 | SET_BITS16(2, 1) << 8;
	i2c4->control = i2c_cfg.control | RKI2C_CON_ENABLE | RKI2C_CON_START | RKI2C_CON_MODE_REGISTER_READ | RKI2C_CON_ACK;
	i2c4->slave_addr = 1 << 24 | 0x62 << 1;
	i2c4->reg_addr = 1 << 24 | 0;
	i2c4->rx_count = 1;
	u32 val;
	while (!((val = i2c4->int_pending) & RKI2C_INTMASK_XACT_END)) {}
	printf("RKI2C4_CON: %"PRIx32", _IPD: %"PRIx32"\n", i2c4->control, i2c4->int_pending);
	_Bool is_pbp = !(val & 1 << RKI2C_INT_NAK);
	printf("%"PRIx32"\n", i2c4->rx_data[0]);
	i2c4->control = i2c_cfg.control | RKI2C_CON_ENABLE | RKI2C_CON_STOP;

	if (is_pbp) {
		mmu_map_mmio_identity(0xff420000, 0xff420fff);
		dsb_ishst();
		info("ACK from i2c4-62, this seems to be a Pinebook Pro\n");
		*(volatile u32*)0xff420024 = 0x96e;
		*(volatile u32*)0xff420028 = 0x25c;
		*(volatile u32*)0xff42002c = 0x13;
        pmugrf[PMUGRF_GPIO1C_IOMUX] = SET_BITS16(2, 1) << 6;
	} else {
		info("not running on a Pinebook Pro ⇒ not setting up regulators\n");
	}

	struct payload_desc *payload = get_payload_desc();

	if (available_boot_media) {
		fiq_handler_same = irq_handler_same = irq_handler;
		gicv3_per_cpu_setup(regmap_gic500r);
		static const struct {
			u16 intid;
			u8 priority;
			u8 targets;
			u32 flags;
		} intids[] = {
			{43, 0x80, 1, IGROUP_0 | INTR_LEVEL},	/* emmc */
			//{85, 0x80, 1, IGROUP_0 | INTR_LEVEL},	/* spi */
			{97, 0x80, 1, IGROUP_0 | INTR_LEVEL},	/* sd */
			{101, 0x80, 1, IGROUP_0 | INTR_LEVEL},	/* stimer0 */
		};
		for_array(i, intids) {
			gicv2_setup_spi(regmap_gic500d, intids[i].intid, intids[i].priority, intids[i].targets, intids[i].flags);
		}

		for_range(i, VSTACK_CPU0+1, NUM_VSTACK) {
			u64 limit = VSTACK_BASE(i) - VSTACK_DEPTH;
			mmu_map_range(limit, limit + (VSTACK_DEPTH - 1), (u64)&vstack_frames[i][0], MEM_TYPE_NORMAL);
		}
		dsb_ishst();
		for_array(i, threads) {
			sched_queue_single(CURRENT_RUNQUEUE, (struct sched_runnable *)(threads + i));
		}

		while (1) {
			irq_mask();
			struct sched_runnable *r = sched_unqueue(get_runqueue());
			if (r) {
				irq_unmask();
				arch_sched_run(r);
			} else  {
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

		for_array(i, intids) {
			gicv2_disable_spi(regmap_gic500d, intids[i].intid);
		}
		gicv2_wait_disabled(regmap_gic500d);
		gicv3_per_cpu_teardown(regmap_gic500r);
	} else {
#if CONFIG_DRAMSTAGE_DECOMPRESSION
		struct async_dummy async = {
			.async = {async_pump_dummy},
			.buf = blob_buffer,
		};
		decompress_payload(&async.async);
#endif
	}
	fiq_handler_same = irq_handler_same = 0;

	commit(payload);
}
