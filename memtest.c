/* SPDX-License-Identifier: CC0-1.0 */
#include <main.h>
#include <rkpll.h>
#include <rk3399.h>
#include <stage.h>
#include <runqueue.h>
#include <exc_handler.h>

#define DEFINE_VSTACK X(CPU0) X(CPU1)
#define VSTACK_DEPTH 0x1000

#define DEFINE_REGMAP\
	MMIO(GIC500D, gic500d, 0xfee00000, struct gic_distributor)\
	MMIO(GIC500R, gic500r, 0xfef00000, struct gic_redistributor)\
	MMIO(STIMER0, stimer0, 0xff860000, struct rktimer_regs)\
	MMIO(GPIO0, gpio0, 0xff720000, struct rkgpio_regs)\
	MMIO(UART, uart, 0xff1a0000, struct uart)\
	MMIO(CRU, cru, 0xff760000, u32)\
	MMIO(PMU, pmu, 0xff310000, u32)\
	MMIO(PMUCRU, pmucru, 0xff750000, u32)\
	MMIO(PMUGRF, pmugrf, 0xff320000, u32)\
	/* the generic SoC registers are last, because they are referenced often, meaning they get addresses 0xffffxxxx, which can be generated in a single MOVN instruction */
#define DEFINE_REGMAP64K\
	X(PMUSGRF, pmusgrf, 0xff330000, u32)\
	X(GRF, grf, 0xff770000, u32)\

#include <rk3399/vmmap.h>

#define MEMTEST_CHACHAISH
#ifdef MEMTEST_SPLITTABLE
static uint64_t splittable64(uint64_t x)
{
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9;
    x ^= x >> 27;
    x *= 0x94d049bb133111eb;
    x ^= x >> 31;
    return x;
}
__attribute__((optimize("unroll-loops")))
static void write_block(u32 block, u64 salt) {
	volatile u64 *block_ptr = (volatile u64*)(uintptr_t)(block << 27);
	for_range(word, !block, 0x01000000) {
		block_ptr[word] = splittable64(salt ^ (word | block << 24));
	}
}

__attribute__((optimize("unroll-loops")))
static _Bool test_block(u32 block, u64 salt) {
	volatile u64 *block_ptr = (volatile u64*)(uintptr_t)(block << 27);
	for_range(word, !block, 0x01000000) {
		u64 got = block_ptr[word], expected = splittable64(salt ^ (word | block << 24));
		if (unlikely(got != expected)) {
			printf("@%zx: expected %zx, got %zx\n", (u64)&block_ptr[word], expected, got);
			return 0;
		}
	}
	return 1;
}

#elif defined(MEMTEST_SPECK)
struct pair64{u64 a, b;};

static struct pair64 speck_round(struct pair64 x, u64 k) {
	__asm__("ror %0, %0, #8;add %0, %0, %1;eor %0, %0, %2;eor %1, %0, %1, ror #61" : "+r"(x.a), "+r"(x.b) : "r"(k));
	return x;
}

__attribute__((optimize("unroll-loops")))
static void write_block(u32 block, u64 salt) {
	volatile u64 *block_ptr = (volatile u64*)(uintptr_t)(block << 27);
	for(u64 word = !block * 2; word < 0x01000000; word +=2) {
		struct pair64 p = {word, word + 1};
		p = speck_round(speck_round(p, block), salt);
		p = speck_round(speck_round(p, block), salt);
		p = speck_round(speck_round(p, block), salt);
		block_ptr[word] = p.a;
		block_ptr[word + 1] = p.b;
	}
}

