/* SPDX-License-Identifier: CC0-1.0 */
#include <stage.h>

u32 end_sramstage(struct stage_store *store) {
	stage_teardown(store);
	return 0;
}