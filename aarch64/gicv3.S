/* SPDX-License-Identifier: CC0-1.0 */
#include <asm.h>

#define GICR_WAKER 0x14
#define GICR_WAKER_ChildrenAsleep 4
#define GICR_WAKER_ProcessorSleep 2

#define ICC_PMR_EL1 S3_0_C4_C6_0

#define ICC_BPR0_EL1 S3_0_C12_C8_3

#define ICC_BPR1_EL1 S3_0_C12_C12_3
#define ICC_IGRPEN0_EL1 S3_0_C12_C12_6
#define ICC_IGRPEN1_EL1 S3_0_C12_C12_7

#define ICC_CTLR_EL3 S3_6_C12_C12_4
#define ICC_SRE_EL3 S3_6_C12_C12_5
#define ICC_IGRPEN1_EL3 S3_6_C12_C12_7

PROC(gicv3_per_cpu_setup, 2)
	msr DAIFSet, #3
	ldr w1, [x0, #GICR_WAKER]
	tbz w1, #2, call_abort
	and w1, w1, #~GICR_WAKER_ProcessorSleep
	str w1, [x0, #GICR_WAKER]
wait_up:
	yield
	ldr w1, [x0, #GICR_WAKER]
	tbnz w1, #2, wait_up

	mov x1, #1
	msr ICC_SRE_EL3, x1
	mov x2, #0xff
	mov x3, 0xf
	mov x4, #0
	msr ICC_PMR_EL1, x2
	msr ICC_CTLR_EL3, x3
	msr ICC_BPR0_EL1, x4
	msr ICC_BPR1_EL1, x4

	mov w2, #3
	msr ICC_IGRPEN0_EL1, x1
	msr ICC_IGRPEN1_EL3, x2
	msr DAIFClr, #3
	ret
ENDFUNC(gicv3_per_cpu_setup)

PROC(gicv3_per_cpu_teardown, 2)
	msr DAIFSet, #3
	ldr w1, [x0, #GICR_WAKER]
	tbnz w1, #2, call_abort
	orr w1, w1, #GICR_WAKER_ProcessorSleep
	str w1, [x0, #GICR_WAKER]
wait_down:
	yield
	ldr w1, [x0, #GICR_WAKER]
	tbz w1, #2, wait_down

	ret
ENDFUNC(gicv3_per_cpu_teardown)

call_abort:
b abort

