/* SPDX-License-Identifier: CC0-1.0 */
#pragma once

#define CORTEX_A53_EXIT_RESET 1
#define CORTEX_A53_CPUACTLR_EL1 S3_1_C15_C2_0
#define CORTEX_A53_CPUACTLR_ENDCCASCI (UINT64_C(1) << 44)
#define CORTEX_A53_CPUECTLR_EL1 S3_1_C15_C2_1
#define CORTEX_A53_CPUECTLR_EL1_SMPEN 0x40
#define CORTEX_A53_L2ACTLR_EL1 S3_1_C15_C0_0

#ifdef __ASSEMBLER__
.macro cortex_a53_init clobber_reg, var_rev
	.if \var_rev <= 0x02
		.error "TODO implement workarounds for errata 819472, 824069, 826319, 827319, 836870"
	.endif
	.if \var_rev >= 0x03
		/* erratum 855873 */
		mrs \clobber_reg, CORTEX_A53_CPUACTLR_EL1
		orr \clobber_reg, \clobber_reg, #CORTEX_A53_CPUACTLR_ENDCCASCI
		msr CORTEX_A53_CPUACTLR_EL1, \clobber_reg
	.endif
	.if \var_rev > 0x04
		.error "update errata info for new revision"
	.endif
	/* enter intra-cluster coherency */
	mrs x19, CORTEX_A53_CPUECTLR_EL1
	orr x19, x19, #CORTEX_A53_CPUECTLR_EL1_SMPEN
	msr CORTEX_A53_CPUECTLR_EL1, x19
.endm
#endif
