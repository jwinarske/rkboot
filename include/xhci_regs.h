/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <defs.h>

struct xhci_trb {
	_Alignas(16)
	u64 param;
	u32 status;
	u32 control;
};
