// SPDX-License-Identifier: CC0-1.0
#include <stddef.h>
#include <stdint.h>

#define DEFINE_PCTL(B, H, W)\
	W(24, tInit) W(24, tInit3) W(24, tInit4) W(24, tInit5)\
	B(5, DFIBus_freq)\
	B(8, rRRD) B(8, tRC) B(8, tRP) B(8, tFAW)\
	B(8, tMRD_PCTL) W(17, tRAS_max) B(8, tCKE) B(8, tCKESR)\
	B(8, tDAL)\
	H(16, tPDEX)\
	H(16, tXPDLL)\
	B(4, tCSCKE) B(4, tCKELCS) B(4, tCKEHCS) B(5, tMRWCKEL) B(4, tZQCKE)\
	H(16, tXSR) H(16, tXSNR)\
	B(4, tCKELCMD) B(4, tCKEHCMD) B(4, tCKCKEL) B(8, tSR) B(3, tESCKE) B(4, tCKELPD) B(4, tCSCKEH) B(4, tCMDCKE)\
	B(8, CkSRE) B(8, CkSRX)\
	W(32, DFS_phy_reg_write_data)\
	H(10, tVRCG_enable) H(10, tVRCG_disable) B(5, tCKFSPE) B(5, tCKFSPX) H(16, tVRef_long)\
	H(12, tZQCal) B(6, tZQLat)\
	H(12, ZQReset)\
	B(5, r2w_DiffCS_dly) B(5, w2r_DiffCS_dly) B(5, w2w_DiffCS_dly)\
	B(5, r2w_SameCS_dly)\
	B(4, tDQSCK_min)\
	B(8, tDFI_phy_RdLat)\
	B(8, tDFI_RdCSLat)\

#define DEFINE_PCTL_PI(B, H, W)\
	B(7, CASLat_lin) B(5, WrLat)\
	B(8, tRAS_min) B(8, tWTR)\
	B(8, tRTP) B(8, tMOD)\
	B(8, tRCD) B(8, tWR)\
	B(5, tMRZ)\
	B(8, tRP_AB)\
	H(10, tRFC) H(16, tREF)\
	B(8, tMRRI)\
	H(16, tDFI_PhyMstr_max)\
	H(16, tDFI_PhyMstr_resp)\
	H(10, tFC)\
	B(8, tODTL_2CMD)\
	B(6, wr_to_ODTH)\
	B(6, rd_to_ODTH)\
	B(4, tDQSCK_max)\
	H(16, tDFI_CtrlUpd_max) H(16, tDFI_PhyUpd_resp)\
	W(32, tDFI_CtrlUpd_interval)\
	B(8, RdLat_adj) B(8, WrLat_adj) /* TODO: TF-A uses different values between PCTL and PI*/\
	H(10, tDFI_CALvl_CC) H(10, tDFI_CALvl_capture)\
	B(8, tDFI_WrCSLat)

#define DEFINE_PI(B, H, W)\
	B(8, tDelay_Rd_Wr_2_bus_idle)\
	H(14, tCAEnt)\
	H(10, tVRef_short) H(10, tVRef_long_PI)\
	B(4, tDFI_CALvl_strobe)\
	B(6, tCKEHDQS)\
	W(17, tRAS_max_PI) B(6, tMRD) B(8, tMRW)

#define DEFINE_PERCS(B, H, W)\
	H(16, MR0_data) H(16, MR1_data) H(16, MR2_data)\
	H(16, MR3_data)\
	B(8, MR11_data)\
	B(8, MR22_data)

#define DEFINE_PHY(B, H, W)\
	B(1, per_CS_training_en)\
	H(10, RdDQS_gate_slave_delay)\
	B(4, RdDQS_latency_adjust)\
	B(4, RdData_en_dly) B(4, RdData_en_TSel_dly)\
	B(4, SW_master_mode)\
	B(4, rptr_update)\
	H(13, PLL_ctrl) H(13, PLL_ctrl_CA)\
	B(1, low_freq_sel)\
	B(12, pad_VRef_ctrl_DQ)\
	H(13, LP4_boot_PLL_ctrl) H(13, LP4_boot_PLL_ctrl_CA)\
	B(2, speed)\
	B(1, TSel_rd_en) B(8, TSel_DQ_rd) B(8, TSel_DQ_wr) B(8, TSel_CA) B(8, TSel_CKCS)\
	B(3, drive_mode)\

