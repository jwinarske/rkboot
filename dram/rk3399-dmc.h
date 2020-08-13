/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

#define for_dslice(i) for (u32 i = 0; i < 4; ++i)
#define for_aslice(i) for (u32 i = 0; i < 3; ++i)
#define for_channel(i) for (u32 i = 0; i < 2; ++i)

struct odt_dram {
	u8 dq_odt;
	u8 ca_odt;
	u16 pdds;
	u16 dq_vref;
	u16 ca_vref;
};

#define ODT_DS_HI_Z	0x0
#define ODT_DS_240	0x1
#define ODT_DS_120	0x8
#define ODT_DS_80	0x9
#define ODT_DS_60	0xc
#define ODT_DS_48	0xd
#define ODT_DS_40	0xe
#define ODT_DS_34_3	0xf
struct odt_phy {
	u8 rd_odt;
	u8 wr_dq_drv;
	u8 wr_ca_drv;
	u8 wr_ckcs_drv;
	u32 rd_vref; /* unit %, range 3.3% - 48.7% */
	_Bool rd_odt_en;
	u8 soc_odt;
};

struct odt_preset {
	struct odt_dram dram;
	struct odt_phy phy;
};

enum odt_situation {
	ODT_RD = 0,
	ODT_IDLE,
	ODT_WR_DQ,
	ODT_WR_CA,
	ODT_CKCS,
	ODT_NUM_SITUATIONS
};
enum odt_level {
	ODT_N = 0,
	ODT_P,
	ODT_NUM_LEVELS
};

enum odt_flags {
	ODT_RD_EN = 1,
	ODT_WR_EN = 2,
	ODT_IDLE_EN = 4,
	ODT_TSEL_ENABLE_MASK = 7,

	ODT_TSEL_CLEAN = 8,
	ODT_SET_RST_DRIVE = 16,
	ODT_SET_BOOST_SLEW = 32,
	ODT_SET_RX_CM_INPUT = 64,
};

struct odt_settings {
	u32 flags;
	u8 soc_odt;
	u8 padding;
	u16 padding2;
	u8 ds[ODT_NUM_SITUATIONS][ODT_NUM_LEVELS];
	u8 mode_dq, value_dq;
	u8 mode_ac, value_ac;
	u8 drive_mode;
};

enum {
	NUM_PCTL_REGS = 332,
	NUM_PI_REGS = 200,
	NUM_PHY_DSLICE_REGS = 91,
	NUM_PHY_ASLICE_REGS = 38,
	NUM_PHY_GLOBAL_REGS = 63
};

struct phy_regs {
	u32 dslice[4][128];
	u32 aslice[3][128];
	u32 global[128];
};
#define PHY_DELTA (128*7)
#define PHY_GLOBAL(n) global[(n) - PHY_DELTA]

struct phy_cfg {
	u32 dslice[NUM_PHY_DSLICE_REGS];
	u32 aslice[3][NUM_PHY_ASLICE_REGS];
	u32 global[NUM_PHY_GLOBAL_REGS];
};

struct phy_update {
	_Bool negedge_pll_switch;	/* PHY913 */
	u8 two_cycle_preamble;	/* dslice+7 */
	u16 grp_shift01;	/* PHY896 */
	u16 wraddr_shift45; /* aslice+1 */
	u32 pll_ctrl; /* PHY911 */
	u32 wraddr_shift0123; /* aslice+0 */
	u32 slave_master_delays[6]; /* aslice+32ff */
	u32 dslice_update[32]; /* dslice+59ff */
	u32 grp_slave_delay[3]; /* PHY916ff */
};

extern const struct phy_layout {
	u32 dslice, aslice, global_diff, ca_offs;
} reg_layout, cfg_layout;
struct dram_regs_cfg {
	u32 pctl[NUM_PCTL_REGS];
	u32 pi[NUM_PI_REGS];
	struct phy_cfg phy;
};
enum dramtype {
	DDR3 = 3,
	LPDDR4 = 7,
};
struct msch_config {
	u32 timing1, timing2, timing3;
	u32 dev2dev;
	u32 ddrmode;
};
struct dram_cfg {
	struct msch_config msch;
	struct dram_regs_cfg regs;
};

