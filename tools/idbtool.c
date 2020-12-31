/* SPDX-License-Identifier: CC0-1.0 */
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "tools.h"

void write_buf(const u8 *buf, size_t size) {
	size_t pos = 0;
	while (pos < size) {
		ssize_t res = write(1, buf + pos, size - pos);
		if (!res) {break;}
		if (res < 0) {
			if (errno != EINTR && errno != EAGAIN) {
				dprintf(2, "write error\n");
				exit(1);
			}
		} else {
			pos += res;
		}
	}
}

enum mode {
	MODE_HELP,
	MODE_SD_IMAGE,
	MODE_SPI_IMAGE,
	MODE_DECRYPT,
};

int main(int argc, char **argv) {
	u8 rc4_state[258];
	rc4_init(&rc4_state[0]);
	enum mode mode = MODE_HELP;
	if (argc == 1) {
		mode = MODE_SD_IMAGE;
	} else if (argc == 2) {
		if (0 == strcmp(argv[1], "spi")) {
			mode = MODE_SPI_IMAGE;
		} else if (0 == strcmp(argv[1], "sd")){
			mode = MODE_SD_IMAGE;
		} else if (0 == strcmp(argv[1], "decrypt")){
			mode = MODE_DECRYPT;
		}
	}
	switch (mode) {
	case MODE_HELP:
		printf("usage: %s [sd | spi | decrypt | help]\n\n", argv[0]);
		puts("Reads from stdin. Operating modes:\n\n"
			"* sd (default): produces an ID block for the RK33xx chips with a single following bootloader image (which is read from stdin), in the format used on SD or eMMC.\n"
			"* spi: produces an ID block for the RK33xx chips with a single following bootloader image (which is read from stdin), in the format used on SPI flash.\n"
			"* decrypt: reads 512 bytes of stdin and XORs them with the well-known RC4 cipher stream used for ID blocks.\n"
			"* help: displays this help.\n"
		);
		return 0;
	case MODE_DECRYPT:{
		u8 buf[512];
		memset(&buf[0], 0, 512);
		size_t size = 0;
		while (size < 512) {
			ssize_t res = read(0, buf + size, 512 - size);
			if (!res) {break;}
			if (res < 0) {
				if (errno != EINTR && errno != EAGAIN) {
					dprintf(2, "read error\n");
					return 1;
				}
			} else {
				size += res;
			}
		}
		rc4(&buf[0], 512, &rc4_state[0]);
		write_buf(&buf[0], 512);
		break;
	}
	case MODE_SD_IMAGE:
	case MODE_SPI_IMAGE:{
		u8 padding[2048];
		memset(padding, 0, sizeof(padding));
		u8 *buf = malloc(1 << 22);
		if (!buf) {dprintf(2, "allocation failed\n"); return 1;}
		size_t size = 0;
		while (1) {
			if (size == 1 << 22) {dprintf(2, "image too big\n");}
			ssize_t res = read(0, buf + size, (1 << 22) - size);
			if (!res) {break;}
			if (res < 0) {
				if (errno != EINTR && errno != EAGAIN) {
					dprintf(2, "read error\n");
					return 1;
				}
			} else {
				size += res;
			}
		}
		dprintf(2, "read %zu bytes\n", size);
		const size_t data_sectors = (size + 4 + 511) / 512;
		const size_t pages = (data_sectors + 3) / 4;
		const size_t sectors = pages * 4;
		assert(sectors < (1 << 16));
		u8 idblock[2048];
		memset(&idblock, 0, 2048);
		idblock[0] = 0x55; idblock[1] = 0xaa; idblock[2] = 0xf0; idblock[3] = 0x0f; /* magic number */
		idblock[8] = 1; /* disable encryption of bootblock */
		idblock[12] = 4;
		idblock[14] = 4;
		idblock[506] = sectors & 0xff; idblock[507] = sectors >> 8;
		idblock[508] = sectors & 0xff; idblock[509] = sectors >> 8;
		rc4(&idblock[0], 512, &rc4_state[0]);
		write_buf(&idblock[0], 2048);
		if (mode == MODE_SD_IMAGE) {
			write_buf(buf, size);
		} else {
			assert(mode == MODE_SPI_IMAGE);
			write_buf(padding, sizeof(padding));
			if (size <= 2048) {
				write_buf(buf, size);
				return 0;
			}
			write_buf(buf, 2048);
			write_buf(padding, sizeof(padding));
			size_t pos = 2048;
			if (size >= 2048) {while (1) {
				if (pos >= size - 2048) {break;}
				write_buf(buf + pos, 2048);
				pos += 2048;
				write_buf(padding, sizeof(padding));
			}}
			write_buf(buf + pos, size - pos);
		}
		break;
	}}
	return 0;
}
