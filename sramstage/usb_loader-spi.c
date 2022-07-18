/* SPDX-License-Identifier: CC0-1.0 */
#include <rk3399/sramstage.h>
#include <inttypes.h>
#include <assert.h>

#include <rk3399.h>
#include <rkspi.h>
#include <rkspi_regs.h>
#include <dump_mem.h>
#include <die.h>
#include <aarch64.h>

static volatile struct rkspi_regs *const spi1 = regmap_spi1;

static void read_sfdp(u32 addr, u8 *buf, size_t size) {
	assert(!(addr >> 24));
	spi1->slave_enable = 1;
	rkspi_tx_cmd4_dummy1(spi1, 0x5a << 24 | addr);
	rkspi_recv_fast(spi1, buf, size);
	spi1->slave_enable = 0;
}

static void tx_cmd(const u8 *buf, const u8 *end) {
	spi1->slave_enable = 1;
	spi1->ctrl0 = rkspi_mode_base | RKSPI_XFM_TX | RKSPI_BHT_APB_8BIT;
	spi1->enable = 1;
	u32 fifo_left = 32;
	while (buf < end) {
		if (!fifo_left) {
			fifo_left = 32 - spi1->tx_fifo_level;
		}
		spi1->tx = *buf++;
		fifo_left -= 1;
	}
	while (spi1->status & 1) {__asm__ volatile("yield");}
	spi1->enable = 0;
	spi1->slave_enable = 0;
}

static void wait_until_ready() {
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
		putchar('.');
	}
	spi1->enable = 0;
	spi1->slave_enable = 0;
}

void sramstage_usb_flash_spi(const u8 *buf, u64 start, u64 length) {
	static volatile u32 *const cru = regmap_cru;
	cru[CRU_CLKGATE_CON+23] = SET_BITS16(1, 0) << 11;
	/* clk_spi1 = CPLL/8 = 100 MHz */
	cru[CRU_CLKSEL_CON+59] = SET_BITS16(1, 0) << 15 | SET_BITS16(7, 7) << 8;
	dsb_st();
	cru[CRU_CLKGATE_CON+9] = SET_BITS16(1, 0) << 13;
	spi1->baud = 2;
	u8 sfdp[1024];
	read_sfdp(0, sfdp, 8);
	assert_msg(sfdp[0] == 'S' && sfdp[1] == 'F' && sfdp[2] == 'D' && sfdp[3] == 'P', "SPI flash not present or not SFDP-compatible\n");
	assert_msg(sfdp[4] == 0 && sfdp[5] != 0, "SFDP version not compatible\n");
	u32 num_headers = (u32)sfdp[6] + 1;
	printf("%"PRIu32" SFDP parameter headers\n", num_headers);

	struct erase_op {u8 shift, opcode;} erase_ops[16] = {};
	u8 num_erase_ops = 0;
	for_range(i, 0, num_headers) {
		read_sfdp(8 + i * 8, sfdp, 8);
		u8 id = sfdp[0], rev_major = sfdp[1], rev_minor = sfdp[2], length = sfdp[3];
		u32 ptp = sfdp[4] | (u32)sfdp[5] << 8 | (u32)sfdp[6] << 16;
		read_sfdp(ptp, sfdp, length * 4);
		if (id == 0) {
			printf("JEDEC parameter table %"PRIu8".%"PRIu8": address 0x%"PRIx32"–0x%"PRIx32"\n", rev_major, rev_minor, ptp, ptp + (u32)length * 4);
			dump_mem(sfdp, length * 4);
			if (rev_major != 0 || rev_minor == 0 || length < 9) {
				puts("\tnot compatible, skipping …");
				continue;
			}
			for_range(j, 0, 4) {
				u8 shift = sfdp[0x1c + j * 2], op = sfdp[0x1d + j * 2];
				if (shift) {
					printf("2^%"PRIu8"-byte erase: %02"PRIx8"\n", shift, op);
					if (num_erase_ops < ARRAY_SIZE(erase_ops) && shift <= 16) {
						puts("\tusable");
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

	assert(length <= 0x01000000 - start);
	u64 write_ptr = start, erased_until = start, end = start + length;
	printf("Flashing SPI: start %"PRIx64" end %"PRIx64"\n", start, end);
	assert(write_ptr <= erased_until);
	u8 wren = 6;
	while (end > erased_until) {
		u32 p = num_erase_ops - 1;
		while (1) {
			u32 size = 1 << erase_ops[p].shift, mask = size - 1;
			if ((erased_until & mask) == 0 && erased_until + size <= end) {break;}
			if (p == 0) {
				assert((erased_until & mask) == 0);
				break;
			}
			p -= 1;
		}
		u32 erase_cmd = (u32)erase_ops[p].opcode << 24 | erased_until;
		erased_until += 1 << erase_ops[p].shift;
		printf("erase command %08"PRIx32" → %"PRIx64" ", erase_cmd, erased_until);
		tx_cmd(&wren, &wren + 1);
		u8 cmd_buf[4] = {erase_cmd >> 24, erase_cmd >> 16, erase_cmd >> 8, erase_cmd};
		tx_cmd(cmd_buf, cmd_buf + 4);
		wait_until_ready();
		puts(" erased.");
	}
	const u8 *read_ptr = buf;
	u8 _Alignas(16) cmd_buf[260];
	cmd_buf[0] = 2;
	while ((write_ptr & 0xffff00) != ((end - 1) & 0xffff00)) {
		printf("%"PRIx64":", write_ptr);
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
		putchar(' ');
	}
	if (write_ptr != end) {
		cmd_buf[1] = write_ptr >> 16;
		cmd_buf[2] = write_ptr >> 8;
		cmd_buf[3] = write_ptr;
		u32 len = 4;
		do {
			cmd_buf[len++] = *read_ptr++;
		} while (++write_ptr < end);
		tx_cmd(&wren, &wren + 1);
		printf("trailing %"PRIu32, len);
		tx_cmd(cmd_buf, cmd_buf + len);
		wait_until_ready();
	}
	puts(" programmed.");
	printf("written until %"PRIx64"\n", write_ptr);
	cru[CRU_CLKGATE_CON+9] = SET_BITS16(1, 1) << 13;
}