struct sdram_geometry {
	u8 csmask;
	u8 width;
	u8 col;
	u8 bank;
	u8 cs0_row, cs1_row;
};

#define DEFINE_PCTL_INTERRUPTS0\
	X(UNKNOWN0) X(UNKNOWN1) X(UNKNOWN2) X(INIT_DONE)\
	X(UNKNOWN4) X(UNKNOWN5) X(UNKNOWN6) X(UNKNOWN7)\
	X(UNKNOWN8) X(UNKNOWN9) X(UNKNOWN10) X(UNKNOWN11)\
	X(MRR_ERROR) X(UNKNOWN13) X(UNKNOWN14) X(UNKNOWN15)\
	X(UNKNOWN16) X(UNKNOWN17) X(UNKNOWN18) X(UNKNOWN19)\
	X(UNKNOWN20) X(MRR_DONE) X(UNKNOWN22) X(UNKNOWN23)\
	X(UNKNOWN24) X(UNKNOWN25) X(UNKNOWN26) X(UNKNOWN27)\
	X(UNKNOWN28) X(UNKNOWN29) X(UNKNOWN30) X(UNKNOWN31)
#define DEFINE_PCTL_INTERRUPTS1\
	X(UNKNOWN32) X(SUMMARY)

enum {
#define X(name) PCTL_INT0_##name##_BIT,
	DEFINE_PCTL_INTERRUPTS0
#undef X
	NUM_PCTL_INT0
};
_Static_assert(NUM_PCTL_INT0 == 32, "miscounted");
enum {
#define X(name) PCTL_INT0_##name = (u32)1 << PCTL_INT0_##name##_BIT,
	DEFINE_PCTL_INTERRUPTS0
#undef X
};

enum {
#define X(name) PCTL_INT1_##name##_BIT,
	DEFINE_PCTL_INTERRUPTS1
#undef X
	NUM_PCTL_INT1
};
_Static_assert(NUM_PCTL_INT1 == 2, "miscounted");
enum {
#define X(name) PCTL_INT1_##name = (u32)1 << PCTL_INT1_##name##_BIT,
	DEFINE_PCTL_INTERRUPTS1
#undef X
};

enum {
	PCTL_DRAM_CLASS = 0,
	PCTL_MRR_ERROR_STATUS = 17,
	PCTL_LP_AUTO_ENTRY_EN = 101,
	PCTL_PERIPHERAL_MRR_DATA = 119,
	PCTL_CONTROLLER_BUSY = 200,
	PCTL_INT_STATUS = 203,
	PCTL_INT_ACK = 205,
	PCTL_INT_MASK = 207,
};
/*enum {};
enum {};*/
enum {
	PCTL_LP_CMD = 93,
	PCTL_LP_STATE = 100,
	PCTL_READ_MODEREG = 118,
};

enum {
	PHY_CALVL_VREF_DRIVING_SLICE = 32,
	PHY_SW_MASTER_MODE = 86,
};
#define PHY_SHIFT_CALVL_VREF_DRIVING_SLICE 16
enum {
	PHY_ADR_SW_MASTER_MODE = 35,
};

enum {
	MSCH_DDRCONF = 2,
	MSCH_DDRSIZE = 3,
	MSCH_TIMING1 = 4,
	MSCH_TIMING2 = 5,
	MSCH_TIMING3 = 6,
	MSCH_DEV2DEV = 7,
	MSCH_DDRMODE = 0x110 / 4,
	MSCH_AGING = 0x1000 / 4,
};

#define ACT2ACT(n) (n)
#define RD2MISS(n) ((n) << 8)
#define WR2MISS(n) ((n) << 16)
#define READ_LATENCY(n) ((n) << 24)

#define RD2WR(n) (n)
#define WR2RD(n) ((n) << 8)
#define RRD(n) ((n) << 16)
#define FAW(n) ((n) << 24)

