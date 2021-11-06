// SPDX-License-Identifier: CC0-1.0
#pragma once
#include <stdatomic.h>
#include <defs.h>

// the actual definitions just below seem to confuse clangd
HEADER_FUNC u16 le16(u16);
HEADER_FUNC u32 le32(u32);
HEADER_FUNC u64 le64(u64);
HEADER_FUNC u16 be16(u16);
HEADER_FUNC u32 be32(u32);
HEADER_FUNC u64 be64(u64);

#define DEFINE_SWAPPED(endian) \
HEADER_FUNC u16 endian##16(u16 v) {return __builtin_bswap16(v);}\
HEADER_FUNC u32 endian##32(u32 v) {return __builtin_bswap32(v);}\
HEADER_FUNC u64 endian##64(u64 v) {return __builtin_bswap64(v);}
#define DEFINE_UNSWAPPED(endian) \
HEADER_FUNC u16 endian##16(u16 v) {return v;}\
HEADER_FUNC u32 endian##32(u32 v) {return v;}\
HEADER_FUNC u64 endian##64(u64 v) {return v;}

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
DEFINE_UNSWAPPED(le)
DEFINE_SWAPPED(be)
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
DEFINE_UNSWAPPED(be)
DEFINE_SWAPPED(le)
#else
#error __BYTE_ORDER__ not set to either __ORDER_LITTLE_ENDIAN__ or __ORDER_BIG_ENDIAN__
#endif

HEADER_FUNC u8 acquire8(_Atomic(u8) *x) {
	return atomic_load_explicit(x, memory_order_acquire);
}
HEADER_FUNC u8 relaxedr8(_Atomic(u8) *x) {
	return atomic_load_explicit(x, memory_order_relaxed);
}
HEADER_FUNC void release8(_Atomic(u8) *x, u8 v) {
	atomic_store_explicit(x, v, memory_order_release);
}
HEADER_FUNC void relaxedw8(_Atomic(u8) *x, u8 v) {
	atomic_store_explicit(x, v, memory_order_relaxed);
}
HEADER_FUNC u32 acquire32(_Atomic(u32) *x) {
	return atomic_load_explicit(x, memory_order_acquire);
}
HEADER_FUNC void relaxedw32(_Atomic(u32) *x, u32 v) {
	atomic_store_explicit(x, v, memory_order_relaxed);
}
HEADER_FUNC void release32(_Atomic(u32) *x, u32 v) {
	atomic_store_explicit(x, v, memory_order_release);
}
HEADER_FUNC u32 acquire32v(volatile _Atomic(u32) *x) {
	return atomic_load_explicit(x, memory_order_acquire);
}
HEADER_FUNC void release32v(volatile _Atomic(u32) *x, u32 v) {
	atomic_store_explicit(x, v, memory_order_release);
}
