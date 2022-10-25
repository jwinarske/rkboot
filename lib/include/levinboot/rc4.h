// SPDX-License-Identifier: CC0-1.0
#pragma once
#include <stddef.h>
#include <stdint.h>

static inline void rc4(uint8_t *buf, size_t len, uint8_t *state) {
	uint8_t i = state[256], j = state[257];
	while (len--) {
		i += 1;
		j += state[i];
		uint8_t a = state[i], b = state[j];
		state[i] = b;
		state[j] = a;
		*buf++ ^= state[(a + b) % 256];
	}
	state[256] = i;
	state[257] = j;
}

static inline void rc4_init(uint8_t *state, const uint8_t *key, size_t keylen) {
	for (size_t i = 0; i < 256; i += 1) {state[i] = (uint8_t)i;}
	state[256] = 0;
	state[257] = 0;
	uint8_t j = 0;
	for (size_t i = 0; i < 256; i += 1) {
		j += state[i] + key[i % keylen];
		uint8_t tmp = state[i];
		state[i] = state[j];
		state[j] = tmp;
	}
}


static const uint8_t RC4_RK_KEY[16] = {124,78,3,4,85,5,9,7,45,44,123,56,23,13,23,17};
