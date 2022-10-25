// SPDX-License-Identifier: CC0-1.0
#pragma once
#include <stddef.h>

typedef struct {
	char *buf;
	size_t size;
	_Bool mmapped;
} LoadFile;

_Bool loadFile(const char *name, LoadFile *res);
void unloadFile(LoadFile *file);
