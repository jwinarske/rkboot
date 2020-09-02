/* SPDX-License-Identifier: CC0-1.0 */
#include <rk3399/dramstage.h>
#include <assert.h>

#include <log.h>
#include <die.h>
#include <stage.h>
#include <rk3399.h>
#include <rk3399/payload.h>
#include <fdt.h>
#include <rkgpio_regs.h>
#include <rkpll.h>
#include <rkcrypto_v1_regs.h>
#include TF_A_HEADER_PATH

static image_info_t bl33_image = {
	.image_base = 0x00600000,
	.image_size = 0x01000000,
	.image_max_size = 0x01000000,
};

static entry_point_info_t bl33_ep = {
	.h = {
		.type = PARAM_EP,
		.version = PARAM_VERSION_1,
		.size = sizeof(bl33_ep),
		.attr = EP_NON_SECURE,
	},
};

static bl_params_node_t bl33_node = {
	.image_id = BL33_IMAGE_ID,
	.image_info = &bl33_image,
	.ep_info = &bl33_ep,
};

static bl_params_t bl_params = {
	.h = {
		.type = PARAM_BL_PARAMS,
		.version = PARAM_VERSION_2,
		.size = sizeof(bl_params),
		.attr = 0
	},
	.head = &bl33_node,
};

static u64 elf_magic[3] = {
	0x00010102464c457f,
	0,
	0x0000000100b70002
};

struct elf_header {
	u64 magic[3];
	u64 entry;
	u64 prog_h_off;
	u64 sec_h_off;
	u32 flags;
	u16 elf_h_size;
	u16 prog_h_entry_size;
	u16 num_prog_h;
	u16 sec_h_entry_size;
	u16 num_sec_h;
	u16 sec_h_str_idx;
};

struct program_header {
	u32 type;
	u32 flags;
	u64 offset;
	u64 vaddr;
	u64 paddr;
	u64 file_size;
	u64 mem_size;
	u64 alignment;
};

typedef void (*bl31_entry)(bl_params_t *, u64, u64, u64);

static void load_elf(const struct elf_header *header) {
	for_range(i, 0, 16) {
		const u32 *x = (u32*)((u64)header + 16*i);
		printf("%2x0: %08x %08x %08x %08x\n", i, x[0], x[1], x[2], x[3]);
	}
	for_array(i, elf_magic) {
		assert_msg(header->magic[i] == elf_magic[i], "value 0x%016zx at offset %u != 0x%016zx", header->magic[i], 8*i, elf_magic[i]);
	}
	printf("Loading ELF: entry address %zx, %u program headers at %zx\n", header->entry, header->num_prog_h, header->prog_h_off);
	assert(header->prog_h_entry_size == 0x38);
	assert((header->prog_h_off & 7) == 0);
	for_range(i, 0, header->num_prog_h) {
		const struct program_header *ph = (const struct program_header*)((u64)header + header->prog_h_off + header->prog_h_entry_size * i);
		if (ph->type == 0x6474e551) {puts("ignoring GNU_STACK segment\n"); continue;}
		assert_msg(ph->type == 1, "found unexpected segment type %08x\n", ph->type);
		printf("LOAD %08zx…%08zx → %08zx\n", ph->offset, ph->offset + ph->file_size, ph->vaddr);
		assert(ph->vaddr == ph->paddr);
		assert(ph->flags == 7);
		assert(ph->offset % ph->alignment == 0);
		assert(ph->vaddr % ph->alignment == 0);
		u64 alignment = ph->alignment;
		assert(alignment % 16 == 0);
		const u64 words_copied = (ph->file_size + 7) >> 3;
		const u64 words_clear = ((ph->mem_size + 7) >> 3) - words_copied;
		const u64 *src = (const u64 *)((u64)header + ph->offset);
		const u64 *end = (const u64 *)ph->vaddr + words_copied;
		u64 *dest = (u64*)ph->vaddr;
		debug("copying to %"PRIx64"–%"PRIx64"\n", dest, (u64)end);
		while (dest < end) {*dest++ = *src++;}
		end += words_clear;
		debug("clearing to %"PRIx64"\n", (u64)end);
		while (dest < end) {*dest++ = 0;}
	}
}

_Noreturn void commit(struct payload_desc *payload, struct stage_store *store) {
	/* GPIO0B3: White and green LED on the RockPro64 and Pinebook Pro respectively, not connected on the Rock Pi 4 */
	gpio0->port |= 1 << 11;
	gpio0->direction |= 1 << 11;

	info("trng: %"PRIx32" %"PRIx32"\n", crypto1->control, crypto1->interrupt_status);
	crypto1->trng_control = RKCRYPTO_V1_TRNG_DISABLE;
	crypto1->control = SET_BITS16(1, 0) << RKCRYPTO_V1_CTRL_TRNG_START_BIT;

	u32 entropy[8];
	for_array(i, entropy) {entropy[i] = crypto1->trng_output[i];}
	info("%08"PRIx32" %08"PRIx32" %08"PRIx32" %08"PRIx32"\n",
	     entropy[0], entropy[1], entropy[2], entropy[3]);
	info("%08"PRIx32" %08"PRIx32" %08"PRIx32" %08"PRIx32"\n",
	     entropy[4], entropy[5], entropy[6], entropy[7]);

	struct fdt_addendum fdt_add = {
		.dram_start = DRAM_START + TZRAM_SIZE,
		.dram_size = dram_size() - TZRAM_SIZE,
		.entropy = entropy,
		.entropy_words = ARRAY_SIZE(entropy),
#ifdef CONFIG_ELFLOADER_INITCPIO
		.initcpio_start = (u64)payload->initcpio_start,
		.initcpio_end = (u64)payload->initcpio_end,
#else
		.initcpio_start = 0,
		.initcpio_end = 0,
#endif
	};

	const struct elf_header *header = (const struct elf_header*)payload->elf_start;
	load_elf(header);
	transform_fdt((const struct fdt_header *)payload->fdt_start, payload->fdt_end, (void *)fdt_out_addr, &fdt_add);

	bl33_ep.pc = (uintptr_t)payload->kernel_start;
	bl33_ep.spsr = 9; /* jump into EL2 with SPSel = 1 */
	bl33_ep.args.arg0 = fdt_out_addr;
	bl33_ep.args.arg1 = 0;
	bl33_ep.args.arg2 = 0;
	bl33_ep.args.arg3 = 0;
	assert_msg(rkpll_switch(cru + CRU_BPLL_CON), "BPLL did not lock-on\n");
	/* aclkm_core_b = clk_core_b = BPLL */
	cru[CRU_CLKSEL_CON + 2] = SET_BITS16(5, 0) << 8 | SET_BITS16(2, 1) << 6 | SET_BITS16(5, 0);
	cru[CRU_CLKGATE_CON+1] = SET_BITS16(8, 0);
	stage_teardown(store);
	fflush(stdout);
	((bl31_entry)header->entry)(&bl_params, 0, 0, 0);
	puts("return\n");
	halt_and_catch_fire();
}
