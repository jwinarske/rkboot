/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

struct sdram_geometry {
	u8 csmask;
	u8 width;
	u8 col;
	u8 bank;
	u8 cs0_row, cs1_row;
};

#define DEFINE_CHANNEL_STATES\
	X(UNINIT) X(CONFIGURED)\
	X(INIT) X(CS0_MR5) X(READY) X(SWITCHED)\
	X(CALVL) X(WRLVL) X(GTLVL) X(RDLVL) X(WDQLVL)\
	X(TRAINED) X(INACTIVE)

enum channel_state {
#define X(name) CHAN_ST_##name,
	DEFINE_CHANNEL_STATES
#undef X
	NUM_CHAN_ST
};

extern const char ddrinit_chan_state_names[NUM_CHAN_ST][12];

enum {
	DDRINIT_PER_CS_TRAINING = 1,
};

struct ddrinit_state {
	enum channel_state chan_st[2];
	struct sdram_geometry geo[2];
	u8 training_idx[2];
	u32 training_flags;
	_Atomic(size_t) sync;
};

void ddrinit_configure(struct ddrinit_state *st);
void ddrinit_irq(struct ddrinit_state *st, u32 ch);
void ddrinit_train(struct ddrinit_state *st);
