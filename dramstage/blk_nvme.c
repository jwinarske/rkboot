/* SPDX-License-Identifier: CC0-1.0 */
#include <rk3399/dramstage.h>

#include <rkpcie_regs.h>
#include <log.h>
#include <runqueue.h>
#include <timer.h>

void boot_nvme() {
	timestamp_t start = get_timestamp();
	volatile u32 *pcie_client = regmap_base(DRAMSTAGE_REGMAP_PCIE_CLIENT);
	while (1) {
		u32 basic_status1 = pcie_client[RKPCIE_CLIENT_BASIC_STATUS+1];
		info("PCIe link status %08"PRIx32" %08"PRIx32"\n", pcie_client[RKPCIE_CLIENT_DEBUG_OUT+0], basic_status1);
		if ((basic_status1 >> 20 & 3) == 3) {break;}
		usleep(1000);
	}
	info("waited %"PRIuTS" μs for PCIe link\n", (get_timestamp() - start) / TICKS_PER_MICROSECOND);
	boot_medium_exit(BOOT_MEDIUM_NVME);
}
