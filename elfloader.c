/* SPDX-License-Identifier: CC0-1.0 */
#include <rk3399/dramstage.h>
#include <inttypes.h>
#include <main.h>
#include <uart.h>
#include <rk3399.h>
#include <rk3399/payload.h>
#include <rkpll.h>
#include <stage.h>
#include <async.h>
#include <rki2c.h>
#include <rki2c_regs.h>
#include <exc_handler.h>
#include <rkgpio_regs.h>
#include <dump_mem.h>
#if CONFIG_ELFLOADER_SPI
#include <rkspi.h>
#include <rkspi_regs.h>
#include "rk3399_spi.h"
#endif
#include <gic.h>
#include <gic_regs.h>
#if CONFIG_ELFLOADER_SD
#include <dwmmc.h>
#endif
#include <runqueue.h>

static const struct mapping initial_mappings[] = {
	MAPPING_BINARY,
	{.first = 0, .last = (u64)&__start__ - 1, .flags = MEM_TYPE_NORMAL},
	{.first = 0x4100000, .last = 0xf7ffffff, .flags = MEM_TYPE_NORMAL},
	{.first = (u64)uart, .last = (u64)uart + 0xfff, .flags = MEM_TYPE_DEV_nGnRnE},
	{.first = 0, .last = 0, .flags = 0}
};

static const struct address_range critical_ranges[] = {
	{.first = __start__, .last = __end__ - 1},
	{.first = uart, .last = uart},
	ADDRESS_RANGE_INVALID
};

void sync_exc_handler(struct exc_state_save UNUSED *save) {
	u64 elr, esr, far;
	__asm__("mrs %0, esr_el3; mrs %1, far_el3; mrs %2, elr_el3" : "=r"(esr), "=r"(far), "=r"(elr));
	die("sync exc@0x%"PRIx64": ESR_EL3=0x%"PRIx64", FAR_EL3=0x%"PRIx64"\n", elr, esr, far);
}

static void init_payload_desc(struct payload_desc *payload) {
	payload->elf_start = (u8 *)elf_addr;
	payload->elf_end =  (u8 *)blob_addr;
	payload->fdt_start = (u8 *)fdt_addr;
	payload->fdt_end = (u8 *)fdt_out_addr;
	payload->kernel_start = (u8 *)payload_addr;
	payload->kernel_end = __start__;

#if CONFIG_ELFLOADER_INITCPIO
	payload->initcpio_start = (u8 *)initcpio_addr;
	payload->initcpio_end = (u8 *)(DRAM_START + dram_size());
#endif
}

UNINITIALIZED _Alignas(16) u8 exc_stack[4096] = {};

static struct sched_runqueue runqueue = {.head = 0, .tail = &runqueue.head};

struct sched_runqueue *get_runqueue() {return &runqueue;}

static u64 _Alignas(4096) UNINITIALIZED pagetable_frames[20][512];
u64 (*const pagetables)[512] = pagetable_frames;
const size_t num_pagetables = ARRAY_SIZE(pagetable_frames);

_Noreturn u32 main(u64 sctlr) {
	puts("elfloader\n");
	struct stage_store store;
	store.sctlr = sctlr;
	stage_setup(&store);
	sync_exc_handler_spx = sync_exc_handler_sp0 = sync_exc_handler;
	mmu_setup(initial_mappings, critical_ranges);
	mmu_map_mmio_identity(0xff750000, 0xff77ffff);	/* {PMU,}CRU, GRF */
	mmu_map_mmio_identity(0xff310000, 0xff33ffff);	/* PMU{,SGRF,GRF} */
	mmu_map_range(0xff8c0000, 0xff8dffff, 0xff8c0000, MEM_TYPE_NORMAL);	/* SRAM */
	mmu_map_range(0xff3b0000, 0xff3b1fff, 0xff3b0000, MEM_TYPE_NORMAL);	/* PMUSRAM */
	mmu_map_mmio_identity(0xff3d0000, 0xff3dffff);	/* i2c4 */
	mmu_map_mmio_identity((u64)gpio0, (u64)gpio0 + 0xfff);
	dsb_ishst();

	assert_msg(rkpll_switch(pmucru + PMUCRU_PPLL_CON), "PPLL did not lock-on\n");
	/* clk_i2c4 = PPLL/4 = 169 MHz, DTS has 200 */
	pmucru[PMUCRU_CLKSEL_CON + 3] = SET_BITS16(7, 0);
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

	struct payload_desc payload;
	init_payload_desc(&payload);

#if CONFIG_ELFLOADER_IRQ
	mmu_map_mmio_identity(0xfee00000, 0xfeffffff);
	dsb_ishst();
	gicv3_per_cpu_setup(gic500r);
	u64 xfer_start = get_timestamp();
#endif

#if CONFIG_ELFLOADER_DECOMPRESSION
	load_compressed_payload(&payload);
#endif

#if CONFIG_ELFLOADER_IRQ
	u64 xfer_end = get_timestamp();
	printf("transfer finished after %zu μs\n", (xfer_end - xfer_start) / CYCLES_PER_MICROSECOND);
	gicv3_per_cpu_teardown(gic500r);
#endif
	commit(&payload, &store);
}
