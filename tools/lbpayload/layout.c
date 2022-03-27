// SPDX-License-Identifier: CC0-1.0
#include "lbpayload.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static uint64_t padded_size(const struct segment *s) {
	if (s->alignment > 63) {return ~(uint64_t)0;}
	uint64_t mask = ~(uint64_t)0 << s->alignment;
	return (~mask | s->last) - (mask & s->first);
}

static void sort(void *arr, size_t size, size_t len, int (*cmp)(void *, void *, void*), void *arg) {
	for (size_t frontier = 1; frontier < len; ++frontier) {
		for (size_t i = frontier; i; --i) {
			char *el = (char*)arr + i * size;
			if (cmp(el - size, el, arg) <= 0) {break;}
			for (char *p = el - size; p < el; ++p) {
				char t = *p;
				*p = p[size];
				p[size] = t;
			}
		}
	}
}

static int cmp_segment(void *a_, void *b_, void *segs_) {
	const struct segment *segs = (const struct segment *)segs_;
	const struct segment *a = segs + *(const size_t *)a_;
	const struct segment *b = segs + *(const size_t *)b_;

#define CMP(a, b) if ((a) < (b)) {return -1;} if ((b) < (a)) {return 1;}
	uint64_t asize = padded_size(a), bsize = padded_size(b);
	CMP(bsize, asize)
	CMP(b->alignment, a->alignment)
	CMP(a->first, b->first)
	CMP(b->size, a->size)
	return 0;
}

struct mem_region {
	uint64_t first, last;
	unsigned allocate : 1;
};

static const struct mem_region rk3399_regions[] = {
	{.first = 0, .last = 0x000fffff, .allocate = 0},	/* TZMEM */
	{.first = 0x00100000, .last = 0x03ffffff, .allocate = 1},
	/* dramstage memory */
	{.first = 0x08000000, .last = 0xf7ffffff, .allocate = 1},
	{.first = 0xff3b0000, .last = 0xff3b2000, .allocate = 0},	/* PMUSRAM */
	{.first = 0xff8c0000, .last = 0xff8effff, .allocate = 0},	/* SRAM */
};

struct mem_region_allocations {
	struct mem_region region;
	DECL_VEC(size_t, segments);
};

void layout_segments(struct context *ctx) {
	size_t *order = calloc(ctx->segments_size, sizeof(size_t));
	if (!order) {perror("While sorting the segments"); abort();}
	for (size_t i = 0; i < ctx->segments_size; ++i) {order[i] = i;}
	sort(order, sizeof(size_t), ctx->segments_size, cmp_segment, ctx->segments);
	ctx->processing_order = order;

	DECL_VEC(struct mem_region_allocations, allocs);
	INIT_VEC(allocs);
	for (size_t i = 0; i < sizeof(rk3399_regions)/sizeof(rk3399_regions[0]); ++i) {
		struct mem_region_allocations *alloc = BUMP(allocs);
		alloc->region = rk3399_regions[i];
		INIT_VEC(alloc->segments);
	}

	size_t i_region = 0;
	uint64_t last = 0;
	size_t i_order = 0;
	for (; i_order < ctx->segments_size; ++i_order) {
		size_t i_seg = order[i_order];
		const struct segment *seg = ctx->segments + i_seg;
		if (seg->alignment != SEG_ADDR_FIXED) {break;}
		while (i_region < allocs_size && seg->last > allocs[i_region].region.last) {
			i_region += 1;
		}
		if (i_region >= allocs_size || seg->first < allocs[i_region].region.first) {
			fprintf(stderr, "Segment 0x%"PRIx64"–0x%"PRIx64" touches invalid address ranges\n", seg->first, seg->last);
			exit(3);
		}
		if (i_order && seg->first <= last) {
			fprintf(stderr, "Segment 0x%"PRIx64"–0x%"PRIx64" overlaps other segments\n", seg->first, seg->last);
			exit(3);
		}
		*BUMP(allocs[i_region].segments) = i_seg;
		last = seg->last;
	}

	for (; i_order < ctx->segments_size; ++i_order) {
		size_t i_seg = order[i_order];
		struct segment *seg = ctx->segments + i_seg;
		bool allocated = false;
		for (i_region = 0; i_region < allocs_size; ++i_region) {
			struct mem_region_allocations *alloc = allocs + i_region;
			if (!alloc->region.allocate) {continue;}
			uint64_t first_free = alloc->region.first;
			if (alloc->segments_size) {
				size_t i_seg = alloc->segments[alloc->segments_size - 1];
				const struct segment *seg = ctx->segments + i_seg;
				if (seg->last == alloc->region.last) {continue;}
				first_free = seg->last + 1;
			}
			assert(seg->alignment < 64);
			uint64_t align_mask = ~UINT64_C(0) << seg->alignment;
			uint64_t first_aligned = ((first_free + ~align_mask) & align_mask) | (seg->first & ~align_mask);
			if (first_aligned < first_free) {continue;}
			if (first_aligned > alloc->region.last) {continue;}
			uint64_t delta = first_aligned - seg->first;
			printf("%"PRIx64" %"PRIx64"\n", first_free, first_aligned);
			assert((delta & align_mask) == delta);
			uint64_t last = seg->last + delta;
			if (last < first_aligned) {continue;}
			if (alloc->region.last < last) {continue;}
			seg->first = first_aligned;
			seg->last_init += delta;
			seg->last = last;
			*BUMP(alloc->segments) = i_seg;
			allocated = true;
			break;
		}
		if (!allocated) {
			fprintf(stderr, "could not find a place for segment\n");
			exit(4);
		}
	}

	for (size_t i = 0; i < allocs_size; ++i) {
		printf("Region %"PRIx64"–%"PRIx64"\n", allocs[i].region.first, allocs[i].region.last);
		for (size_t j = 0; j < allocs[i].segments_size; ++j) {
			size_t i_seg = allocs[i].segments[j];
			printf("%2zu: ", i_seg);
			dump_segment(ctx->segments + i_seg);
		}
	}
}
