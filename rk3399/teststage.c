/* SPDX-License-Identifier: CC0-1.0 */
#include <inttypes.h>
#include <stdio.h>

#include <die.h>
#include <fdt.h>

#include <arch/context.h>
#include <mmu.h>

#include <stage.h>

#define DEFINE_VSTACK X(CPU0)
#define VSTACK_DEPTH 0x1000

#define DEFINE_REGMAP\
	MMIO(GIC500D, gic500d, 0xfee00000, struct gic_distributor)\
	MMIO(GIC500R, gic500r, 0xfef00000, struct gic_redistributor)\
	MMIO(STIMER0, stimer0, 0xff860000, struct rktimer_regs)\
	MMIO(GPIO0, gpio0, 0xff720000, struct rkgpio_regs)\
	MMIO(UART, uart, 0xff1a0000, struct uart)\
	MMIO(CRU, cru, 0xff760000, u32)\
	MMIO(PMUCRU, pmucru, 0xff750000, u32)\
	MMIO(PMUGRF, pmugrf, 0xff320000, u32)\
	/* the generic SoC registers are last, because they are referenced often, meaning they get addresses 0xffffxxxx, which can be generated in a single MOVN instruction */
#define DEFINE_REGMAP64K\
	X(GRF, grf, 0xff770000, u32)\

#include <rk3399/vmmap.h>

static UNINITIALIZED _Alignas(4096) u8 vstack_frames[NUM_VSTACK][VSTACK_DEPTH];
void *const boot_stack_end = (void*)VSTACK_BASE(VSTACK_CPU0);

static u64 _Alignas(4096) UNINITIALIZED pagetable_frames[11][512];
u64 (*const pagetables)[512] = pagetable_frames;
const size_t num_pagetables = ARRAY_SIZE(pagetable_frames);

volatile struct uart *const console_uart = regmap_uart;

const struct mmu_multimap initial_mappings[] = {
	{.addr = 0x100000, .desc = MMU_MAPPING(UNCACHED, 0x100000)},
	/* continue to start of binary mapping */
#include <rk3399/base_mappings.inc.c>
	{.addr = (u64)&__end__, .desc = MMU_MAPPING(UNCACHED, (u64)&__end__)},
	{.addr = 0xf8000000, .desc = 0},
	VSTACK_MULTIMAP(CPU0),
	{}
};

void plat_handler_fiq() {
	die("unexpected FIQ");
}
void plat_handler_irq() {
	die("unexpected IRQ");
}

static struct sched_runqueue runqueue = {};
struct sched_runqueue *get_runqueue() {return &runqueue;}

void dump_fdt(const struct fdt_header *);

_Noreturn void main(u64 x0) {
	printf("FDT pointer: %"PRIx64"\n", x0);
	const struct fdt_header *fdt = (const struct fdt_header*)x0;
	dump_fdt(fdt);
	halt_and_catch_fire();
}
