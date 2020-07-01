/* SPDX-License-Identifier: CC0-1.0 */
#include <inttypes.h>
#include <rk3399.h>
#include <mmu.h>
#include <stage.h>
#include <exc_handler.h>
#include <uart.h>
#include <die.h>
#include <rkspi.h>
#include <rkspi_regs.h>
#include "rk3399_spi.h"
#include <dump_mem.h>

static const struct mapping initial_mappings[] = {
	MAPPING_BINARY,
	{.first = 0, .last = 0xfff, .flags = MEM_TYPE_NORMAL},
	{.first = 0xf8000000, 0xff1cffff, .flags = MEM_TYPE_DEV_nGnRnE},
	{.first = 0xff1e0000, 0xff8bffff, .flags = MEM_TYPE_DEV_nGnRnE},
	{.first = 0xff8c0000, .last = 0xff8effff, .flags = MEM_TYPE_UNCACHED},
	{.first = 0xff8f0000, .last = 0xfffeffff, .flags = MEM_TYPE_DEV_nGnRnE},
	{.first = 0xffff0000, .last = 0xffffffff, .flags = MEM_TYPE_NORMAL},
	{.first = 0, .last = 0, .flags = 0}
};

static const struct address_range critical_ranges[] = {
	{.first = __start__, .last = __end__ - 1},
	{.first = uart, .last = uart},
	ADDRESS_RANGE_INVALID
};

struct erase_op {u8 shift, opcode;} erase_ops[16] = {};
u8 num_erase_ops = 0;

u32 write_ptr = 0, erased_until = 0;

static void UNUSED tx_cmd(const u8 *buf, const u8 *end) {
	spi1->slave_enable = 1;
	spi1->ctrl0 = rkspi_mode_base | RKSPI_XFM_TX | RKSPI_BHT_APB_8BIT;
	spi1->enable = 1;
	u32 fifo_left = 32;
	while (buf < end) {
		if (!fifo_left) {
			fifo_left = 32 - spi1->tx_fifo_level;
			//printf("<%"PRIu32">", fifo_left);
		}
		spi1->tx = *buf++;
		fifo_left -= 1;
	}
	while (spi1->status & 1) {__asm__ volatile("yield");}
	spi1->enable = 0;
	spi1->slave_enable = 0;
}

static void UNUSED wait_until_ready() {
	spi1->slave_enable = 1;
	spi1->ctrl0 = rkspi_mode_base | RKSPI_XFM_TR | RKSPI_BHT_APB_8BIT;
	spi1->enable = 1;
	spi1->tx = 5;
	spi1->tx = 0xff;
	while (spi1->rx_fifo_level < 2) {__asm__("yield");}
	spi1->rx;
	while (spi1->rx & 1) {
		spi1->tx = 0xff;
		while (!spi1->rx_fifo_level) {__asm__("yield");}
		puts(".");
	}
	spi1->enable = 0;
	spi1->slave_enable = 0;
}

void xfer_handler(u64 *buf, size_t len, u16 cmd, struct exc_state_save UNUSED *save) {
	puts("xfer\n");
	u64 load_addr = buf[0], end_addr = buf[1];
	assert(load_addr < 0x1000000 && end_addr <= 0x1000000);
	if (load_addr != write_ptr) {
		erased_until = write_ptr = (u32)load_addr;
	}
	printf("start %"PRIx32" end %"PRIx32" len %zx, erased until %"PRIx32"\n", (u32)load_addr, (u32)end_addr, len, erased_until);
	assert(end_addr > load_addr && end_addr - load_addr <= len * 8 - 16);
	assert(write_ptr <= erased_until);
	u8 wren = 6;
	while (end_addr > erased_until) {
		u32 p = num_erase_ops - 1;
		while (1) {
			u32 size = 1 << erase_ops[p].shift, mask = size - 1;
			if ((erased_until & mask) == 0 && erased_until + size <= end_addr) {break;}
			if (p == 0) {
				assert((erased_until & mask) == 0);
				break;
			}
			p -= 1;
		}
		u32 erase_cmd = (u32)erase_ops[p].opcode << 24 | erased_until;
		erased_until += 1 << erase_ops[p].shift;
		printf("erase command %08"PRIx32" → %"PRIx32" ", erase_cmd, erased_until);
		tx_cmd(&wren, &wren + 1);
		u8 cmd_buf[4] = {erase_cmd >> 24, erase_cmd >> 16, erase_cmd >> 8, erase_cmd};
		tx_cmd(cmd_buf, cmd_buf + 4);
		wait_until_ready();
		puts(" erased.\n");
	}
	const u8 *read_ptr = (const u8 *)(buf + 2);
	u8 _Alignas(16) cmd_buf[260];
	cmd_buf[0] = 2;
	while ((write_ptr & 0xffff00) != ((end_addr - 1) & 0xffff00)) {
		printf("%"PRIx32":", write_ptr);
		cmd_buf[1] = write_ptr >> 16;
		cmd_buf[2] = write_ptr >> 8;
		cmd_buf[3] = write_ptr;
		u32 len = 4;
		do {
			cmd_buf[len++] = *read_ptr++;
		} while (++write_ptr & 0xff);
		tx_cmd(&wren, &wren + 1);
		printf("%"PRIu32, len);
		tx_cmd(cmd_buf, cmd_buf + len);
		wait_until_ready();
		//dump_mem(cmd_buf, len);
		puts(" ");
	}
	if (write_ptr != end_addr) {
		cmd_buf[1] = write_ptr >> 16;
		cmd_buf[2] = write_ptr >> 8;
		cmd_buf[3] = write_ptr;
		u32 len = 4;
		do {
			cmd_buf[len++] = *read_ptr++;
		} while (++write_ptr < end_addr);
		//dump_mem(cmd_buf, len);
		tx_cmd(&wren, &wren + 1);
		printf("trailing %"PRIu32, len);
		tx_cmd(cmd_buf, cmd_buf + len);
		wait_until_ready();
	}
	puts(" programmed.");
	printf("written until %"PRIx32"\n", write_ptr);
	if (cmd == 0x0472) {
		rk3399_spi_teardown();
		die("finished.\n");
	}
}

