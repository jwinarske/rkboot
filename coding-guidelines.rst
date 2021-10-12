.. SPDX-License-Identifier: CC0-1.0
levinboot Coding Guidelines
===========================

Note that, as of now, many of these guidelines are widely violated within the code base.
This document is at this point only definition of goals, so new code should conform to these guidelines, but old code may not.

Code organization and global names
----------------------------------

levinboot code is divided into three rough layers: generic, architecture and platform.
The term `facilities` is used as a general term for notions such as types, constants, functions or more general linker symbols.
Facilities may be namespaced, typically by prefixing their name.
Header files declaring these facilities should also have this prefix in the include path.
Some of these namespaces are described in the layer definitions below.

Generic code
    Code that is (at least in theory) portable between CPU architectures and may possibly even work in a hosted environment. Types, constants and functions that need to have architecture- or platform-specific implementations but should be widely available may be referenced from architecture or platform code in the namespaces `arch` and `plat` respectively.

Architecture code
    Code that is portable between platforms but specific to a CPU architecture (in a broad sense), including non-platform-specific assembly code (e. g. register save/restore helpers), MMU code and interrupt handling code (e. g. IRQ masking, interrupt routing).
    This includes code for specific CPU cores or interrupt controllers used in multiple platforms.
    May implement facilities in the `arch` namespace used by generic code.
    Code implementing facilities that are not available, or substantially differ, on other architectures should use either the architecture's own namespace or a more specific one (e. g. for the specific interrupt controller or CPU core).
    Global assembly symbols that do not conform to a normal calling convention should be namespaced under the prefix `asm`.
    This code may, in addition to all namespaces it implements, reference generic code directly.
    Platform facilities can be accessed through the `plat` namespace.

Platform code
    Chip- or board-specific code.
    May implement facilities in the `plat` namespace.
    It does not need to conform to strict namespacing rules (as long as it does not infringe on other namespaces used on this platform), though for better organization namespacing is still useful, e. g. if the code is in different directories.

`lib/`
    generic code

`drivers/`
    drivers for hardware available on multiple platforms and which fulfill the standards of generic code.

`aarch64/`
    architecture code for aarch64 and, for now, other ARM facilities.

    Namespaces:

    `aarch64`
        namespace for everything that does not fit elsewhere
    `gicv[23]`
        interrupt controllers
    `mmu`
        page tables and other MMU handling

Code Style – C code
-------------------

Use good judgement – violating the rules in this section may very well be okay if the alternative is worse.

Comments
^^^^^^^^

Comments use C++/C99 style (`//`), also for multi-line comments. This avoids gnarly questions about how to begin subsequent lines.

Comments can go on the same line as the code they are annotating (in which case the two are separated with a single tab character), as long the line can fit the entire comment without becoming overly long.
Otherwise move the comment to the preceding or following line, as appropriate.

DEFINE\_ pattern
^^^^^^^^^^^^^^^

Many times one wants to define an enumeration and be able to pretty-print its values, or perhaps associate some other value with the name.
In levinboot, the way to achieve this is using the `DEFINE\_` pattern::

    #define DEFINE_MY_ENUM X(A, 42) X(B, 1337)
    enum my_enum {
    #define X(name, value)
        DEFINE_MY_ENUM
    #undef X
        NUM_MY_ENUM
    };

    const char my_enum_names[NUM_MY_ENUM][2] = {
    #define X(name, value) #name,
        DEFINE_MY_ENUM
    #undef X
    };
    const unsigned my_enum_values[NUM_MY_ENUM] = {
    #define X(name, value) value,
        DEFINE_MY_ENUM
    #undef X
    };

This pattern makes perhaps obscure use of the C preprocessor's evaluation strategy.

Unless the names vary wildly in length (or are generally much longer than pointers), the "names" array should use character arrays instead of pointers to strings, for efficient lookup.

The :code:`NUM\_` enumerator is an exception to the `Trailing commata`_ rule, since it is expected to be the final entry for the entire life-cycle of the enum.

Includes
^^^^^^^^

Includes are grouped into 3 categories, each of which is sorted (collation: /, -, \_, letters, numbers, others – in Unicode code point order; please don't get too creative with filenames):

1. This module

  Includes that declare the functions that this source file implements, or tightly related code.

2. Standard library

3. Other levinboot headers

If all three categories are present, the second should be separated from the third one by an empty line.

Indentation
^^^^^^^^^^^

Indentation uses tabs, always.
There is no space alignment within a function call.

If a pair of delimiters (`()`, `[]` or `{}`) is broken onto multiple lines (regardless of whether they form a function call, a statement block, a data type definition, an initializer list or something else), the indentation is increased by one tab for all lines (strictly) between the ones with the opening delimiter and the closing delimiter on them.
If multiple delimiter pairs are broken up that have all of their opening delimiters on the same line and all of their closing delimiters on the same line, the indent is still only one line.

Line length
^^^^^^^^^^^

There is a soft limit at 79 columns and a hard limit at 120 columns.
Tabs count for 8 columns each.

In the future there should be additional guidelines accounting for proportional fonts.

Local variables
^^^^^^^^^^^^^^^

Wherever reasonably possible, the definition of a variable should also be the initialization.

A typical exception to this would be a variable that gets initialized depending on an `if` condition, with the result of a complicated and/or side-effectful computation::

    unsigned x;
    if (condition) {
        /* compute something */
        x = expression;
    } else {
        /* compute something else */
        x = other_expression;
    }

Statement blocks
^^^^^^^^^^^^^^^^

`if` and loop statements always use braces around their body (including the `else` branch if applicable).
The opening brace always goes on the same line as the previous token, the closing brace always on the same line as the next token (if applicable).

If it would not make the line significantly overlength, the body does not even need to be broken onto multiple lines. The following is considered acceptible::

    do {
        if (condition) {continue;}
        while (other_condition) {*ptr++ = value;}
    } while (yet_other_condition);

Generally, if a block is broken onto multiple lines, the last line should only contain no content of the block (only the closing brace and potentially following parts of the surrounding statement) and the opening line should contain no body of the block, except possibly a comment.

An exception to this is when a block is not used as the body of a conditional/loop construct or a function definition, but to limit the scope of a single variable.
In this case, the initialization can go on the opening line of the block and a cleanup statement related to the variable can go on the closing line::

    {irq_save_t irq = irq_lock(&strct->lock);
        /* do something with data protected by the lock */
    irq_unlock(&strct->lock, irq);}

Trailing commata
^^^^^^^^^^^^^^^^

Items in initializer lists for arrays should generally use a trailing comma, such that the list can be extended without modifying the last line, keeping the diff cleaner.

The same goes for enumerators, except where no enumerator can sensibly be inserted after the currently-last one, e. g. because it is the highest bit in an enumeration of register bits, or the :code:`NUM\_` entry in `DEFINE\_ pattern`_.
