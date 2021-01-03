/* SPDX-License-Identifier: CC0-1.0 */
#include <rk3399/sramstage.h>
#include <assert.h>

#include <mmu.h>
#include <compression.h>
#include <die.h>

extern const u8 _binary_dramstage_lz4_start[], _binary_dramstage_lz4_end[];

extern const struct decompressor lz4_decompressor;

_Noreturn void next_stage(u64 x0, u64 x1, u64 x2, u64 x3, void *entry, void *stack);

_Noreturn void end_sramstage() {
	void *loadaddr = (void *)0x4000000;
	mmu_map_range(0, 0xf7ffffff, 0, MEM_TYPE_NORMAL);
	__asm__ volatile("dsb sy");
	size_t size;
	assert_msg(lz4_decompressor.probe(_binary_dramstage_lz4_start, _binary_dramstage_lz4_end, &size) == COMPR_PROBE_SIZE_KNOWN, "could not probe dramstage compression frame\n");
	struct decompressor_state *state = (struct decompressor_state *)(loadaddr + (size + LZCOMMON_BLOCK + sizeof(max_align_t) - 1)/sizeof(max_align_t)*sizeof(max_align_t));
	const u8 *ptr = lz4_decompressor.init(state, _binary_dramstage_lz4_start, _binary_dramstage_lz4_end);
	assert(ptr);
	state->window_start = state->out = loadaddr;
	state->out_end = loadaddr + size;
	while (state->decode) {
		size_t res = state->decode(state, ptr, _binary_dramstage_lz4_end);
		assert(res >= NUM_DECODE_STATUS);
		ptr += res - NUM_DECODE_STATUS;
	}
	next_stage(0, 0, 0, 0, loadaddr, (void *)0x1000);
}
