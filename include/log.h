/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <stdio.h>
#include <inttypes.h>

#ifndef FREESTANDING_STDIO
	#ifdef SPEW_MSG
	#define spew(...) fprintf(stderr, __VA_ARGS__)
	#define spews(...) fputs(__VA_ARGS__,  stderr)
	#endif
	#ifdef DEBUG_MSG
	#define debug(...) fprintf(stderr, __VA_ARGS__)
	#define debugs(...) fputs(__VA_ARGS__,  stderr)
	#endif

	#ifndef NO_INFO_MSG
	#define info(...) fprintf(stderr, __VA_ARGS__)
	#define infos(...) fputs(__VA_ARGS__,  stderr)
	#endif
#else
	#ifdef SPEW_MSG
	#define spew(...) printf(__VA_ARGS__)
	#define spews(...) printf("%s", __VA_ARGS__)
	#endif
	#ifdef DEBUG_MSG
	#define debug(...) printf(__VA_ARGS__)
	#define debugs(...) printf("%s", __VA_ARGS__)
	#endif

	#ifndef NO_INFO_MSG
	#define info(...) printf(__VA_ARGS__)
	#define infos(...) printf("%s", __VA_ARGS__)
	#endif
#endif

#ifndef SPEW_MSG
#define spew(...)
#define spews(...)
#endif
#ifndef DEBUG_MSG
#define debug(...)
#define debugs(...)
#endif
#ifdef NO_INFO_MSG
#define info(...)
#define infos(...)
#endif
