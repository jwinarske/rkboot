#pragma once
#include <types.h>

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
	u32 dslice[4][NUM_PHY_DSLICE_REGS];
	u32 aslice[3][NUM_PHY_ASLICE_REGS];
	u32 global[NUM_PHY_GLOBAL_REGS];
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
struct channel_config {
	_Bool pwrup_sref_exit;
	u8 ddrconfig, csmask;
	u8 bw, col, bk;
	u8 row[2];
	u32 timing1, timing2, timing3;
	u32 dev2dev;
	u32 ddrmode;
	u32 aging;
};
struct dram_cfg {
	u32 mhz;
	enum dramtype type;
	struct channel_config channels[2];
	struct dram_regs_cfg regs;
};

enum {
	PCTL_DRAM_CLASS = 0,
	PCTL_MRR_ERROR_STATUS = 17,
	PCTL_LP_AUTO_ENTRY_EN = 101,
	PCTL_PERIPHERAL_MRR_DATA = 119,
	PCTL_CONTROLLER_BUSY = 200,
	PCTL_INT_STATUS = 203,
	PCTL_INT_ACK = 205
};
/*enum {};
enum {};*/
enum {
	PCTL_LP_CMD = 93,
	PCTL_LP_STATE = 100,
	PCTL_READ_MODEREG = 118,
};

enum {
	PHY_SW_MASTER_MODE = 86,
};
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

extern const struct odt_preset odt_50mhz;
void lpddr4_get_odt_settings(struct odt_settings *odt, const struct odt_preset *preset);
void lpddr4_modify_config(struct dram_cfg *cfg, const struct odt_settings *odt);
void set_drive_strength(volatile u32 *pctl, volatile u32 *phy, const struct phy_layout *layout, const struct odt_settings *odt);
void set_phy_io(volatile u32 *phy, const struct phy_layout *layout, const struct odt_settings *odt);
