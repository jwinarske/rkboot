#pragma once
#include <defs.h>
#include <async.h>

extern struct async_transfer spi1_async;

struct rkspi;
void rkspi_read_flash_poll(volatile struct rkspi *spi, u8 *buf, size_t buf_size, u32 addr);
void rkspi_start_irq_flash_read(u32 addr);
void rkspi_end_irq_flash_read();