#define BURST_PENALTY(n) (n)
#define WR2MWR(n) ((n) << 8)

#define BUSRD2RD(n) (n)
#define BUSRD2WR(n) ((n) << 4)
#define BUSWR2RD(n) ((n) << 8)
#define BUSWR2WR(n) ((n) << 12)

#define AUTO_PRECHARGE 1
#define BYPASS_FILTERING 2
#define FAW_BANK 4
#define BURST_SIZE(n) ((n) << 3)
#define MWR_SIZE(n) ((n) << 5)
#define FORCE_ORDER(n) ((n) << 8)
#define FORCE_ORDER_STATE(n) ((n) << 16

extern struct dram_cfg init_cfg;
extern const struct phy_update phy_400mhz;
extern const struct phy_update phy_800mhz;

#define DEFINE_CHANNEL_STATES\
	X(UNINIT) X(CONFIGURED)\
	X(INIT) X(CS0_MR5) X(READY) X(SWITCHED)\
	X(CALVL) X(WRLVL) X(GTLVL) X(RDLVL) X(WDQLVL)\
	X(TRAINED)

enum channel_state {
#define X(name) CHAN_ST_##name,
	DEFINE_CHANNEL_STATES
#undef X
	NUM_CHAN_ST
};
extern const char chan_state_names[NUM_CHAN_ST][12];
extern enum channel_state ch_states[2];
extern struct sdram_geometry ch_geo[2];

struct mr_adjustments;
void mr_adjust(volatile u32 *pctl, volatile u32 *pi, const struct mr_adjustments *adj, u32 regset, u32 val);
extern const struct mr_adjustments dq_odt_adj, ca_odt_adj, mr3_adj, mr12_adj, mr14_adj;

struct regshift {u16 reg;u8 shift;};
extern const struct regshift speed_regs[8];
void apply32_multiple(const struct regshift *regs, u8 count, volatile u32 *base, u32 delta, u64 op);
extern const struct odt_preset odt_50mhz, odt_600mhz, odt_933mhz;
void lpddr4_get_odt_settings(struct odt_settings *odt, const struct odt_preset *preset);
void lpddr4_set_odt(volatile u32 *pctl, volatile u32 *pi, u32 freqset, const struct odt_preset *preset);
void lpddr4_modify_config(u32 *pctl, u32 *pi, struct phy_cfg *phy, const struct odt_settings *odt);
void set_drive_strength(volatile u32 *pctl, volatile u32 *phy, const struct phy_layout *layout, const struct odt_settings *odt);
void set_phy_io(volatile u32 *phy, u32 delta, const struct odt_settings *odt);

_Bool train_channel(u32 ch, u32 csmask, volatile u32 *pctl, volatile u32 *pi, volatile struct phy_regs *phy);

#define MIRROR_TEST_ADDR 0x100

_Bool test_mirror(u32 addr, u32 bit);
void channel_post_init(volatile u32 *pctl, volatile u32 *pi, volatile u32 *msch, const struct msch_config *msch_cfg, struct sdram_geometry *geo);
void encode_dram_size(const struct sdram_geometry *geo);

enum {MC_NUM_CHANNELS = 2, MC_CHANNEL_STRIDE = 0x8000, MC_NUM_FREQUENCIES = 3};
HEADER_FUNC volatile struct phy_regs *phy_for(u32 channel) {
	return (volatile struct phy_regs *)(0xffa82000 + MC_CHANNEL_STRIDE * (uintptr_t)channel);
}
HEADER_FUNC volatile u32 *pctl_base_for(u32 channel) {
	return (volatile u32 *)(0xffa80000 + MC_CHANNEL_STRIDE * (uintptr_t)channel);
}
HEADER_FUNC volatile u32 *pi_base_for(u32 channel) {
	return (volatile u32 *)(0xffa80800 + MC_CHANNEL_STRIDE * (uintptr_t)channel);
}
HEADER_FUNC volatile u32 *msch_base_for(u32 channel) {
	return (volatile u32 *)(0xffa84000 + MC_CHANNEL_STRIDE * (uintptr_t)channel);
}
