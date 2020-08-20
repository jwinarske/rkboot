/* SPDX-License-Identifier: CC0-1.0 */
#include <main.h>
#include <rkpll.h>
#include <rk3399.h>
#include <stage.h>
#include <uart.h>
#include <runqueue.h>

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

UNINITIALIZED _Alignas(16) u8 exc_stack[4096] = {};
static struct sched_runqueue runqueue = {};
struct sched_runqueue *get_runqueue() {return &runqueue;}
static u64 _Alignas(4096) UNINITIALIZED pagetable_frames[11][512];
u64 (*const pagetables)[512] = pagetable_frames;
const size_t num_pagetables = ARRAY_SIZE(pagetable_frames);

_Bool memtest(u64 salt) {
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

static const struct mapping initial_mappings[] = {
#ifdef UNCACHED_MEMTEST
	{.first = 0, .last = 0xf7ffffff, .flags = MEM_TYPE_DEV_GRE},
#else
	{.first = 0, .last = 0xf7ffffff, .flags = MEM_TYPE_NORMAL},
#endif
	{.first = (u64)uart, .last = (u64)uart + 0xfff, .flags = MEM_TYPE_DEV_nGnRnE},
	MAPPING_BINARY_SRAM,
	{.first = 0xff8c0000, .last = 0xff8c1fff, .flags = MEM_TYPE_NORMAL}, /* stack */
	{.first = 0xff760000, .last = 0xff77ffff, .flags = MEM_TYPE_DEV_nGnRnE}, /* CRU, GRF */
	{.first = 0, .last = 0, .flags = 0}
};

static const struct address_range critical_ranges[] = {
	{.first = __start__, .last = __end__ - 1},
	{.first = uart, .last = uart},
	ADDRESS_RANGE_INVALID
};

_Noreturn void main(u64 sctlr) {
	puts("memtest\n");
	struct stage_store store;
	store.sctlr = sctlr;
	stage_setup(&store);
	mmu_setup(initial_mappings, critical_ranges);
	u64 round = 0, failed_rounds = 0;
	while (1) {
		printf("\nround %zu, %zu failed so far\n", round, failed_rounds);
		failed_rounds += !memtest(round++ << 29);
	}
}
