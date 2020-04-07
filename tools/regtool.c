#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <limits.h>
#include <fcntl.h>

#include "lpddr4_spec_tables.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#ifdef DEBUG_MSG
#define debug(...) fprintf(stderr, __VA_ARGS__)
#else
#define debug(...)
#endif
#define check(expr, ...) do{if (!(expr)) {fprintf(stderr, __VA_ARGS__);exit(1);}}while(0)
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define DECL_VEC(type, name) type *name; size_t name##_cap, name##_size
#define INIT_VEC(name) name = 0; name##_cap = name##_size = 0;
#define BUMP(name) ((++ name##_size < name##_cap ? 0 : (name = realloc(name, (name##_cap = (name##_cap ? name##_cap << 1 : 8)) * sizeof(*name)), allocation_failed), name ? 0 : allocation_failed()), name + (name##_size - 1))
static _Noreturn int allocation_failed() {fprintf(stderr, "allocation failed\n");abort();}


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
	
	u32 freq_mhz[3];
	const struct frequency_step *freq_steps[3];
	DECL_VEC(struct rpn_op, ops);
};

const char *const rep_names[NUM_REP] = {
#define X(caps, normal, num) #normal,
	REPETITIONS
#undef X
};
const char *const rep_macro[NUM_REP] = {
#define X(caps, normal, num) #normal "(",
	REPETITIONS
#undef X
};
const u8 rep_num_repetitions[NUM_REP] = {
#define X(caps, normal, num) num,
	REPETITIONS
#undef X
};

const char *stripr(const char *start, const char *end) {
	while (start < end) {
		char c = *(end - 1);
		if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {break;}
		end -= 1;
	}
	return end;
}

const char *stripl(const char *start, const char *end) {
	while (start < end) {
		char c = *start;
		if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {break;}
		start += 1;
	}
	return start;
}

_Bool parse_dec(const char **start, const char *end, u32 *out) {
	u32 val = 0;
	const char *str = *start;
	if (str >= end || *str > '9' || *str < '0') {return 0;}

	char c;
	while (str < end && (c = *str) >= '0' && c <= '9') {
		if (val > (UINT32_MAX - c + '0') / 10) {return 0;}
		val = val * 10 + (c - '0');
		str += 1;
	}
	*start = str;
	*out = val;
	return 1;
}

u16 parse_offset(const char *start, const char *end) {
	u32 reg, bit;
	if (!parse_dec(&start, end, &reg)) {return UINT16_MAX;}
	if (start >= end || *start++ != '+' || start >= end) {return UINT16_MAX;}
	if (!parse_dec(&start, end, &bit) || bit > 32 || reg * 32 + bit >= UINT16_MAX) {return UINT16_MAX;}
	return reg * 32 + bit;
}

const char *memmem(const char *haystack, size_t haystack_len, const char *needle, size_t needle_len) {
	if (haystack_len < needle_len) {return 0;}
	const char *hay_end = haystack + haystack_len - needle_len, *needle_end = needle + needle_len;
	const char *hay = haystack;
	while (haystack <= hay_end) {
		const char *n = needle;
		do {
			if (n >= needle_end) {return haystack;}
		} while (*n++ == *hay++);
		hay = haystack += 1;
	}
	return 0;
}

