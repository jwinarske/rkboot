#pragma once

#ifdef ENV_STAGE
#define ENTRY __attribute__((section(".entry")))
#else
#define ENTRY
#endif

#define PRINTF(str_idx, start) __attribute__((format(printf, str_idx, start)))

/* must be shorter than 64 bytes */
#define CONFIG_GREETING "levinboot/0.1\r\n"

/* base clock 1.5MHz */
#define CONFIG_UART_CLOCK_DIV 1
