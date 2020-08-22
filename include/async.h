/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

struct async_buf {u8 *start, *end;};

struct async_transfer {
	struct async_buf (*pump)(struct async_transfer *async, size_t consume, size_t min_size);
};

struct async_dummy {
	struct async_transfer async;
	struct async_buf buf;
};

HEADER_FUNC struct async_buf async_pump_dummy(struct async_transfer *async_, size_t consume, size_t UNUSED min_size)  {
	struct async_dummy *async = (struct async_dummy *)async_;
	async->buf.start += consume;
	return async->buf;
}

struct async_blockdev {
	struct async_transfer async;
	u32 block_size;
	u64 num_blocks;
	enum iost (*start)(struct async_blockdev *, u64 addr, u8 *buf, u8 *buf_end);
};
