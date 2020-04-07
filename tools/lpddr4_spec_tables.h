#pragma once
#include <stdint.h>
#define TABULATED_VALUES\
	X(RL_nDBI) X(RL_DBI) X(WL_A) X(WL_B) X(nWR) X(nRTP) X(MRVal)

enum lpddr4_latency_value {
#define X(name) LAT_##name,
	TABULATED_VALUES
#undef X
	NUM_LATENCY_VALUES
};

struct frequency_step {
	uint16_t MHz;
	uint8_t values[NUM_LATENCY_VALUES];
};

static const struct frequency_step frequency_table[] = {
	{.MHz = 266, .values = {
		[LAT_RL_nDBI] = 6, [LAT_RL_DBI] = 6,
		[LAT_WL_A] = 4, [LAT_WL_B] = 4,
		[LAT_nWR] = 6, [LAT_nRTP] = 8,
		[LAT_MRVal] = 0
	}}, {.MHz = 533, .values = {
		[LAT_RL_nDBI] = 10, [LAT_RL_DBI] = 12,
		[LAT_WL_A] = 6, [LAT_WL_B] = 8,
		[LAT_nWR] = 10, [LAT_nRTP] = 8,
		[LAT_MRVal] = 1
	}}, {.MHz = 800, .values = {
		[LAT_RL_nDBI] = 14, [LAT_RL_DBI] = 16,
		[LAT_WL_A] = 8, [LAT_WL_B] = 12,
		[LAT_nWR] = 16, [LAT_nRTP] = 8,
		[LAT_MRVal] = 2
	}},
};
