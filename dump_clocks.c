#include <stdio.h>

#include <rk3399.h>

static inline u32 ubfx32(u32 v, u32 shift, u32 length) {
	return v >> shift & ((1 << length) - 1);
}

static void dump_pll(const char *name, volatile u32 *base) {
	u32 con3 = base[3], con1 = base[1], con2 = base[2];
	u32 refdiv = ubfx32(con1, 0, 6), fbdiv = base[0];
	u32 postdiv1 = ubfx32(con1, 8, 3), postdiv2 = ubfx32(con1, 12, 3);
	printf(
		"%s@%zx: fbdiv=%u refdiv=%u postdiv=%u,%u",
		name, (u64)base,
		fbdiv,
		refdiv,
		postdiv1, postdiv2
	);
	if (con3 & 8) {
		puts(" integer");
	} else {
		printf(" fracdiv=%u", con2);
	}
	u32 mode = ubfx32(con3, 8, 2);
	puts(mode == 0 ? " slow" : mode == 1 ? " normal" : " deep slow");
	if (con3 & 0x40) {puts(" 4phasepd");}
	if (con3 & 0x20) {puts(" vcopd");}
	if (con3 & 0x10) {puts(" postdivpd");}
	if (con3 & 4) {puts(" dacpd");}
	if (con3 & 2) {puts(" bypass");}
	if (con3 & 1) {puts(" globalpd");}
	printf(" ssmod=%x", base[4] << 16 | base[5]);
	if (mode == 0) {
		puts("→ 24 MHz\n");
	} else if (mode == 2) {
		puts("→ 32.768 KHz");
	} else if (con3 & 8) {
		printf("→ %u MHz\n", 24 * fbdiv / refdiv / postdiv1 / postdiv2);
	} else {
		printf("→ %u MHz\n", 24 * (fbdiv * 224 + con2) / 224 / refdiv / postdiv1 / postdiv2);
	}
}

void dump_clocks(volatile u32 *pmucru, volatile u32 *cru) {
	const struct {
		const char name[8];
		volatile u32 *addr;
	} plls[] = {
		{"PPLL", pmucru + 0},
		{"LPLL", cru + CRU_LPLL_CON},
		{"BPLL", cru + CRU_BPLL_CON},
		{"DPLL", cru + (0x40/4)},
		{"CPLL", cru + (0x60/4)},
		{"GPLL", cru + (0x80/4)},
		{"NPLL", cru + (0xa0/4)},
		{"VPLL", cru + (0xc0/4)},
	};
	for_array(i, plls) {
		dump_pll(plls[i].name, plls[i].addr);
	}
	for_range(i, 0, 4) {
		printf("clksel%u: %x\n", i, cru[0x100/4 + i]);
	}
}
