/* SPDX-License-Identifier: CC0-1.0 */
#include <main.h>
#include <rk3399.h>

static inline u64 get_timer_value() {
	u64 res;
	__asm__ volatile("mrs %0, CNTPCT_EL0" : "=r"(res));
	return res;
}

void udelay(u32 usec) {
	u64 start = get_timer_value();
	while (get_timer_value() - start < usec * CYCLES_PER_MICROSECOND) {
		__asm__ volatile("yield");
	}
}

u64 get_timestamp() {
	return get_timer_value();
}

struct timer {
	u32 load_count0;
	u32 load_count1;
	u32 value0;
	u32 value1;
	u32 load_count2;
	u32 load_count3;
	u32 interrupt_status;
	u32 control;
};
_Static_assert(sizeof(struct timer) == 32, "wrong size for timer struct");

static volatile struct timer *const stimer6 = (volatile struct timer *)0xff868000;

void NO_ASAN setup_timer() {
	u64 freq = 0x016e3600;
	__asm__ volatile("msr CNTFRQ_EL0, %0": : "r"(freq));
	stimer6[5].control = 0;
	stimer6[5].load_count0 = ~(u32)0;
	stimer6[5].load_count1 = ~(u32)0;
	stimer6[5].control = 1;
}
