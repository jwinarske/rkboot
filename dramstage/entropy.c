/* SPDX-License-Identifier: CC0-1.0 */
#include <rk3399/dramstage.h>
#include <rk3399.h>
#include <rkcrypto_v1_regs.h>
#include <log.h>

u32 entropy_buffer[1024] = {};
u16 entropy_words = 0;
static u16 entropy_pos = 0;
_Static_assert(ARRAY_SIZE(entropy_buffer) % ARRAY_SIZE(regmap_crypto1->trng_output) == 0, "entropy buffer not evenly divisible by TRNG output size");

void pull_entropy(_Bool keep_running) {
	volatile struct rkcrypto_v1_regs *crypto1 = regmap_crypto1;
	u32 control = crypto1->control;
	if (!keep_running) {
		crypto1->trng_control = RKCRYPTO_V1_TRNG_DISABLE;
		crypto1->control = SET_BITS16(1, 0) << RKCRYPTO_V1_CTRL_TRNG_START_BIT;
	}
	if (~control & 1 << RKCRYPTO_V1_CTRL_TRNG_START_BIT) {
		debugs("TRNG ready\n");
		static const u32 words = ARRAY_SIZE(crypto1->trng_output);
		for_range(i, 0, words) {entropy_buffer[entropy_pos + i] ^= crypto1->trng_output[i];}
		if (entropy_words < ARRAY_SIZE(entropy_buffer)) {entropy_words += words;}
		entropy_pos = (entropy_pos + words) % ARRAY_SIZE(entropy_buffer);
		if (keep_running) {
			crypto1->control = SET_BITS16(1, 1) << RKCRYPTO_V1_CTRL_TRNG_START_BIT;
		}
	}
}
