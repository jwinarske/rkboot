/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include "defs.h"

/* the actual definitions just below seem to confuse clangd */
HEADER_FUNC u16 from_le16(u16);
HEADER_FUNC u32 from_le32(u32);
HEADER_FUNC u64 from_le64(u64);
HEADER_FUNC u16 to_le16(u16);
HEADER_FUNC u32 to_le32(u32);
HEADER_FUNC u64 to_le64(u64);

#define DEFINE_SWAPPED(endian) \
HEADER_FUNC u16 endian##16(u16 v) {return __builtin_bswap16(v);}\
HEADER_FUNC u32 endian##32(u32 v) {return __builtin_bswap32(v);}\
HEADER_FUNC u64 endian##64(u64 v) {return __builtin_bswap64(v);}
#define DEFINE_UNSWAPPED(endian) \
HEADER_FUNC u16 endian##16(u16 v) {return v;}\
HEADER_FUNC u32 endian##32(u32 v) {return v;}\
HEADER_FUNC u64 endian##64(u64 v) {return v;}
#define DEFINE_ENDIAN_HELPERS(swapped, unswapped) \
DEFINE_SWAPPED(from_##swapped) DEFINE_SWAPPED(to_##swapped) \
DEFINE_UNSWAPPED(from_##unswapped) DEFINE_UNSWAPPED(to_##unswapped)

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
DEFINE_ENDIAN_HELPERS(be, le)
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
DEFINE_ENDIAN_HELPERS(le, be)
#else
#error __BYTE_ORDER__ not set to either __ORDER_LITTLE_ENDIAN__ or __ORDER_BIG_ENDIAN__
#endif
