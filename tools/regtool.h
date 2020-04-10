#pragma once
#include <stdint.h>
#include <stdlib.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#ifdef DEBUG_MSG
#define debug(...) fprintf(stderr, __VA_ARGS__)
#else
#define debug(...)
#endif
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define DECL_VEC(type, name) type *name; size_t name##_cap, name##_size
#define INIT_VEC(name) name = 0; name##_cap = name##_size = 0;
#define BUMP(name) ((++ name##_size < name##_cap ? 0 : (name = realloc(name, (name##_cap = (name##_cap ? name##_cap << 1 : 8)) * sizeof(*name)), allocation_failed), name ? 0 : allocation_failed()), name + (name##_size - 1))
_Noreturn int allocation_failed();


#define REPETITIONS \
	X(FREQ, freq, 3)\
	X(CS, cs, 2)\
	X(CS4, cs4, 4)\
	X(ADDRBIT, addrbit, 6)\
	X(DATABIT, databit, 8)\
	X(DEVICE, device, 2)\
	X(DSLICE, dslice, 4)\
	X(ASLICE, aslice, 3)\
	X(ACSLICE, acslice, 4)\
	X(MYSTERY10, mystery10, 10)\
	X(SYNC, sync, 0)

enum line_type {LINE_PRAGMA, LINE_FIELD};
enum repetition {
#define X(caps, normal, num) REP_##caps,
	REPETITIONS
#undef X
	NUM_REP
};
enum {
	RO_BIT = 0,
	VOLATILE_BIT,
	COMMAND_BIT,
	PACKED_BIT,
	SUBFIELD_BIT,
	UPPER_LIMIT_BIT,
	LOWER_LIMIT_BIT,
	NUM_LINE_FLAGS
};

enum value_type {
	VAL_NUMBER,
	VAL_QMCYC,
	VAL_PARENTHESIS,
	VAL_BOOL,
};

struct value {
	enum value_type type;
	u64 val;
};
struct stack {DECL_VEC(struct value, values);};

struct context;

struct token {
	void (*operator)(struct context *, struct stack *, u32 param);
	u32 param;
};

struct line {
	u16 line;
	u8 indent;
	enum line_type type;
	union {
		struct {
			const char *name, *value;
			size_t name_len, value_len;
			u8 alignment;
			u8 size;
			u32 flags;
		};
		struct {
			enum repetition rep;
			u16 offset_start, offset_end;
		};
	};
};

struct field {
	size_t line;
	u32 flags;
	u16 offset;
	u8 size;
	u8 rep_value[NUM_REP];
};

struct rpn_op {
	const char *tok;
	const char *(*op)(struct context *ctx, struct stack *stack, u64 param, const u8 *rep_values);
	u8 tok_len; u8 inputs;
	u64 param;
};

struct context {
	DECL_VEC(struct line, lines);
	DECL_VEC(struct field, fields);

	u32 global_reps;
	u8 rep_values[NUM_REP];

	u16 first, last;
	u32 freq_mhz[3];
	const struct frequency_step *freq_steps[3];
	DECL_VEC(struct rpn_op, ops);
};

extern const char *const rep_names[NUM_REP];

_Bool parse_hex(const char **str, const char *end, u32 *out);
_Bool parse_dec(const char **str, const char *end, u32 *out);
const char *stripl(const char *start, const char *end);
void init_ops(struct context *ctx);
void run_expression(struct context *ctx, struct stack *stack, const char *expr, const char *end, u16 line, u32 local_reps, const u8 *local_rep_values);
