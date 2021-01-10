/* SPDX-License-Identifier: CC0-1.0 */
#pragma once

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define DECL_VEC(type, name) type *name; size_t name##_cap, name##_size
#define INIT_VEC(name) name = 0; name##_cap = name##_size = 0;
#define BUMP(name) ((++ name##_size < name##_cap ? 0 : (name = realloc(name, (name##_cap = (name##_cap ? name##_cap << 1 : 8)) * sizeof(*name)), allocation_failed), name ? 0 : allocation_failed()), name + (name##_size - 1))
_Noreturn int allocation_failed();
