#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include "regtool.h"
#include "lpddr4_spec_tables.h"

#define check(expr, ...) do{if (!(expr)) {fprintf(stderr, __VA_ARGS__);exit(1);}}while(0)

#define RPN_OP(name) const char *op_##name(struct context *ctx, struct stack *stack, u64 param, const u8 *rep_values)
RPN_OP(timeunit) {
	struct value *v = stack->values + stack->values_size - 1;
	if (v->type != VAL_NUMBER) {return "value not a raw number";}
	v->val *= param >> 16;
	v->val += param & 0xffff;
	u8 freq = ctx->global_reps & 1 << REP_FREQ
		? ctx->rep_values[REP_FREQ]
		: rep_values[REP_FREQ];
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
RPN_OP(nogreater) {
	struct value *a = stack->values + stack->values_size - 2, *b = a + 1;
	if (a->type != b->type) {return "inputs are of different type";}
	stack->values_size -= 1;
	a->val = a->val <= b->val;
	a->type = VAL_BOOL;
	return 0;
}

RPN_OP(tabulated_latency) {
	struct value *v = stack->values + stack->values_size - 1;
	if (v->type != VAL_NUMBER) {return "value not a raw number";}
	u8 freq = ctx->global_reps & 1 << REP_FREQ
		? ctx->rep_values[REP_FREQ]
		: rep_values[REP_FREQ];
	v->val *= ctx->freq_steps[freq]->values[param];
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
	u8 freq = ctx->global_reps & 1 << REP_FREQ
		? ctx->rep_values[REP_FREQ]
		: rep_values[REP_FREQ];
	v->val = ctx->freq_mhz[freq];
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

struct op_template {
	const char *tok;
	const char *(*op)(struct context *ctx, struct stack *stack, u64 param, const u8 *rep_values);
	u64 param;
};

static void insert_ops(struct  context *ctx, const struct op_template *templ, size_t num, u8 inputs) {
	for (size_t i = 0; i < num; ++i) {
		struct rpn_op *op = BUMP(ctx->ops);
		/*while (op > ctx->ops && strcmp((op - 1)->tok, templ[i].tok) > 0) {
			* insertion sort *
			memcpy(op, op - 1, sizeof(*op));
			op -= 1;
		}*/
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
		{.tok = "<=", .op = op_nogreater},
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

void run_expression(struct context *ctx, struct stack *stack, const char *expr, const char *end, u16 line, u32 local_reps, const u8 *local_rep_values) {
	const struct rpn_op *ops = ctx->ops;
	const size_t ops_size = ctx->ops_size;
	struct parse_ctx {
		const char *pos, *end;
		size_t macro_end;
	};
	DECL_VEC(struct parse_ctx, expansion_stack);
	INIT_VEC(expansion_stack)
	size_t macro_end = ctx->macros_size;
	const char *pos = expr;
	while (1) {
		if (pos >= end) {
			if (!expansion_stack_size) {break;}
			debug("macro expansion ended\n");
			pos = expansion_stack[--expansion_stack_size].pos;
			end = expansion_stack[expansion_stack_size].end;
			macro_end = expansion_stack[expansion_stack_size].macro_end;
		}
		u32 val;
		if (parse_hex(&pos, end, &val) || parse_dec(&pos, end, &val)) {
			struct value *v = BUMP(stack->values);
			v->type = VAL_NUMBER;
			v->val = val;
		} else {
			const char *op_end = pos;
			while (op_end < end && *op_end != ' ') {op_end += 1;}
			size_t op_len = op_end - pos;
			for (size_t i = 0; i < ops_size; ++i) {
				u8 len = ops[i].tok_len;
				if (op_len < len || memcmp(pos, ops[i].tok, len)) {continue;}
				check(stack->values_size >= ops[i].inputs, "line %"PRIu16": not enough values on stack for input to %s in expression '%.*s'", line, ops[i].tok, (int)(end - expr), expr);
				const char *error = ops[i].op(ctx, stack, ops[i].param, local_rep_values);
				check(!error, "line %"PRIu16": evaluation error '%s' in expression '%.*s'", line, error, (int)(end - expr), expr);
				pos += len;
				goto continue_outer;
			}
			for (size_t i = 0; i < macro_end; ++i) {
				struct macro *macro = ctx->macros + i;
				if (op_len != macro->name_len || memcmp(pos, macro->name, op_len)) {continue;}
				debug("expanding macro %.*s\n", (int)(macro->name_len), macro->name);
				const char *continue_pos = stripl(op_end, end);
				if (continue_pos < end) {
					*BUMP(expansion_stack) = (struct parse_ctx) {
						.pos = continue_pos, .end = end,
						.macro_end = macro_end,
					};
				}
				pos = macro->value;
				end = macro->value + macro->value_len;
				macro_end = i;
				debug("current input: %.*s\n", (int)(end - pos), pos);
				goto continue_outer;
			}
			check(0, "line %"PRIu16": cannot tokenize value expression '%.*s', starting at '%.*s'\n", line, (int)(end - expr), expr, (int)(end - pos), pos);
		}
		continue_outer:
		pos = stripl(pos, end);
	}
	free(expansion_stack);
}
