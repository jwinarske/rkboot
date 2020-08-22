/* SPDX-License-Identifier: CC0-1.0 */
#pragma once

#define DEFINE_IOST\
	X(OK)\
	X(TRANSIENT)	/* transient fault: the same request may be tried again */\
	X(INVALID)	/* invalid request: the request is malformed or otherwise permanently failed, but the hardware is operating normally */\
	X(LOCAL)	/* local fault: the same request should not be tried again, but others on the same device may still work */\
	X(GLOBAL)	/* global fault: the device may be in a completely broken state */

enum iost {
#define X(name) IOST_##name,
	DEFINE_IOST
#undef X
	NUM_IOST
};
