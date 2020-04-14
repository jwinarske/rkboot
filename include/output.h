#pragma once
#include <defs.h>

void puts(const char *);
_Noreturn int PRINTF(1, 2) die(const char *fmt, ...);
void PRINTF(1, 2) printf(const char *fmt, ...);

u64 get_timestamp();
#define log(fmt, ...) printf("[%zu] " fmt, get_timestamp(), __VA_ARGS__)
#define logs(str) printf("[%zu] %s", get_timestamp(), str)
#ifdef DEBUG_MSG
#define debug(fmt, ...) printf(fmt, __VA_ARGS__)
#define debugs(s) puts(s)
#else
#define debug(fmt, ...)
#define debugs(s)
#endif