void rk3399_spi_setup();

static void read_sfdp(u32 addr, u8 *buf, size_t size) {
	assert(!(addr >> 24));
	spi1->slave_enable = 1;
	rkspi_tx_cmd4_dummy1(spi1, 0x5a << 24 | addr);
	rkspi_recv_fast(spi1, buf, size);
	spi1->slave_enable = 0;
}

void patch_brom();

u32 ENTRY main() {
	puts("spi-flasher\n");
	struct stage_store store;
	stage_setup(&store);
	mmu_setup(initial_mappings, critical_ranges);
	rk3399_spi_setup();
	u8 sfdp[1024];
	read_sfdp(0, sfdp, 8);
	assert_msg(sfdp[0] == 'S' && sfdp[1] == 'F' && sfdp[2] == 'D' && sfdp[3] == 'P', "SPI flash not present or not SFDP-compatible\n");
	assert_msg(sfdp[4] == 0 && sfdp[5] != 0, "SFDP version not compatible\n");
	u32 num_headers = (u32)sfdp[6] + 1;
	printf("%"PRIu32" SFDP parameter headers\n", num_headers);
	for_range(i, 0, num_headers) {
		read_sfdp(8 + i * 8, sfdp, 8);
		u8 id = sfdp[0], rev_major = sfdp[1], rev_minor = sfdp[2], length = sfdp[3];
		u32 ptp = sfdp[4] | (u32)sfdp[5] << 8 | (u32)sfdp[6] << 16;
		read_sfdp(ptp, sfdp, length * 4);
		if (id == 0) {
			printf("JEDEC parameter table %"PRIu8".%"PRIu8": address 0x%"PRIx32"–0x%"PRIx32"\n", rev_major, rev_minor, ptp, ptp + (u32)length * 4);
			dump_mem(sfdp, length * 4);
			if (rev_major != 0 || rev_minor == 0 || length < 9) {
				puts("\tnot compatible, skipping …\n");
				continue;
			}
			for_range(j, 0, 4) {
				u8 shift = sfdp[0x1c + j * 2], op = sfdp[0x1d + j * 2];
				if (shift) {
					printf("2^%"PRIu8"-byte erase: %02"PRIx8"\n", shift, op);
					if (num_erase_ops < ARRAY_SIZE(erase_ops) && shift <= 16) {
						puts("\tusable\n");
						erase_ops[num_erase_ops].shift = shift;
						erase_ops[num_erase_ops++].opcode = op;
					}
				}
			}
		} else {
			printf("0x%02"PRIx8" parameter table %"PRIu8".%"PRIu8": address 0x%"PRIx32"–0x%"PRIx32"\n", sfdp[0], rev_major, rev_minor, ptp, ptp + (u32)length * 4);
			dump_mem(sfdp, length * 4);
		}
	}
	assert(num_erase_ops);
	for_range(i, 1, num_erase_ops) {
		struct erase_op tmp = erase_ops[i];
		u32 p = i;
		while (p-- > 0 && erase_ops[p].shift > tmp.shift) {
			erase_ops[p] = erase_ops[p + 1];
		}
		erase_ops[p + 1] = tmp;
	}
	for_range(i, 0, num_erase_ops) {printf("%02"PRIx8": %"PRIu8"\n", erase_ops[i].opcode, erase_ops[i].shift);}
	/*rkspi_read_flash_poll(spi1, sfdp, 1024, 0);
	dump_mem(sfdp, 1024);
	die("end.\n");*/
	patch_brom();
	puts("ready to flash\n");
	return 0;
}