char *demultiplex(const struct context *ctx, const char *expr, size_t expr_size, u32 local_reps, const u8 *rep_values, size_t *len) {
	u32 active_reps = local_reps | ctx->global_reps;
	char *string = malloc(expr_size);
	assert(string);
	char *string_end = string + expr_size;
	memcpy(string, expr, expr_size);
	for (enum repetition rep = 0; rep < NUM_REP; ++rep) {
		if (!(active_reps & 1 << rep)) {continue;}
		u8 value = local_reps & 1 << rep ? rep_values[rep] : ctx->rep_values[rep];
		const char *occ;
		size_t len = strlen(rep_macro[rep]);
		while ((occ = memmem(string, string_end - string, rep_macro[rep], len))) {
			char *new_str = malloc(string_end - string);
			assert(new_str);
			memcpy(new_str, string, occ - string);
			char *s = new_str + (occ - string);
			occ += len;
			u8 param = 0;
			while (1) {
				check(occ < string_end,
					"unclosed parenthesis in value expression %.*s",
					(int)expr_size, expr
				);
				char c = *occ++;
				if (c == ')') {
					check(param + 1 == rep_num_repetitions[rep],
						"%s call had %"PRIu8" arguments, expected %"PRIu8", in %.*s",
						rep_names[rep], param + 1, rep_num_repetitions[rep],
						(int)expr_size, expr
					);
					break;
				} else if (c == ',') {
					param += 1;
					check(param < rep_num_repetitions[rep],
						"%s call has more than %"PRIu8" arguments, in %.*s",
						rep_names[rep], rep_num_repetitions[rep],
						(int)expr_size, expr
					);
				} else if (param == value) {
					size_t depth = 0;
					while (1) {
						*s++ = c;
						if (c == ')') {
							depth -= 1;
						} else if (c == '(') {
							depth += 1;
						}
						if (depth == 0) {break;}
						check(occ < string_end,
							"unclosed parenthesis in value expression %.*s",
							(int)expr_size, expr
						);
						c = *occ++;
					}
				} else {
					size_t depth = 0;
					while (1) {
						if (c == ')') {
							depth -= 1;
						} else if (c == '(') {
							depth += 1;
						}
						if (depth == 0) {break;}
						check(occ < string_end,
							"unclosed parenthesis in value expression %.*s",
							(int)expr_size, expr
						);
						c = *occ++;
					}
				}
			}
			memcpy(s, occ, string_end - occ);
			s += string_end - occ;
			free(string);
			string = new_str;
			string_end = s;
			debug("substituted %s: %.*s\n", rep_names[rep], (int)(string_end - string), string);
		}
	}
	*len = string_end - string;
	return string;
}

void reg_table(struct context *ctx) {
	u16 last_reg = UINT16_MAX;
	for (size_t f = 0; f < ctx->fields_size; ++f) {
		const struct field *field = ctx->fields + f;
		const struct line *pline = ctx->lines + field->line;
		/*if (pline->flags & 1 << SUBFIELD_BIT) {continue;}
		if (pline->name_len == 8 && !strncmp("reserved", pline->name, pline->name_len)) {continue;}*/
		printf("\t");
		if (last_reg != field->offset / 32) {
			printf("%"PRIu16, field->offset / 32);
			last_reg = field->offset / 32;
		}
		printf("\t%"PRIu16"\t", field->offset % 32);
		if (pline->size != 1) {
			printf("%"PRIu8, pline->size);
		}
		if (pline->flags & 1 << PACKED_BIT) {
			printf("\t\t");
		} else {
			if (pline->flags & 1 << COMMAND_BIT) {
				printf("\tw\t");
			} else {
				printf("\tr%.*s%.*s\t", !(pline->flags & 1 << RO_BIT), "w", !!(pline->flags & 1 << VOLATILE_BIT), "x");
			}
		}
		if (field->flags & 1 << REP_ADDRBIT) {
			printf("%"PRIu8" ", field->rep_value[REP_ADDRBIT]);
		} else if (field->flags & 1 << REP_DATABIT) {
			printf("%"PRIu8" ", field->rep_value[REP_DATABIT]);
		}
		printf("%.*s", (int)pline->name_len, pline->name);
		if (field->flags & 1 << REP_FREQ) {
			printf(" F%"PRIu8, field->rep_value[REP_FREQ]);
		}
		if (field->flags & 1 << REP_CS) {
			printf(" %"PRIu8, field->rep_value[REP_CS]);
		} else if (field->flags & 1 << REP_CS4) {
			printf(" %"PRIu8"%.*s", field->rep_value[REP_CS4] & 1, !!(field->rep_value[REP_CS4] >> 1), "x");
		}
		if (field->flags & 1 << REP_ASLICE) {
			printf(" %"PRIu8, field->rep_value[REP_ASLICE]);
		} else if (field->flags & 1 << REP_DSLICE) {
			printf(" %"PRIu8, field->rep_value[REP_DSLICE]);
		} else if (field->flags & 1 << REP_ACSLICE) {
			printf(" %"PRIu8, field->rep_value[REP_ACSLICE]);
		}
		size_t demuxed_len;
		char *demuxed = demultiplex(ctx, pline->value, pline->value_len, field->flags, field->rep_value, &demuxed_len);
		const char *demux_end = stripr(demuxed, demuxed + demuxed_len), *demux_stripped = stripl(demuxed, demux_end);
		printf(" = %.*s\n", (int)(demux_end - demux_stripped), demux_stripped);
		free(demuxed);
	}
}

