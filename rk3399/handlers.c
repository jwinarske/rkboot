/* SPDX-License-Identifier: CC0-1.0 */
#include <inttypes.h>

#include <die.h>

#include <arch/context.h>

#if CONFIG_EL == 3
#define EL "3"
#elif CONFIG_EL == 2
#define EL "2"
#elif CONFIG_EL == 1
#define EL "1"
#endif

static void sync_exc_handler(u64 elr, const char *label) {
	u64 esr, far;
	__asm__("mrs %0, esr_el"EL"; mrs %1, far_el"EL : "=r"(esr), "=r"(far));
	die("%s@0x%"PRIx64": ESR=0x%"PRIx64", FAR=0x%"PRIx64"\n", label, elr, esr, far);
}

void plat_handler_sync_thread(u64 elr) {
	sync_exc_handler(elr, "thread sync exc");
}
void plat_handler_sync_cpu(u64 elr) {
	sync_exc_handler(elr, "CPU sync exc");
}
void plat_handler_sync_aarch64(u64 elr) {
	sync_exc_handler(elr, "Aarch64 sync exc");
}
void plat_handler_sync_aarch32(u64 elr) {
	sync_exc_handler(elr, "Aarch32 sync exc");
}

static void serror_handler(const char *label) {
	u64 esr, far;
	__asm__("mrs %0, esr_el"EL"; mrs %1, far_el"EL : "=r"(esr), "=r"(far));
	die("%s: ESR=0x%"PRIx64", FAR=0x%"PRIx64"\n", label, esr, far);
}
void plat_handler_serror_same() {
	serror_handler("EL"EL" SError");
}
void plat_handler_serror_lower() {
	serror_handler("guest SError");
}

