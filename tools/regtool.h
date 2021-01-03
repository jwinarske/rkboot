/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <stdint.h>
#include <stdlib.h>

/*
=======
regtool
=======

regtool reads a description of register fields from its input file and linearly lays them out into 32-bit registers.
*/
/* the input format is described here, next to the structures it is read into. */

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

/*
type of lines
=============

an effective line can take 3 forms:

- a macro, which is constructed as `macro[space]${name} = ${expression}`

- a pragma. which is constructed as ``${directive}[space]${start_register}+${start_offset}[space]${end_register}+${end_offset}``. see the directives below.

- a field description, which is constructed as ${specifier}[tab]${name}:${extended_specifier}=${value_expression}. the :${extended_specifier} and =${value_expression} parts are optional, but generating values for the registers requires having at least one given.

each line may be preceded by a (significant) number of tab characters and may have insignificant trailing whitespace and/or a comment started with a # character.

a line that contains no content except for possibly a comment is ignored for further processing.

regtool distinguishes the line types by the presence or lack of a non-leading, non-trailing tab character.
*/
enum line_type {LINE_PRAGMA, LINE_FIELD};


/*
pragma lines
============

a pragma line starts with a directive, which is usually a repetition command. the number of repetitions for each directive can be taken from the macro definition. the file format uses the lowercase version of the name exclusively.

following the directive is a register+offset pair (no spaces inbetween) that will be checked when the pragma is first processed while laying out registers.
the position of the layout "pointer" after processing the last and before processing the following field description is compared to the given values and if they are not the same, processing is aborted.
this is a non-intrusive reliability feature, since it allows to detect missing fields when originally writing the register description, while not requiring every field to describe its position explicitly.

the line ends in a second pair of register+offset (again, no spaces).
this position is what the layout pointer is supposed to be after processing the pragma and its covered lines. what this means depends on the directive used.

a repetition pragma may be followed by a number of "covered" lines, which are denoted by their higher indentation.
these will be processed a number of times as given by the used directive.
after this, the end register+offset values are compared to the current layout pointer, and like at the beginning, processing is aborted if they are different.

there is currently only one directive that is not a repetition: the 'sync' directive should not have any covered lines, and instead of comparing the end register+offset values to the layout pointer, it *moves* the layout pointer to that position.
because this only works once, the sync directive can only be used at the top level (i. e. not covered by any repetition pragmas)
*/
#define REPETITIONS /* X(CAPS_NAME, lowercase_name, num_reps) */ \
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
enum repetition {
#define X(caps, normal, num) REP_##caps,
	REPETITIONS
#undef X
	NUM_REP
};

