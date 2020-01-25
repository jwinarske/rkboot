#include <main.h>
#include <rk3399.h>
#include "rk3399-dmc.h"

struct mr_adjustments {
	struct mr_adj_regset {
		u8 pctl_regs[2], pctl_shift;
		u8 pi_regs[2], pi_shifts[2];
		u8 pad;
	} loc[3];
	u16 pctl_mask, pi_mask;
	char name[8];
};

const struct mr_adjustments dq_odt_adj = {{
	{{139, 153}, 24, {132, 139}, {0, 16}, 0},
	{{140, 154}, 0, {129, 137}, {16, 0}, 0},
	{{140, 154}, 8, {127, 134}, {0, 16}, 0},
}, 0x7, 0x7, "DQ ODT"};

const struct mr_adjustments ca_odt_adj = {{
	{{139, 152}, 28, {132, 139}, {4, 20}, 0},
	{{140, 154}, 4, {129, 137}, {20, 4}, 0},
	{{140, 154}, 12, {127, 134}, {4, 20}, 0},
}, 0x7, 0x7, "CA ODT"};

const struct mr_adjustments mr3_adj = {{
	{{138, 152}, 0, {131, 139}, {16, 0}, 0},
	{{138, 152}, 16, {129, 136}, {0, 16}, 0},
	{{139, 153}, 0, {126, 134}, {16, 0}, 0},
}, 0xffff, 0xffff, "MR3"};

const struct mr_adjustments mr12_adj = {{
	{{140, 154}, 16, {132, 139}, {8, 24}, 0},
	{{141, 155}, 0, {129, 137}, {24, 8}, 0},
	{{141, 155}, 16, {127, 134}, {8, 24}, 0},
}, 0xffff, 0xff, "MR12"};

const struct mr_adjustments mr14_adj = {{
	{{142, 156}, 16, {132, 140}, {16, 0}, 0},
	{{143, 157}, 0, {130, 137}, {0, 16}, 0},
	{{143, 157}, 16, {127, 135}, {16, 0}, 0},
}, 0xffff, 0xff, "MR14"};

void mr_adjust(volatile u32 *pctl, volatile u32 *pi, const struct mr_adjustments *adj, u32 regset, u32 val) {
	debug("setting %s to %x, pctl mask 0x%x\n", (const char *)&adj->name[0], val, (u32)adj->pctl_mask);
	const struct mr_adj_regset *rs = &adj->loc[regset];
	u32 pctl_mask = (u32)adj->pctl_mask << rs->pctl_shift;
	u32 pctl_value = (val & adj->pctl_mask) << rs->pctl_shift;
	for_range(i, 0, 2) {
		debug("pctl%u mask %x value %x\n", (u32)rs->pctl_regs[i], pctl_mask, pctl_value);
		volatile u32 *reg = &pctl[rs->pctl_regs[i]];
		debug("read %08x\n", *reg);
		*reg = (*reg & ~pctl_mask) | pctl_value;
		debug("readback %08x\n", *reg);
	}
	u32 pi_mask = adj->pi_mask, pi_value = val & pi_mask;
	for_range(i, 0, 2) {
		for_range(j, 0, 2) {
			volatile u32 *reg = &pi[rs->pi_regs[j] + 15*i];
			u8 shift = rs->pi_shifts[j];
			*reg = (*reg & ~(pi_mask << shift)) | pi_value << shift;
		}
	}
}
