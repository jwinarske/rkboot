/* SPDX-License-Identifier: CC0-1.0 */
#include <rki2c.h>
#include <rki2c_regs.h>
#include <assert.h>
#include <log.h>
#include <inttypes.h>
#include <die.h>

struct rki2c_config UNUSED rki2c_calc_config_v1(u32 ctrl_mhz, u32 max_hz, u32 rise_ns, u32 fall_ns) {
	assert(max_hz > 0);
	u32 min_scl_high, min_scl_low;
	u32 min_data_setup, max_data_hold;
	u32 min_start_setup, min_stop_setup;
	if (max_hz <= 100000) { /* standard-mode */
		min_scl_low = 4700, min_scl_high = 4000;
		min_data_setup = 250, max_data_hold = 3450;
		min_start_setup = 4700, min_stop_setup = 4000;
	} else if (max_hz <= 400000) { /* fast-mode */
		min_scl_low = 1300, min_scl_high = 600;
		min_data_setup = 100, max_data_hold = 900;
		min_start_setup = 600, min_stop_setup = 600;
	} else { /* fast-mode+ */
		assert(max_hz <= 1000000);
		min_scl_low = 500, min_scl_high = 260;
		min_data_setup = 50, max_data_hold = 400;
		min_start_setup = 260, min_stop_setup = 260;
	}
	u32 divh = div_round_up_u32((rise_ns + min_scl_high) * ctrl_mhz, 8000);
	u32 divl = div_round_up_u32((fall_ns + min_scl_low) * ctrl_mhz, 8000);
	u32 total_div = div_round_up_u32(ctrl_mhz * (1000000 / 8), max_hz);
	if (divh + divl < total_div) {
		/* rise/fall timings allow for higher frequencies, distribute the additional time equally between high and low, rounding towards low */
		u32 total_bonus = total_div - divh - divl;
		u32 high_bonus = total_bonus / 2;
		divh += high_bonus;
		divl += total_bonus - high_bonus;
	} else {
		max_hz = ctrl_mhz * (1000000 / 8) / (divl + divh);
		info("limiting frequency to %"PRIu32" Hz\n", max_hz);
	}
	assert(divh <= 0x10000 && divl <= 0x10000); /* field limits */
	u32 step = 8000 * divh;
	u32 setup_start = div_round_up_u32(ctrl_mhz * (rise_ns + min_start_setup) - 1000, step);
	u32 setup_stop = div_round_up_u32(ctrl_mhz * (rise_ns + min_stop_setup) - 1000, step);
	assert(setup_start < 4 && setup_stop < 4);
	u32 data_upd_st;
	for_range(i, 0, 6) {
		u32 val = 5 - i;
		/* we follow the Linux calculations here, not the formula in the TRM */
		u32 setup_cycles = (8 - val) * divl + 1, hold_cycles = val * divl + 1;
		u32 setup_ns = setup_cycles * 1000 / ctrl_mhz;
		u32 hold_ns = div_round_up_u32(hold_cycles * 1000, ctrl_mhz);
		if (hold_ns <= max_data_hold && setup_ns >= min_data_setup) {
			data_upd_st = val;
			goto found;
		}
	}
	die("could not satisfy data setup/hold times at %"PRIu32" Hz with %"PRIu32" ns rise / %"PRIu32" ns fall time\n", max_hz, rise_ns, fall_ns);
	found:;
	struct rki2c_config res = {.control = setup_start << 12 | setup_stop << 14 | data_upd_st << 8, .clkdiv = (divh - 1) << 16 | (divl - 1)};
	return res;
}