#define W(bits, name) uint32_t name;
#define H(bits, name) uint16_t name;
#define B(bits, name) uint8_t name;
struct rk3399_dram_timings {
	DEFINE_PCTL(B, H, W)
	DEFINE_PCTL_PI(B, H, W)
	DEFINE_PI(B, H, W)
	DEFINE_PHY(B, H, W)
};
struct rk3399_dram_timings_percs {
	DEFINE_PERCS(B, H, W)
};
#undef W
#undef H
#undef B

struct lpddr4_spec_timing {
	uint16_t mhz;
	uint8_t mrval;
	uint8_t RL_nDBI, RL_DBI;
	uint8_t WL_A, WL_B;
	uint8_t nWR;
};

const struct lpddr4_spec_timing lpddr4_timings[] = {
	{266, 0,	 6,  6,	4,  4,	6},
	{533, 1,	10, 12,	6,  8,	10},
	{800, 2,	14, 16,	8, 12,	16},
};

struct odt_setting {
	uint16_t mhz;
	uint32_t mr3_11_12_14;
	uint32_t TSel_DQ;
	uint16_t TSel_CA_CKCS;
	uint8_t vref_percent;
};

// 0x0E01we0r, 0xXeYf

const struct odt_setting lpddr4_odt_settings[] = {
	{50,   0x31007272, 0x0001ee00, 0xeeef, 41},
	{600,  0x31017272, 0x0001de00, 0xeeef, 32},
	{800,  0x09017272, 0x0101de0e, 0xeeef, 17},
	{933,  0x31037259, 0x0001de00, 0xeeef, 32},
	{1066, 0x09067210, 0x0101ce0e, 0xeeef, 17}
};

const struct lpddr4_spec_timing *lpddr4_get_timing(uint32_t mhz) {
	for (size_t i = 0; i < sizeof(lpddr4_timings) / sizeof(lpddr4_timings[0]); ++i) {
		if (lpddr4_timings[i].mhz >= mhz)  {return lpddr4_timings + i;}
	}
	return 0;
}

static const _Bool legacy = 1;

