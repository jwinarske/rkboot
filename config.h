#pragma once

#ifdef ENV_STAGE
#define ENTRY __attribute__((section(".entry")))
#else
#define ENTRY
#endif

/* must be shorter than 64 bytes */
#define CONFIG_GREETING "levinboot/0.1\r\n"

/* base clock 1.5MHz */
#define CONFIG_UART_CLOCK_DIV 1