/*
field lines
===========

a field description line starts with a specifier, which is a short description of many different properties of the field.

it may start with any number of flag characters, described below.

a specifier always contains an alignment specification, which can currently be 'p', 'b', 'h' or 'w', for an unaligned field with the SUBFIELD flag (see below), a byte-aligned field, a (16-bit-)halfword-aligned field, and a (32-bit)word-aligned field respectively.

a specifier ends with a length indication in bits, given as a decimal number. note that a field may be larger or smaller that its alignment.
the length is used for 3 purposes:

- calculation of start offset: if, after moving the layout pointer to the next alignment boundary as specified, there is not enough space in the register for this field, the pointer is moved to the start of the next register.

- calculation of end offset: after processing the field description, the layout pointer is moved ahead by the given number of bits.

  an exception to this are "packed fields", described below.

- value checking: after computing the value for a field during register value generation, the value is checked to be representable in the field.

following the specifier, separated by a tab character, is the name of the field. it is purely for human consumption and is not used in register value generation.

following the name may be an "extended specifier", separated by a colon, which is a (comma-separated list of) keyword(s) that is/are mapped to a flag, described below.

following name or extended specifier may be a value expression, separated by a = character. the syntax and semantics for those is described further below (TODO).

generally, flags are only mapped to extended specifiers if it is expected that that information will in some way 'replace' a value expression, i. e. extended specifiers and value expressions are not expected to used at the same time.

field flags
-----------
.. role:: kw(code)
*/
enum {
/*
- the :kw:`ro` and :kw:`static` extended specifiers describe that a field is not writable for the CPU.
  the :kw:`static` extended specifier additionally defines that the field cannot change its value through hardware events.
  :kw:`ro` or :kw:`static` fields are unconditionally set to 0 during register value generation and thus do not need a value expression.

- the ! specifier flag character specifies that a (writable; TODO: check this) field may change its value throug hardware events.
*/
	RO_BIT = 0, /*< field not writable from CPU */
	VOLATILE_BIT, /*< field may change its value */
/*
- the * specifier flag character specifies that the field is used to issue a command to hardware.
  it may not be readable for the CPU.
  if used together with the ! flag character, it specifies a self-clearing command field.
*/
	COMMAND_BIT,
/*
- the :kw:`packed` extended specifier describes that a field is composed of multiple subfields.
  the definition for these fields follow the packed field immediately, but may be instanced through repetition pragmas.

  this in particular means that while the length specification is used for packed fields to calculate the start offset, it is not used also used to move the layout pointer, but instead to check whether the following sequence of subfields matches the length of the enclosing field.

  subfields are designated by the special alignment specifier 'p', which implies no alignment being enforced by the layout algorithm.
*/
	PACKED_BIT,
	SUBFIELD_BIT,
/*
- the < and > specifier flag characters specify that a field's value expression should evaluate to a timing value (instead of a raw number) which is rounded up or down (respectively) to full cycles
*/
	UPPER_LIMIT_BIT,	/*< round up timing value */
	LOWER_LIMIT_BIT,	/*< round down timing value */
	NUM_LINE_FLAGS	/*< number of line flag bits, not an actual flag bit */
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

/*
value expressions
=================

value expressions have two layers of syntax following their two stages of evaluation.

the first stage is called 'demultiplexing' and can be performed whenever values for the relevant repetition variables (see _`pragma lines`) are known. it consists of replacing text macro invocations of the form :code:`$repetition_variable[no space]($comma_separated_argument_list)` by the argument specified by the variable's value.
for instance :code:`freq(arg0, arg1, arg2)` would be evaluated to :code:`arg1` if :code:`freq` had the value 1 or :code:`arg2` if it had the value 2.
the number of arguments to such a macro call is checked to match the number of valid values for the variable.

matching parentheses and their contents are considered a unit by the stage 1 evaluation, and commas inside such pairs of parentheses are not considered a separator for the argument list of the current macro. this allows for nesting of macro invocations

macro calls are only evaluated (or checked) for variables whose value is either given by a repetition pragma or set globally on the regtool command line.
missed global variable values may lead to evaluation failures in the second stage.

the second stage of evaluation can take place when other things such as the frequencies associated with each value of the :code:`freq` repetition variable is known.
it evaluates a reverse polish notation expression. i. e. runs a stack machine.

tokens for this stack machine are integers (in decimal notation or in hexadecimal prefixed with :code:`0x` as in C and other programming languages) and operators.
there does not need to be whitespace separating a preceding number from a following operator. all other pairs of tokens should be separated by whitespace.

values
------

a value on the stack has a type, and usually also a payload value, whose meaning and representation is defined by its type.

currently defined types are:
*/
enum value_type {
/*
- the simplest type of value is a raw (natural) number.
  this is what an integer token evaluates to.

  default fields expect the value expression to evaluate to a single raw number.
*/
	VAL_NUMBER,
/*
- timing values have a type in the stack machine, and are produced by various operators.
  most such operators are unary, take a raw number and multiply their time unit with the input to produce their result.

  timing values are currently represented as unsigned integers in the unit of quartermillicycles, i. e. a 4000th of a cycle.

  timing fields (i. e. those with the < or > specifier flag character) expect the value expression to evaluate to a single timing value.
*/
	VAL_QMCYC,
/*
- to support (and enforce) parenthesizing expressions, there are 'values' of the 'parenthesis' type.
  they have no payload value and are produced exclusively by open-parenthesis operators. close-parenthesis-operators consume two values, check that the first is an open parenthesis and then push the second value as their result, regardless of its type or content (FIXME: check for empty parentheses)
*/
	VAL_PARENTHESIS,
/*
- relational operators produce boolean values. these are consumed as the third input of conditional selection.
*/
	VAL_BOOL,
};

struct value {
	enum value_type type;
	u64 val;
};
struct stack {DECL_VEC(struct value, values);};

struct context;
struct rpn_op {
	const char *tok;
	const char *(*op)(struct context *ctx, struct stack *stack, u64 param, const u8 *rep_values);
	u8 tok_len; u8 inputs;
	u64 param;
};

struct macro {
	const char *name, *value;
	size_t name_len, value_len;
};

struct context {
	DECL_VEC(struct macro, macros);
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
