/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#define UNUSED __attribute__((unused))

typedef unsigned char u8;

static inline void rc4(u8 *buf, size_t len, u8 *state) {
	u8 i = state[256], j = state[257];
	while (len--) {
		i += 1;
		j += state[i];
		u8 a = state[i], b = state[j];
		state[i] = b;
		state[j] = a;
		*buf++ ^= state[(a + b) % 256];
	}
	state[256] = i;
	state[257] = j;
}

static inline void rc4_init(u8 *state) {
	static const u8 key[16] = {124,78,3,4,85,5,9,7,45,44,123,56,23,13,23,17};
	for (size_t i = 0; i < 256; i += 1) {
		state[i] = (u8)i;
	}
	state[256] = 0;
	state[257] = 0;
	u8 j = 0;
	for (size_t i = 0; i < 256; i += 1) {
		j += state[i] + key[i % 16];
		u8 tmp = state[i];
		state[i] = state[j];
		state[j] = tmp;
	}
}