static _Bool ishex(char c) {
	return c <= 'F' ?  (c <= '9' ? c >= '0' : c >= 'A') : (c <= 'f' && c >= 'a');
}

_Bool parse_hex(const char **start, const char *end, u32 *out) {
	assert(start);
	const char *pos = *start;
	assert(end >= pos);
	if (end - pos < 3 || *pos++ != '0' || *pos++ != 'x') {return 0;}
	char c = *pos;
	if (!ishex(c)) {return 0;}
	u32 val = 0;
	while (1) {
		if (c <= 'F') {
			if (c <= '9') {
				if (c < '0') {break;}
				c -= '0';
			} else if (c >= 'A') {
				c -= 'A' - 10;
			} else {break;}
		} else {
			if (c >= 'a' && c <= 'f') {
				c -= 'a' - 10;
			} else {break;}
		}
		if (val > (UINT32_MAX >> 4)) {return 0;}
		val = val << 4 | c;
		if (++pos == end) {break;}
		c = *pos;
	}
	*start = pos;
	*out = val;
	return 1;
}

#define RPN_OP(name) const char *op_##name(struct context *ctx, struct stack *stack, u64 param, const u8 *rep_values)
RPN_OP(timeunit) {
	struct value *v = stack->values + stack->values_size - 1;
	if (v->type != VAL_NUMBER) {return "value not a raw number";}
	v->val *= param >> 16;
	v->val += param & 0xffff;
	u8 freq = rep_values[REP_FREQ];
	assert(freq < 3);
	v->val *= ctx->freq_mhz[freq];
	v->type = VAL_QMCYC;
	return 0;
}

RPN_OP(clocks) {
	struct value *v = stack->values + stack->values_size - 1;
	if (v->type != VAL_NUMBER) {return "value not a raw number";}
	v->val *= 4000;
	v->val += param;
	v->type = VAL_QMCYC;
	return 0;
}

RPN_OP(max) {
	struct value *a = stack->values + stack->values_size - 2, *b = a + 1;
	if (a->type != b->type) {return "inputs are of different type";}
	stack->values_size -= 1;
	if (a->val < b->val) {a->val = b->val;}
	return 0;
}

RPN_OP(plus) {
	struct value *a = stack->values + stack->values_size - 2, *b = a + 1;
	if (a->type != b->type) {return "inputs are of different type";}
	stack->values_size -= 1;
	a->val += b->val;
	return 0;
}

RPN_OP(less) {
	struct value *a = stack->values + stack->values_size - 2, *b = a + 1;
	if (a->type != b->type) {return "inputs are of different type";}
	stack->values_size -= 1;
	a->val = a->val < b->val;
	a->type = VAL_BOOL;
	return 0;
}

RPN_OP(tabulated_latency) {
	struct value *v = stack->values + stack->values_size - 1;
	if (v->type != VAL_NUMBER) {return "value not a raw number";}
	v->val *= ctx->freq_steps[rep_values[REP_FREQ]]->values[param];
	return 0;
}

RPN_OP(round) {
	struct value *v = stack->values + stack->values_size - 1;
	if (v->type != VAL_QMCYC) {return "value not a timing value";}
	v->val += param;
	v->val -= v->val % 4000;
	return 0;
}

RPN_OP(select) {
	struct value *a = stack->values + stack->values_size - 3, *b = a + 1, *v = a + 2;
	if (v->type != VAL_BOOL) {return "condition not a boolean";}
	if (!v->val) {
		a->val = b->val;
	}
	stack->values_size -= 2;
	return 0;
}

RPN_OP(mhz) {
	struct value *v = BUMP(stack->values);
	v->type = VAL_NUMBER;
	v->val = ctx->freq_mhz[rep_values[REP_FREQ]];
	return 0;
}

RPN_OP(paren_open) {
	struct value *v = BUMP(stack->values);
	v->type = VAL_PARENTHESIS;
	v->val = 0;
	return 0;
}