__attribute__((optimize("unroll-loops")))
static _Bool test_block(u32 block, u64 salt) {
	volatile u64 *block_ptr = (volatile u64*)(uintptr_t)(block << 27);
	for(u64 word = !block * 2; word < 0x01000000; word +=2) {
		struct pair64 p = {word, word + 1};
		p = speck_round(speck_round(p, block), salt);
		p = speck_round(speck_round(p, block), salt);
		p = speck_round(speck_round(p, block), salt);
		u64 a = block_ptr[word], b = block_ptr[word + 1];
		if (unlikely(a != p.a || b != p.b)) {
			printf("@%zx: expected %016zx, %016zx, got %016zx, %016zx\n", (u64)&block_ptr[word], p.a, p.b, a, b);
			return 0;
		}
	}
	return 1;
}
#elif defined(MEMTEST_CHACHAISH)
#define ROR(a, r) __asm__("ror %0, %0, #" #r : "+r"(a))
#define LADDER(a, b, c, r) b += a; c ^= b; ROR(c, r)
#define QR(a, b, c, d) LADDER(b, a, d, 32); LADDER(d, c, b, 24); LADDER(b, a, d, 16); LADDER(d, c, b, 12); LADDER(b, a, d, 8); LADDER(d, c, b, 7)
#define DR(a0, a1, a2, a3, b0, b1, b2, b3, c0, c1, c2, c3, d0, d1, d2, d3) \
	QR(a0, a1, a2, a3); QR(b0, b1, b2, b3); QR(c0, c1, c2, c3); QR(d0, d1, d2, d3);\
	QR(a0, b1, c2, d3); QR(a1, b2, c3, d0); QR(a2, b3, c0, d1); QR(a3, b0, c1, d2)

static void write_block(u32 block, u64 salt) {
	u32 salt1 = (u32)salt, salt2 = (u32)(salt >> 32);
	volatile u64 *block_ptr = (volatile u64*)(uintptr_t)(block << 27);
	for(u64 word = !block * 16; word < 0x01000000; word +=16) {
		u64 a0 = 0x65787062, a1 = 0x6e642033, a2 = 0x322d6279, a3 = 0x7465206b,
			b0 = 0x65790000, b1 = block, b2 = salt1, b3 = salt2,
			c0 = block, c1 = salt1, c2 = salt2, c3 = block,
			d0 = salt1, d1 = salt2, d2 = block, d3 = salt1;
		DR(a0, a1, a2, a3, b0, b1, b2, b3, c0, c1, c2, c3, d0, d1, d2, d3);
		DR(a0, a1, a2, a3, b0, b1, b2, b3, c0, c1, c2, c3, d0, d1, d2, d3);
#define STORE(x, i) block_ptr[word + 0 + i] = x##0;\
		block_ptr[word + 1 + i] = x##1;\
		block_ptr[word + 2 + i] = x##2;\
		block_ptr[word + 3 + i] = x##3
		STORE(a, 0);
		STORE(b, 4);
		STORE(c, 8);
		STORE(d, 12);
	}
}

static _Bool test_block(u32 block, u64 salt) {
	u32 salt1 = (u32)salt, salt2 = (u32)(salt >> 32);
	volatile u64 *block_ptr = (volatile u64*)(uintptr_t)(block << 27);
	u64 addr, x0, x1, x2, x3, e0, e1, e2, e3;
	for(u64 word = !block * 16; word < 0x01000000; word += 16) {
		u64 a0 = 0x65787062, a1 = 0x6e642033, a2 = 0x322d6279, a3 = 0x7465206b,
			b0 = 0x65790000, b1 = block, b2 = salt1, b3 = salt2,
			c0 = block, c1 = salt1, c2 = salt2, c3 = block,
			d0 = salt1, d1 = salt2, d2 = block, d3 = salt1;
		DR(a0, a1, a2, a3, b0, b1, b2, b3, c0, c1, c2, c3, d0, d1, d2, d3);
		DR(a0, a1, a2, a3, b0, b1, b2, b3, c0, c1, c2, c3, d0, d1, d2, d3);
#define LOAD(e, i) x##e = block_ptr[word + e + i];
#define CHECK(e, i) do{got = block_ptr[word + i], expected = e;\
		if (unlikely(got != expected)) {addr = (u64)&block_ptr[word + i]; goto error;}\
	}while(0)
#define CHECK_ROW(x, i) LOAD(0, i); LOAD(1, i); LOAD(2, i); LOAD(3, i);\
		e0 = x##0; e1 = x##1; e2 = x##2; e3 = x##3;\
		if (unlikely(e0 != x0 || e1 != x1 || e2 != x2 || e3 != x3)) {\
			addr = (u64)&block_ptr[word + i]; goto error;\
		}
		CHECK_ROW(a, 0);
		CHECK_ROW(b, 4);
		CHECK_ROW(c, 8);
		CHECK_ROW(d, 12);
	}
	return 1;
