#include "main.h"

_Noreturn void unimplemented(const char *description) {
	printf("ERROR: unimplemented: %s\n", description);
	halt_and_catch_fire();
}