RPN_OP(paren_close) {
	struct value *a = stack->values + stack->values_size - 2, *b = a + 1;
	if (a->type != VAL_PARENTHESIS) {return "parenthesis unmatched";}
	stack->values_size -= 1;
	a->val = b->val;
	a->type = b->type;
	return 0;
}

void hex_blob(struct context *ctx) {
	u32 mask = 0;
	u32 reg_val = 0;
	u16 reg = 0;
	struct stack stack;
	INIT_VEC(stack.values);
	for (size_t f = 0; f < ctx->fields_size; ++f) {
		const struct field *field = ctx->fields + f;
		const struct line *line = ctx->lines + field->line;
		if (line->flags & (1 << RO_BIT | 1 << PACKED_BIT)) {continue;}
		while (field->offset / 32 > reg) {
			printf("0x%08"PRIx32",\n", reg_val);
			reg_val = 0;
			mask = 0;
			reg += 1;
		}
		size_t demuxed_len;
		char *demuxed = demultiplex(ctx, line->value, line->value_len, field->flags, field->rep_value, &demuxed_len);
		const char *demux_end = stripr(demuxed, demuxed + demuxed_len), *demux = stripl(demuxed, demux_end), *demux_stripped = demux;
		assert(line->size <= 32);
		const struct rpn_op *ops = ctx->ops;
		size_t ops_size= ctx->ops_size;
		while (demux < demux_end) {
			u32 val;
			if (parse_hex(&demux, demux_end, &val) || parse_dec(&demux, demux_end, &val)) {
				struct value *v = BUMP(stack.values);
				v->type = VAL_NUMBER;
				v->val = val;
			} else {
				for (size_t i = 0; i < ops_size; ++i) {
					u8 len = ops[i].tok_len;
					if (demux_end - demux < len || memcmp(demux, ops[i].tok, len)) {continue;}
					check(stack.values_size >= ops[i].inputs, "line %"PRIu16": not enough values on stack for input to %s in expression '%.*s'", line->line, ops[i].tok, (int)(demux_end - demux_stripped), demux_stripped);
					const char *error = ops[i].op(ctx, &stack, ops[i].param, field->rep_value);
					check(!error, "line %"PRIu16": evaluation error '%s' in expression '%.*s'", line->line, error, (int)(demux_end - demux_stripped), demux_stripped);
					demux += len;
					goto continue_outer;
				}
				check(0, "line %"PRIu16": cannot tokenize value expression '%.*s', starting at '%.*s'\n", line->line, (int)(demux_end - demux_stripped), demux_stripped, (int)(demux_end - demux), demux);
			}
			continue_outer:
			demux = stripl(demux, demux_end);
		}
		check(stack.values_size == 1, "line %"PRIu16": value expression '%.*s' produced more than one result\n", line->line, (int)(demux_end - demux_stripped), demux_stripped);
		u32 field_value;
		if (line->flags & (1 << LOWER_LIMIT_BIT | 1 << UPPER_LIMIT_BIT)) {
			check(stack.values[0].type == VAL_QMCYC, "line %"PRIu16": value expression '%.*s' did not produce a timing value\n", line->line, (int)(demux_end - demux_stripped), demux_stripped);
			field_value = (stack.values[0].val + (line->flags & 1 << UPPER_LIMIT_BIT ? 3999 : 0)) / 4000;
		} else {
			check(stack.values[0].type == VAL_NUMBER, "line %"PRIu16": value expression '%.*s' did not produce a raw number\n", line->line, (int)(demux_end - demux_stripped), demux_stripped);
			field_value = stack.values[0].val;
		}
		reg_val |= field_value << field->offset % 32;
		stack.values_size = 0;
	}
	free(stack.values);
	printf("0x%08"PRIx32",\n", reg_val);
}

