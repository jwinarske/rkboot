#include <defs.h>

static uint64_t splittable64(uint64_t x)
{
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9;
    x ^= x >> 27;
    x *= 0x94d049bb133111eb;
    x ^= x >> 31;
    return x;
}

static _Bool memtest(u64 salt) {
	_Bool res = 1;
	for_range(block, 0, 31) {
		u64 block_start = block * 0x08000000;
		printf("testing %08zx–%08zx … ", block_start, block_start + 0x07ffffff);
		volatile u64 *block_ptr = (volatile u64*)block_start;
		for_range(word, !block, 0x01000000) {
			block_ptr[word] = splittable64(salt ^ (word | block << 24));
		}
		for_range(word, !block, 0x01000000) {
			u64 got = block_ptr[word], expected = splittable64(salt ^ (word | block << 24));
			if (unlikely(got != expected)) {
				printf("@%zx: expected %zx, got %zx\n", (u64)&block_ptr[word], expected, got);
				break;
			}
		}
		puts("\n");
	}
	return res;
}

_Noreturn void memtest_main() {
	u64 round = 0;
	while (1) {
		memtest(round++ << 29);
	}
}
