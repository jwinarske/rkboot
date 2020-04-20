/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include "../include/defs.h"

enum {LZCOMMON_BLOCK = 8};
void lzcommon_literal_copy(u8 *dest, const u8 *src, u32 length);
void lzcommon_match_copy(u8 *dest, u32 dist, u32 length);

#define DEFINE_COMPR_PROBE_STATUS\
	X(SIZE_UNKNOWN, "probe successful, size unknown")\
	X(SIZE_KNOWN, "probe successful, size known")\
	X(NOT_ENOUGH_DATA, "not enough data to probe")\
	X(WRONG_MAGIC, "wrong magic number")\
	X(UNIMPLEMENTED_FEATURE,  "unimplemented feature used")\
	X(RESERVED_FEATURE, "reserved feature used")

enum compr_probe_status {
#define X(name, msg) COMPR_PROBE_##name,
	DEFINE_COMPR_PROBE_STATUS
#undef X
	NUM_COMPR_PROBE_STATUS,
	COMPR_PROBE_LAST_SUCCESS = COMPR_PROBE_SIZE_KNOWN
};

#define DEFINE_DECODE_STATUS\
	X(ERR, "decoding error")\
	X(NEED_MORE_SPACE, "decoding can't proceed without more output space")\
	X(NEED_MORE_DATA, "decoding can't proceed without more input data")

enum {
#define X(name, msg) DECODE_##name,
	DEFINE_DECODE_STATUS
#undef X
	NUM_DECODE_STATUS
};

struct decompressor_state;

typedef size_t decompress_func(struct decompressor_state *state, const u8 *in, const u8 *end);

struct decompressor_state {
	decompress_func *decode;

	/* these are initialized by the client after `decompressor::init`
	client code may move things around between `decompressor_state::decode` calls, but must preserve at least the bytes between window_start and out. */
	u8 *window_start;
	u8 *out;
	u8 *out_end;
};

struct decompressor {
	/* tests if the given input stream is in a format decodable by this decompressor.
	should return an appropriate error if it can be seen that the stream is not decodable.
	should return COMPR_PROBE_NOT_ENOUGH_DATA if the input is not long enough to decide if it is decodable, or it is too short for `decompressor::init` to complete successfully.
	if the input stream can be decoded, may set `*size` and return COMPR_PROBE_SIZE_KNOWN or return COMPR_PROBE_SIZE_UNKNOWN, leaving `*size` unchanged. */
	enum compr_probe_status (*probe)(const u8 *in, const u8 *end, size_t *size);
	size_t state_size;
	/* is allowed to assume that `decompressor::probe` returned a success code.
	 * returns the decode pointer (everything before which can be reclaimed) or null on failure */
	const u8 *(*init)(struct decompressor_state *state, const u8 *in, const u8 *end);
};