void read_lines(struct context *ctx, const char *input_ptr, const char *input_end) {
	u16 line = 1;
	do {
		const char *line_end = memchr(input_ptr, '\n', input_end - input_ptr);
		if (!line_end) {
			line_end = input_end;
		} else {
			line_end += 1;
		}
		u8 indent = 0;
		while (input_ptr < line_end && *input_ptr == '\t') {
			input_ptr += 1;
			check(indent < NUM_REP, "line %"PRIu16": insane indent (≥%"PRIu8" tabs)", line, indent);
			indent += 1;
		}
		const char *comment = memchr(input_ptr, '#', line_end - input_ptr);
		if (!comment) {comment = line_end;}
		comment = stripr(input_ptr, comment);
		const char *tab = memchr(input_ptr, '\t', comment - input_ptr);
		if (tab) { /* field */
			assert(tab > input_ptr);
			u8 alignment = 0;
			u32 flags = 0;
			while (1) {
				switch (*input_ptr++) {
				case '!':
					check(!(flags & 1 << VOLATILE_BIT), "line %"PRIu16": double ! specified", line);
					flags |= 1 << VOLATILE_BIT;
					break;
				case '*':
					check(!(flags & 1 << COMMAND_BIT), "line %"PRIu16": double * specified", line);
					flags |= 1 << COMMAND_BIT;
					break;
				case '<':
					check(!(flags & 1 << UPPER_LIMIT_BIT), "line %"PRIu16": double < specified", line);
					flags |= 1 << UPPER_LIMIT_BIT;
					break;
				case '>':
					check(!(flags & 1 << LOWER_LIMIT_BIT), "line %"PRIu16": double > specified", line);
					flags |= 1 << LOWER_LIMIT_BIT;
					break;
				case 'b': alignment = 8; goto endloop;
				case 'h': alignment = 16; goto endloop;
				case 'w': alignment = 32; goto endloop;
				case 'p': alignment = 1; flags |= 1 << SUBFIELD_BIT; goto endloop;
				default: check(0, "line %"PRIu16": unknown alignment specifier\n", line);
				}
			} endloop:;

			const char *ptr = input_ptr;
			u32 length;
			check(parse_dec(&ptr, tab, &length) && ptr == tab && length <= 32 && length > 0, "line %"PRIu16": malformed field specifier", line);
			const char *name = stripl(tab, comment);
			const char *equals = memchr(name, '=', comment - name);
			const char *name_end = comment, *value = 0;
			size_t name_len, value_len = 0;
			if (equals) {
				name_end = equals;
				value = stripl(equals + 1, comment);
				value_len = stripr(value, comment) - value;
			}
			name_end = stripr(name, name_end);
			const char *ext_specifier = memchr(name, ':', name_end - name), *ext_spec_end = name_end;
			if (ext_specifier) {
				name_end = stripr(name, ext_specifier);
				do {
					ext_specifier += 1;
					const char *comma = memchr(ext_specifier, ',', ext_spec_end - ext_specifier), *next = comma;
					if (!comma) {comma = next = ext_spec_end;}
					ext_specifier = stripl(ext_specifier, ext_spec_end);
					const char *end = stripr(ext_specifier, next);
					if (end - ext_specifier == 6 && !strncmp("packed", ext_specifier, end - ext_specifier)) {
						flags |= 1 << PACKED_BIT;
					} else if (end - ext_specifier == 2 && !strncmp("ro", ext_specifier, end - ext_specifier)) {
						flags |= 1 << RO_BIT | 1 << VOLATILE_BIT;
					} else if (end - ext_specifier == 6 && !strncmp("static", ext_specifier, end - ext_specifier)) {
						flags |= 1 << RO_BIT;
					} else {
						check(0, "line %"PRIu16": extended specifier %.*s unknown", line, (int)(end - ext_specifier), ext_specifier);
					}
					ext_specifier = next;
				} while(ext_specifier < ext_spec_end);
			}
			name_len = name_end - name;
			
			debug("field line %"PRIu16", alignment %"PRIu8", flags %"PRIx32", indent %"PRIu8", length %"PRIu32", %.*s = %.*s\n", line, alignment, flags, indent, length, (int)name_len, name, (int)value_len, value);
			struct line *parsed_line = BUMP(ctx->lines);
			parsed_line->line = line;
			parsed_line->indent = indent;
			parsed_line->type = LINE_FIELD;
			parsed_line->alignment = alignment;
			parsed_line->size = length;
			parsed_line->value = value;
			parsed_line->value_len = value_len;
			parsed_line->name = name;
			parsed_line->name_len = name_len;
			parsed_line->flags = flags;
		} else if (input_ptr != comment) { /* repetition */
			const char *space1 = memchr(input_ptr, ' ', comment - input_ptr);
			check(space1, "line %"PRIu16" has neither comment nor space nor non-indent tab in it\n", line);
			enum repetition rep = 0;
			do {
				if (!strncmp(rep_names[rep], input_ptr, space1 - input_ptr)) {break;}
			} while (++rep < NUM_REP);
			check(rep != NUM_REP, "line %"PRIu16": unknown repetition specifier %.*s\n", line, (int)(space1 - input_ptr), input_ptr);
			input_ptr = stripl(space1, comment);

			const char *space2 = memchr(input_ptr, ' ', comment - input_ptr);
			check(space2, "line %"PRIu16" has no second offset indication\n", line);
			u16 start_offset = parse_offset(input_ptr, space2);
			check(start_offset != UINT16_MAX, "cannot parse start offset in line %"PRIu16"\n", line);

			input_ptr = stripl(space2, comment);
			u16 end_offset = parse_offset(input_ptr, comment);
			check(end_offset != UINT16_MAX, "cannot parse end offset in line %"PRIu16"\n", line);

			debug("%s %"PRIu16" %"PRIu16"\n", rep_names[rep], start_offset, end_offset);
			struct line *parsed_line = BUMP(ctx->lines);
			parsed_line->line = line;
			parsed_line->indent = indent;
			parsed_line->type = LINE_PRAGMA;
			parsed_line->rep = rep;
			parsed_line->offset_start = start_offset;
			parsed_line->offset_end = end_offset;
		}
		input_ptr = line_end;
		check(line < UINT16_MAX, "input has too many lines (>%"PRIu16")\n", UINT16_MAX);
		line += 1;
	} while (input_ptr < input_end);
	debug("got %zu lines\n", ctx->lines_size);
}