error:
	printf("@%zx: expected %zx %zx %zx %zx, got %zx %zx %zx %zx\n", addr, x0, x1, x2, x3, e0, e1, e2, e3);
	return 0;
}
#else
#error "no PRNG selected"
#endif

static void timed_flush() {
#ifndef UNCACHED_MEMTEST
	u64 start_flush = get_timestamp();
	flush_dcache();
	printf("flush took %zu μs … ", (get_timestamp() - start_flush)/CYCLES_PER_MICROSECOND);
#endif
}

static struct sched_runqueue runqueue = {};
struct sched_runqueue *get_runqueue() {return &runqueue;}
static u64 _Alignas(4096) UNINITIALIZED pagetable_frames[11][512];
u64 (*const pagetables)[512] = pagetable_frames;
const size_t num_pagetables = ARRAY_SIZE(pagetable_frames);

static _Bool memtest(u64 salt) {
	_Bool res = 1;
	for_range(block, 0, 31) {
		u64 block_start = block * 0x08000000;
		log("testing %08zx–%08zx … ", block_start, block_start + 0x07ffffff);
		write_block(block, salt);
		timed_flush();
		if (test_block(block, salt)) {
			puts("good\n");
		} else {
			puts("FAILED\n");
			res = 0;
		}
	}
	timed_flush();
	puts("\n");
	for_range(block, 0, 31) {
		u64 block_start = block * 0x08000000;
		log("retention testing %08zx–%08zx … ", block_start, block_start + 0x07ffffff);
		if (test_block(block, salt)) {
			puts("good\n");
		} else {
			puts("FAILED\n");
			res = 0;
		}
	}
	return res;
}


static UNINITIALIZED _Alignas(4096) u8 vstack_frames[NUM_VSTACK][VSTACK_DEPTH];
void *const boot_stack_end = (void*)VSTACK_BASE(VSTACK_CPU0);

volatile struct uart *const console_uart = regmap_uart;

const struct mmu_multimap initial_mappings[] = {
#include <rk3399/base_mappings.inc.c>
#ifdef UNCACHED_MEMTEST
	{.addr = 0, .desc = MMU_MAPPING(DEV_GRE, 0)},
#else
	{.addr = 0, .desc = MMU_MAPPING(NORMAL, 0)},
#endif
	{.addr = 0xf8000000, .desc = 0},
	VSTACK_MULTIMAP(CPU0),
	VSTACK_MULTIMAP(CPU1),
	{}
};

static void sync_exc_handler(struct exc_state_save UNUSED *save) {
	u64 elr, esr, far;
	__asm__("mrs %0, esr_el3; mrs %1, far_el3; mrs %2, elr_el3" : "=r"(esr), "=r"(far), "=r"(elr));
	die("sync exc@0x%"PRIx64": ESR_EL3=0x%"PRIx64", FAR_EL3=0x%"PRIx64"\n", elr, esr, far);
}

_Noreturn void main() {
	sync_exc_handler_spx = sync_exc_handler_sp0 = sync_exc_handler;
	puts("memtest\n");

	u64 round = 0, failed_rounds = 0;
	while (1) {
		if (!failed_rounds) {
			printf("\nround %zu, all green so far\n", round);
		} else {
			printf("\nround %zu, %zu failed so far\n", round, failed_rounds);
		}
		failed_rounds += !memtest(round++ << 29);
	}
}
