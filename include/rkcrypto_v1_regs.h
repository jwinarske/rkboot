/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <stdint.h>

struct rkcrypto_v1_regs {
	uint32_t interrupt_status;
	uint32_t interrupt_enable;
	uint32_t control;
	uint32_t config;
	uint32_t brdma_start_addr;
	uint32_t btdma_start_addr;
	uint32_t brdma_length;
	uint32_t hrdma_start_addr;
	uint32_t hrdma_length;
	char padding0[0x80 - 0x24];
	uint32_t aes_control;
	uint32_t aes_status;
	uint32_t aes_input[4];
	uint32_t aes_output[4];
	uint32_t aes_iv[4];
	uint32_t aes_key[8];
	uint32_t aes_counter[4];
	char padding1[0x100 - 0xe8];
	uint32_t tdes_control;
	uint32_t tdes_status;
	uint32_t tdes_input[2];
	uint32_t tdes_output[2];
	uint32_t tdes_iv[2];
	uint32_t tdes_keys[3][2];
	char padding2[0x180 - 0x138];
	uint32_t hash_control;
	uint32_t hash_status;
	uint32_t hash_message_length;
	uint32_t hash_output[8];
	uint32_t hash_seed[5];
	char padding3[0x200 - 0x1c0];
	uint32_t trng_control;
	uint32_t trng_output[8];
	char padding4[0x280 - 0x224];
	uint32_t pka_control;
	char padding5[0x400 - 0x284];
	uint32_t pka_m[64];
	uint32_t pka_c[64];
	uint32_t pka_n[64];
	uint32_t pka_e[64];
};
_Static_assert(sizeof(struct rkcrypto_v1_regs) == 0x800, "wrong size");

enum {
	RKCRYPTO_V1_CTRL_TRNG_START_BIT = 8,
};

#define RKCRYPTO_V1_TRNG_ENABLE(sample_period) ((uint32_t)1 << 16 | (uint16_t)(sample_period))
#define RKCRYPTO_V1_TRNG_DISABLE (0)