void layout_fields(struct context *ctx) {
	/** indexed by nesting depth */
	enum repetition reps_active[NUM_REP];
	u8 num_active_reps = 0;
	u16 rep_end[NUM_REP];
	size_t rep_start[NUM_REP];
	size_t pos = 0;
	u16 cur_offset = 0;
	memset(rep_start, 0, sizeof(rep_start));
	memset(rep_end, 0, sizeof(rep_end));
	while (1) {continue_outer_loop:;
		u8 cur_indent = 0;
		/* don't immediately break out of the loop at end of input, let repetitions do their processing */
		if (pos < ctx->lines_size) {
			cur_indent = ctx->lines[pos].indent;
			check(cur_indent <= num_active_reps,
				"line %"PRIu16": indented without declaring the repetition scope\n",
				ctx->lines[pos].line
			);
		}
		while (cur_indent < num_active_reps) {
			enum repetition rep = reps_active[num_active_reps - 1];
			if (++ctx->rep_values[rep] < rep_num_repetitions[rep]) {
				pos = rep_start[rep] + 1;
				goto continue_outer_loop;
			} else {
				for (int i = 0; i < num_active_reps - 1; ++i) {
					if (ctx->rep_values[reps_active[i]] != 0) {goto skip_end_check;}
				}
				check(rep_end[rep] == cur_offset,
					"line %"PRIu16": end offset did not match",
					ctx->lines[rep_start[rep]].line
				);
				skip_end_check:;
			}
			ctx->rep_values[rep] = 0;
			num_active_reps -= 1;
		}
		/* we are at the end and have no pending repetitions */
		if (pos >= ctx->lines_size) {break;}

		struct line *pline = ctx->lines + pos;
		if (pline->type == LINE_FIELD) {
			/* align field */
			cur_offset = (cur_offset + pline->alignment - 1) & ~(u16)(pline->alignment - 1);

			/* start new register if field would straddle a boundary */
			if ((cur_offset + pline->size - 1) / 32 != cur_offset / 32) {
				cur_offset = (cur_offset & ~31) + 32;
			}

			debug("field %.*s, %u+%u",
				(int)ctx->lines[pos].name_len, ctx->lines[pos].name,
				(unsigned)cur_offset / 32, (unsigned)cur_offset % 32
			);
			u32 flags = 0;
			for (int i = 0; i < num_active_reps; ++i) {
				enum repetition rep = reps_active[i];
				flags |= 1 << rep;
				debug(", %s=%"PRIu8, rep_names[rep], ctx->rep_values[rep]);
			}
			debug(", length %u\n", (unsigned)pline->size);

			struct field *field = BUMP(ctx->fields);
			field->line = pos;
			field->flags = flags;
			field->offset = cur_offset;
			memcpy(field->rep_value, ctx->rep_values, sizeof(ctx->rep_values));

			/* packed fields have their lengths given only for defining their start offset */
			if (!(pline->flags & 1 << PACKED_BIT)) {
				cur_offset += pline->size;
			}
		} else {
			assert(pline->type == LINE_PRAGMA);
			_Bool skip_start_check = 0;
			for (int i = 0; i < num_active_reps; ++i) {
				check(reps_active[i] != pline->rep,
					"line %"PRIu16": nested repetitions of type %s\n",
					pline->line, rep_names[pline->rep]
				);
				skip_start_check |= ctx->rep_values[reps_active[i]] != 0;
			}
			check(skip_start_check || cur_offset == pline->offset_start,
				"line %"PRIu16": start offset does not match: expected %u+%u, got %u+%u", pline->line,
				(unsigned)pline->offset_start / 32, (unsigned)pline->offset_start % 32,
				(unsigned)cur_offset / 32, (unsigned)cur_offset % 32
			);
			check(!(ctx->global_reps & 1 << pline->rep),
				"line %"PRIu16": '%s' value was set globally, cannot be used as a repetition", pline->line,
				rep_names[pline->rep]
			);

			if (pline->rep != REP_SYNC) {
				assert(num_active_reps + 1 < NUM_REP);
				reps_active[num_active_reps++] = pline->rep;
				ctx->rep_values[pline->rep] = 0;
				rep_end[pline->rep] = pline->offset_end;
				rep_start[pline->rep] = pos;
			} else {
				check(num_active_reps == 0,
					"line %"PRIu16": sync pragmas should only be used at the top level\n",
					pline->line
				);
				cur_offset = pline->offset_end;
			}
		}
		pos += 1;
	}
}

