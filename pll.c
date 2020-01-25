#include <main.h>
#include <rk3399.h>

struct postdiv_setting {
	u16 mhz;
	u8 pd1;
	u8 pd2;
};

static const struct postdiv_setting postdivs[] = {
	/*{51, 6, 3},*/
	{201, 3, 2},
	{301, 4, 1},
	{529, 3, 1},
	{601, 2, 1},
	{667, 4, 1},
	{733, 2, 1},
	{801, 3, 1}
};

enum {PLL_SLOW = 0, PLL_NORMAL = 1, PLL_DEEP_SLOW = 2};

_Bool setup_pll(volatile u32 *base, u32 mhz) {
	u8 postdiv1 = 1, postdiv2 = 1;
	for (u8 i = 0; i < ARRAY_SIZE(postdivs); i += 1) {
		if (mhz < postdivs[i].mhz) {
			postdiv1 = postdivs[i].pd1;
			postdiv2 = postdivs[i].pd2;
			break;
		}
	}
	const u32 vco = (u32)mhz * postdiv1 * postdiv2;
	const u16 fbdiv = vco / 24;
	const u8 refdiv = 1;
	debug("PLL@%08zx: vco=%u pd1=%u pd2=%u ⇒ %u MHz … ", (u64)base, (unsigned)vco, (unsigned)postdiv1, (unsigned)postdiv2, (unsigned)mhz);
	base[3] = SET_BITS16(2, PLL_SLOW) << 8; /* | SET_BITS16(1, 1) << 3; */
	base[0] = SET_BITS16(12, fbdiv);
	base[1] = (SET_BITS16(3, postdiv2) << 12) | (SET_BITS16(3, postdiv1) << 8) | SET_BITS16(6, refdiv);
	_Bool locked;
	for (u16 i = 0; i < 1000; i += 1) {
		locked = base[2] >> 31;
		if (locked) {break;}
		udelay(1);
	}
	if (!locked) {
		debugs("lock failed\n");
	} else {
		debugs("locked on\n");
		base[3] = SET_BITS16(2, PLL_NORMAL) << 8;
	}
	return locked;
}