_Bool calcTiming(struct rk3399_dram_timings *x, uint32_t mhz, uint32_t die_cap_mbit) {
#define NS(i) (((i) * mhz + 999) / 1000)
#define NS_2(i) (((i) * mhz + 1999) / 2000)
#define NS_4(i) (((i) * mhz + 3999) / 4000)
#define MIN_tCK(n, i) if (x->n < (i)) {x->n = (i);}
	// the standard says it's 3904ns, but the RK3399 uses 3900ns - 8tCK
	// in most places
	uint32_t tREFI = legacy ? NS(3900) - 8 : NS(3904);
	const struct lpddr4_spec_timing *spec = lpddr4_get_timing(mhz);
	if (!spec) {return 0;}

	// === Power sequence
	// tINIT1: reset time (includes tINIT2)
	x->tInit = 200 * mhz;
	// tINIT3: init time
	x->tInit3 = 2000 * mhz;
	// tINIT4: clock active before CKE high
	x->tInit4 = 5;
	// tINIT5: MRR/MRW after CKE high
	x->tInit5 = 2 * mhz;

	// === CAS
	x->CASLat_lin = 2 * spec->RL_nDBI;
	x->WrLat = spec->WL_A;
	if (legacy) {
		x->tWTR = 10;
	} else {
		// tWTR: Write To Read
		x->tWTR = NS(10); MIN_tCK(tWTR, 8)
	}

	// apparently these should be rounded outward instead of inward
	// tDQSCK: something about read strobe, min 1.5 ns, max 3.5 ns
	if (legacy) {
		x->tDQSCK_min = 0;
	} else {
		x->tDQSCK_min = 3 * mhz / 2000;
	}
	x->tDQSCK_max = NS_2(7);

	// === RAS
	// tRAS min: ACT latency
	x->tRAS_min = NS(42); MIN_tCK(tRAS_min, 3)
	// tRAS max: max active time
	// this field is only 17 bits wide, so this overflows at a bit over 1800 MHz
	// (though RK only seem to support LPDDR4 up to 800 MHz anyway)
	// standard specifies this as min(9*tREFI*refresh rate, 70.2 μs)
	uint32_t tRAS_max = 351 * mhz / 5;
	if (tRAS_max > 0x1ffff) {return 0;}
	x->tRAS_max = tRAS_max;
	if (legacy) {// the param blobs do this:
		x->tRAS_max_PI = mhz == 800 ? 0xd92e : mhz == 400 ? 0x6c97 : 0xd92;
	} else if (0) {	// this happens to match:
		x->tRAS_max_PI = 69498 * mhz / 1000;
	} else {	// but by spec it is 70200ns:
		x->tRAS_max_PI = x->tRAS_max;
	}

	if (legacy) {
		x->tRP = NS(18); MIN_tCK(tRP, 4)
		x->tRP_AB = NS(21); MIN_tCK(tRP_AB, 4)
	} else {
		// tRP: Row Precharge (single bank)
		x->tRP = NS(18); MIN_tCK(tRP, 3)
		// tRP_AB: Row Precharge (all banks)
		x->tRP_AB = NS(21); MIN_tCK(tRP_AB, 3)
	}

	// tRRD: RAS-RAS delay (different bank)
	x->rRRD = NS(10); MIN_tCK(rRRD, 4)
	if (legacy) {
		x->tRC = (mhz == 800 ? 49 : mhz == 400 ? 25 : 7);
	} else {
		// tRC: (Refresh cycle): ACT-RP
		x->tRC = x->tRAS_min + x->tRP_AB;
	}
	// tFAW: sliding time window in during which only 4 banks can be activated at once
	x->tFAW = NS(40);

	// tRTP: Read To Precharge
	x->tRTP = NS(10); MIN_tCK(tRTP, 8)

	if (legacy) {
		x->tWR = mhz == 800 ? 17 : mhz == 400 ? 10 : 4;
	} else {
		// tWR: Write Recovery (write-to-precharge)
		x->tWR = spec->nWR;
	}

	// tRCD: RAS-CAS delay
	x->tRCD = NS(18); MIN_tCK(tRCD, 4)

	if (legacy) {
		x->tDAL = mhz == 800 ? 32 : mhz == 400 ? 18 : 8;
	} else {
		// tDAL does not exist in LPDDR4, this is the DDR3 definition
		x->tDAL = x->tRP_AB + spec->nWR;
	}

	// === refresh intervals
	uint32_t tRFC_ns = 180;
	if (!legacy && die_cap_mbit < 3000) {tRFC_ns = 130;}
	if (!legacy && die_cap_mbit >= 6000) {tRFC_ns = 280;}
	x->tRFC = NS(tRFC_ns);
	x->tREF = tREFI;

	// === Mode registers
	// tMRW: duration of Mode Register Write commands
	// referenced but not defined in the LPDDR4 spec. lovely.
	x->tMRW = NS(10);
	// tMRD: Mode Register (write) Delay to next command
	x->tMRD = NS(14); MIN_tCK(tMRD, 10)
	// tMOD does not exist in LPDDR4 spec, the param blobs set it to 10
	x->tMOD = 10;
	// TODO: TF-A asserts that tMRW = tMRD, but IIRC a datasheet
	// contradicted this, and the PCTL uses the max(10ns, 10) value
	// for "tMRD". Meanwhile "tMRW" in the PI is just 10ns without clamp.
	// It's a mess.
	uint32_t tMRW = NS(10); if (tMRW < 10) {tMRW = 10;}
	if (legacy) {	// this seems to actually be tMRW
		x->tMRD_PCTL = tMRW;
	} else {
		x->tMRD_PCTL = x->tMRD;
	}

	// tMRRI does not exist in LPDDR4
	if (legacy) {// the parameter blobs do this:
		x->tMRRI = 0;
	} else {// TF-A does this:
		x->tMRRI = x->tRCD + 3;
	}

	// === power down
	// tCKE: min time between CKE transitions (posedge *and* negedge)
	x->tCKE = NS_2(15); MIN_tCK(tCKE, 4)
	// tCKESR does not exist in LPDDR4, the param blobs set it to tCKE
	x->tCKESR = x->tCKE;
	// tCSCKE: CS valid time before CKE low
	// tCSCKEH: CS valid time before CKE high
	x->tCSCKE = x->tCSCKEH = NS_4(7);
	if (legacy) {
		x->tCKELCS = x->tCKEHCS = 0;
	} else {
		// tCKELCS: CKE low before CS invalid
		x->tCKELCS = NS(5); MIN_tCK(tCKELCS, 5)
		// tCKEHCS: CKE high before CS invalid
		x->tCKEHCS = NS_2(15); MIN_tCK(tCKEHCS, 5)
	}

	// tMRWCKEL: min time from MRW to CKE low
	x->tMRWCKEL = NS(14); MIN_tCK(tMRWCKEL, 10)
	// tZQCKE: min time from ZQ calibration to CKE low
	x->tZQCKE = NS_4(7); MIN_tCK(tZQCKE, 3)
	// tCMDCKE: valid command to CKE low
	x->tCMDCKE = NS_4(7); MIN_tCK(tCMDCKE, 3)

	// tXP: CKE high before next command
	x->tPDEX = NS_2(15); MIN_tCK(tPDEX, 5)
	// tXPDLL does not exist in LPDDR4, this is the DDR3 definition
	x->tXPDLL = NS(24); MIN_tCK(tXPDLL, 10)

	// === self-refresh
	// tSR: min SRE to SRX
	x->tSR = NS(15); MIN_tCK(tSR, 3)

	if (legacy) {// TF-A does this:
		x->tESCKE = NS_4(7); MIN_tCK(tESCKE, 3)
	} else {// the spec says this:
		// tESCKE: SRE to CKE low
		x->tESCKE = 2;
	}

	if (legacy) {// param blobs do this:
		x->tXSR = (mhz == 800 ? 6 : 5);
		// this matches the spec value for the smallest tRFC value
		x->tXSNR = NS_2(275); MIN_tCK(tXSNR, 2)
	} else {// spec says this:
		// tXSR / tXSV: exit self refresh before valid command
		x->tXSR = NS_2(2 * tRFC_ns + 15); MIN_tCK(tXSR, 2)
		// tXSNR does not exist in LPDDR4, TF-A sets it to the same value
		x->tXSNR = x->tXSR;
	}

	// tCKELPD: min time between CKE transitions during self-refresh
	// see tCKE for outside SR
	x->tCKELPD = NS_2(15); MIN_tCK(tCKELPD, 3)
	if (legacy) {// param blobs do this:
		x->tCKELCMD = 5;
		x->tCKEHCMD = mhz == 800 ? 6 : 5;
		x->tCKCKEL = 5;
	} else {// spec and TF-A say this:
		// tCKELCMD: valid command after CKE low
		x->tCKELCMD = NS_2(15); MIN_tCK(tCKELCMD, 3)
		// tCKEHCMD: valid command after CKE high
		// tCKCKEL: valid clock after CKE low
		x->tCKEHCMD = x->tCKCKEL = x->tCKEHCMD;
	}

	// tCKSRE/tCKSRX do not exist in LPDDR4
	// this matches tCKCKEL, which seems like a decent equivalent
	x->CkSRE = NS_2(15); MIN_tCK(CkSRE, 3)
	// TF-A doesn't initialize this field
	// this matches tCKELCK, which seems like a decent equivalent
	x->CkSRX = NS(5); MIN_tCK(CkSRX, 5)

	// === CA training
	// tCAENT: CA training command after CKE low
	x->tCAEnt = NS(250);
	// tMRZ: during CA training, CKE high to DQ tri-state
	x->tMRZ = NS_2(3);
	// tVREF_LONG: max 250ns
	x->tVRef_long = mhz / 4;
	if (legacy) {// the param blobs do this:
		x->tVRef_short = NS(250) + 1;
		x->tVRef_long_PI = NS(250) + 1;
	} else if (0) {// TF-A does this:
		x->tVRef_short = NS(100);
		x->tVRef_long_PI = x->tVRef_long;
	} else {// by spec they should be:
		// tVREF_SHORT: max 80ns
		x->tVRef_short = mhz * 2 / 25;
		x->tVRef_long_PI = x->tVRef_long;
	}
	// tCKEHDQS: CKE high to DQS high-impedance
	// by spec this should be just 10ns, but TF-A does this adjustment too
	x->tCKEHDQS = NS(10) + (mhz > 100 ? 8 : 1);

	// === frequency change
	// tFC: frequency set point change MRW to next command
	x->tFC = NS(250);
	// tCKFSPE: clock at old freq after freq change MRW
	// tCKFSPX: clock at new freq before tFC elapses
	x->tCKFSPE = NS_2(15); MIN_tCK(tCKFSPE, 4)
	x->tCKFSPX = x->tCKFSPE;

	// === VRef current generator
	x->tVRCG_enable = NS(200);
	x->tVRCG_disable = NS(100);

	// === ODT timings
	if (legacy) {// the param blobs do this:
		x->tODTL_2CMD = 0;
	} else if (0) {// TF-A does this:
		x->tODTL_2CMD = NS_2(7);	// 3.5 ns (rounded up)
	} else {// based on reading the spec, I assume it should be:
		// tODTon/off, max
		x->tODTL_2CMD = 7 * mhz / 2000; // 3.5 ns (rounded down)
		// it might also be the table-based value of ODTLon/off
	}

	// === ZQ calibration
	// tZQCAL: calibration time (=1 μs)
	x->tZQCal = mhz;
	// tZQLAT: calibration latch time
	x->tZQLat = NS(30); MIN_tCK(tZQLat, 8)
	// tZQRESET: calibration reset time (MR10[0] = 1)
	x->ZQReset = NS(50); MIN_tCK(ZQReset, 3)

	// === DFI timings
	x->tDFI_PhyMstr_max = 4 * tREFI;
	x->tDFI_PhyMstr_resp = 2 * tREFI;
	x->tDFI_CtrlUpd_max = 2 * tREFI;
	x->tDFI_PhyUpd_resp = 2 * tREFI;
	x->tDFI_CtrlUpd_interval = 20 * tREFI;

	if (legacy) {
		x->tDFI_phy_RdLat = mhz == 800 ? 26 : mhz == 400 ? 22 : 18;
	} else {
		return 0;
	}

	x->tDFI_CALvl_strobe = NS(2) + 5;
	x->tDFI_CALvl_CC = (10 * mhz + 23500 + 999) / 1000;
	if (legacy) {
		x->tDFI_CALvl_capture = (10 * mhz + 5500 + 999) / 1000;
	} else {
		return 0;
	}

	if (legacy) {
		x->tDFI_RdCSLat = mhz == 800 ? 9 : mhz == 400 ? 5 : 1;
		x->tDFI_WrCSLat = mhz == 800 ? 0 : mhz == 400 ? 3 : 1;
	} else {
		return 0;
	}

	// === non-specced delays
	if (legacy) {
		x->wr_to_ODTH = mhz == 800 ? 5 : mhz == 400 ? 4 : 3;
		x->rd_to_ODTH = mhz == 800 ? 10 : mhz == 400 ? 7 : 4;
		x->r2w_DiffCS_dly = mhz == 800 ? 11 : mhz == 400 ? 10 : 8;
		x->w2r_DiffCS_dly = mhz == 800 ? 1 : 0;
		x->w2w_DiffCS_dly = mhz == 800 ? 15 : mhz == 400 ? 14 : 13;
		x->r2w_SameCS_dly = mhz == 800 ? 11 : mhz == 400 ? 10 : 8;
		x->RdLat_adj = mhz == 800 ? 12 : mhz == 400 ? 9 : 5;
		x->WrLat_adj = mhz == 800 ? 6 : mhz == 400 ? 4 : 2;
		x->tDelay_Rd_Wr_2_bus_idle = mhz == 800 ? 50 : mhz == 400 ? 43 : 35;
	} else {
		return 0;
	}

	// === PHY ===
	// === AdrCtl
	// these PLL control registers are a mystery.
	// Looking at TF-A they seem to have fields beginning at bits 1, 5 and 7,
	// with the one at 7 being at least 2 bits long, but we don't know what they
	// do.
	if (legacy) {
		x->PLL_ctrl = mhz == 800 ? 0x1102 : mhz == 400 ? 0x1302 : 0x1902;
		x->LP4_boot_PLL_ctrl = 0x1902;
		x->PLL_ctrl_CA = mhz == 800 ? 0x122 : mhz == 400 ? 0x322 : 0x922;
		x->LP4_boot_PLL_ctrl_CA = 0x922;
	} else {	// TF-A does this:
		x->PLL_ctrl = x->LP4_boot_PLL_ctrl = 0x1102;
		x->PLL_ctrl_CA = x->LP4_boot_PLL_ctrl_CA = 0x122;
	}
	x->low_freq_sel = mhz <= 400 ? 1 : 0;
	// each nibble in the TSel bytes selects a termination setting
	// (upper nibble: N, lower nibble: P): 0 = Hi-Z, 1 = 240 Ω,
	// 8 = 120 Ω, 9 = 80 Ω, 12-15: 240 / (n - 8) Ω
	x->TSel_CA = x->TSel_CKCS = 0xef;
	x->TSel_DQ_rd = mhz <= 600 ? 0 : 0x09;
	x->TSel_rd_en = mhz <= 600 ? 0 : 1;
	x->TSel_DQ_wr = mhz <= 50 ? 0xef : 0xdf;

	// TF-A doesn't set these, but U-Boot does
	x->speed = mhz >= 800 ? 2 : mhz >= 400 ? 1 : 0;
	return 1;
}