struct op_template {
	const char *tok;
	const char *(*op)(struct context *ctx, struct stack *stack, u64 param, const u8 *rep_values);
	u64 param;
};

static void insert_ops(struct  context *ctx, const struct op_template *templ, size_t num, u8 inputs) {
	for (size_t i = 0; i < num; ++i) {
		struct rpn_op *op = BUMP(ctx->ops);
		while (op > ctx->ops && strcmp((op - 1)->tok, templ[i].tok) > 0) {
			/* insertion sort */
			memcpy(op, op - 1, sizeof(*op));
			op -= 1;
		}
		op->op = templ[i].op;
		op->tok = templ[i].tok;
		op->tok_len = strlen(templ[i].tok);
		op->inputs = inputs;
		op->param = templ[i].param;
	}
}

void init_ops(struct context *ctx) {
	INIT_VEC(ctx->ops);
	static const struct op_template nullary[] = {
		{.tok = "MHz", .op = op_mhz},
		{.tok = "(", .op = op_paren_open},
	};
	static const struct op_template unary[] = {
		{.tok = "ns", .op = op_timeunit, .param = 4 << 16},
		{.tok = ".5ns", .op = op_timeunit, .param = 4 << 16 | 2},
		{.tok = ".75ns", .op = op_timeunit, .param = 4 << 16 | 3},
		{.tok = "us", .op = op_timeunit, .param = 4000 << 16},
		{.tok = "tCK", .op = op_clocks, .param = 0},
		{.tok = ".5tCK", .op = op_clocks, .param = 2000},
		{.tok = "tRFC", .op = op_timeunit, .param = 180*4 << 16},
		{.tok = "ceil", .op = op_round, .param = 3999},
		{.tok = "floor", .op = op_round, .param = 0},
#define X(name) {.tok = #name, .op = op_tabulated_latency, .param = LAT_##name},
		TABULATED_VALUES
#undef X
	};
	static const struct op_template binary[] = {
		{.tok = "max", .op = op_max},
		{.tok = "+", .op = op_plus},
		{.tok = "<", .op = op_less},
		{.tok = ")", .op = op_paren_close}
	};
	static const struct op_template ternary[] = {
		{.tok = "sel", .op = op_select},
	};
	insert_ops(ctx, nullary, ARRAY_SIZE(nullary), 0);
	insert_ops(ctx, unary, ARRAY_SIZE(unary), 1);
	insert_ops(ctx, binary, ARRAY_SIZE(binary), 2);
	insert_ops(ctx, ternary, ARRAY_SIZE(ternary), 3);
	for (size_t i = 0; i < ctx->ops_size; ++i) {debug("registered op %s\n", ctx->ops[i].tok);}
}

