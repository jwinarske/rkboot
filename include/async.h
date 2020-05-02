/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

struct async_transfer {
	u8 *buf;
	_Atomic(size_t) pos;
	size_t total_bytes;
};