_Bool calcTimingPerCS(struct rk3399_dram_timings_percs *x, uint32_t mhz, unsigned cs) {
	const struct lpddr4_spec_timing *spec = lpddr4_get_timing(mhz);
	if (!spec) {return 0;}
	if (legacy) {
		x->MR0_data = mhz == 800 ? 0xe3 : mhz == 400 ? 0xa3 : 0x63;
	} else {
		return 0;
	}
	// bits 0-1: burst length = 0
	// bit 2: write preamble length = 1
	// bit 3: read preamble type = 0
	// bits 4-6: nWR encoding
	// bit 7: read postamble length = 0
	x->MR1_data = 4 | spec->mrval << 4;
	// bits 0-2: read latency encoding
	// bits 3-5: write latency encoding
	// bit 6: write latency set = 0 (A)
	// bit 7: write leveling = 0
	x->MR2_data = spec->mrval | spec->mrval << 3;

	// bit 0: pull-up calibration = 1
	// bit 1: write postamble length = 0
	// bit 2: post-package repair protection = 0
	// bits 3-5: pull-down drive strength (PDDS)
	// bit 6: DBI read
	// bit 7: DBI write
	x->MR3_data = 1 | (mhz > 600 ? 3 : 6) << 3;

	// bits 0-2: DQ ODT
	// bits 4-6: CA ODT = 0
	// bits 3, 7: reserved
	x->MR11_data = mhz > 50 ? 1 : 0;

	// bits 0-2: SOC ODT
	// bit 3: ODT enable CK
	// bit 4: ODT enable CS
	// bit 5: ODT enable CA = 0
	// bits 6-7: reserved
	x->MR22_data = mhz > 600 ? 3 : 0;
	if (cs == 1) {x->MR22_data |= 0x18;}
	return 1;
}
