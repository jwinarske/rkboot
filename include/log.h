/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <stdio.h>

#ifndef FREESTANDING_STDIO
	#ifdef DEBUG_MSG
	#define debug(...) fprintf(stderr, __VA_ARGS__)
	#define debugs(...) fputs(__VA_ARGS__,  stderr)
	#endif

	#ifndef NO_INFO_MSG
	#define info(...) fprintf(stderr, __VA_ARGS__)
	#define infos(...) fputs(__VA_ARGS__,  stderr)
	#endif
#else
	#ifdef DEBUG_MSG
	#define debug(...) printf(__VA_ARGS__)
	#define debugs(...) puts(__VA_ARGS__)
	#endif

	#ifndef NO_INFO_MSG
	#define info(...) printf(__VA_ARGS__)
	#define infos(...) puts(__VA_ARGS__)
	#endif
#endif

#ifndef DEBUG_MSG
#define debug(...)
#define debugs(...)
#endif
#ifdef NO_INFO_MSG
#define info(...)
#define infos(...)
#endif