int main(int argc, char **argv)  {
	struct context ctx;
	init_ops(&ctx);
	INIT_VEC(ctx.lines);
	INIT_VEC(ctx.fields);
	memset(ctx.rep_values, 0, sizeof(ctx.rep_values));
	ctx.global_reps = 0;
	
	DECL_VEC(char, buf);

	INIT_VEC(buf);
	buf_cap = 1024;
	buf = malloc(buf_cap);
	assert(buf);


	ctx.freq_mhz[0] = 400; ctx.freq_mhz[1] = 800; ctx.freq_mhz[2] = 50;
	ctx.freq_steps[0] = frequency_table + 1;
	ctx.freq_steps[1] = frequency_table + 2;
	ctx.freq_steps[2] = frequency_table + 0;
	for (char **arg = argv + 1; *arg; ++arg) {
		char *cmd = *arg;
		if (!strcmp("--read", cmd)) {
			buf_size = 0;
			ctx.lines_size = 0;
			ctx.fields_size = 0;
			char *filename = *++arg;
			check(filename, "%s needs a filename parameter\n", cmd);
			int fd = 0;
			if (strcmp("-", filename)) {
				check((fd = open(filename, O_RDONLY)) >= 0, "could not open input file %s", filename);
			}
			while (1) {
				if (buf_cap - buf_size < 128) {
					buf = realloc(buf, buf_cap *= 2);
					assert(buf);
				}
				ssize_t res = read(fd, buf + buf_size, buf_cap - buf_size);
				if (res > 0) {
					buf_size += res;
				} else if (!res) {
					break;
				} else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
					perror("While reading input file");
				}
			}
			debug("read %zu bytes\n", buf_size);
			check(buf_size < INT_MAX, "input longer than INT_MAX bytes\n");
			read_lines(&ctx, buf, buf + buf_size);
			layout_fields(&ctx);
		} else if (!strcmp("--mhz", cmd)) {
			char *f0_str, *f1_str, *f2_str;
			check((f0_str = *++arg) && (f1_str = *++arg) && (f2_str = *++arg),
			      "%s needs 3 parameters\n", cmd
			);
			check(sscanf(f0_str, "%"PRIu32, ctx.freq_mhz) == 1,
			      "cannot parse %s as a MHz value\n", f0_str
			);
			check(sscanf(f1_str, "%"PRIu32, ctx.freq_mhz + 1) == 1,
			      "cannot parse %s as a MHz value\n", f1_str
			);
			check(sscanf(f2_str, "%"PRIu32, ctx.freq_mhz + 2) == 1,
			      "cannot parse %s as a MHz value\n", f2_str
			);
			for (size_t f = 0; f < 3; ++f) {
				ctx.freq_steps[f] = frequency_table;
				while (
					ctx.freq_steps[f] < frequency_table + ARRAY_SIZE(frequency_table)
					&& ctx.freq_steps[f]->MHz < ctx.freq_mhz[f]
				) {
					ctx.freq_steps[f] += 1;
				}
			}
		} else if (!strcmp("--table", cmd)) {
			reg_table(&ctx);
		} else if (!strcmp("--hex", cmd)) {
			hex_blob(&ctx);
		} else if (!strcmp("--set", cmd)) {
			char *var, *val;
			check((var = *++arg) && (val = *++arg), "%s needs name and value parameters\n", cmd);
			for (enum repetition rep = 0; rep < NUM_REP; ++rep) {
				if (0 != strcmp(var, rep_names[rep])) {continue;}
				ctx.global_reps |= 1 << rep;
				check(sscanf(*arg, "%"SCNu8, ctx.rep_values + rep) == 1, "could not parse %s as 8-bit uint\n", *arg);
				check(ctx.rep_values[rep] < rep_num_repetitions[rep], "%s value out of bounds\n", rep_names[rep]);
			}
		} else {
			check(0, "unknown command/parameter %s\n", cmd);
		}
	}
	return 0;
}
