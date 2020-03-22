#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <libusb-1.0/libusb.h>
#include "tools.h"

_Bool transfer(libusb_device_handle *handle, int fd, uint8_t *buf, size_t pos, uint16_t opcode) {
	u8 rc4state[258];
	rc4_init(rc4state);
	uint16_t crc = 0xffff;
	const uint16_t poly = 0x1021;
	_Bool eof = 0;
	while (!eof) {
		ssize_t res = read(fd, buf + pos, 4096 - pos);
		_Bool flush = 0;
		printf("read %zd\n", res);
		if (res == 0) {
			flush = 1;
			eof = 1;
		} else if (res < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {continue;}
			perror("Error while reading input file");
			exit(1);
		} else {
			pos += res;
			if (pos >= 4096) {flush = 1;}
		}
		if (flush) {
			printf("4096-byte 0x%x transfer (%zu bytes)\n", (unsigned)opcode, pos);
			memset(buf + pos, 0, 4096 - pos);
			rc4(buf, 4096, rc4state);
			for (size_t p = 0; p < 4096; ++p) {
				uint8_t bit = 0x80, byte = buf[p];
				do {
					crc = (crc << 1) ^ (((crc & 0x8000) != 0) ? poly : 0);
					crc ^= (bit & byte) != 0 ? poly : 0;
				} while(bit >>= 1);
			}
			if (libusb_control_transfer(handle, 0x40, 0xc, 0, opcode, buf, 4096, 100) != 4096) {
				fprintf(stderr, "error while sending data\n");
				return 2;
			}
			pos = 0;
		}
	}
	buf[0] = crc >> 8;
	buf[1] = (uint8_t)crc;
	printf("2-byte 0x%x transfer\n", (unsigned)opcode);
	ssize_t res = libusb_control_transfer(handle, 0x40, 0xc, 0, opcode, buf, 2, 1000);
	if (res != 2) {
		fprintf(stderr, "error while sending CRC: %zd (%s)\n", res, libusb_error_name((int)res));
		return 2;
	}
}

int main(int UNUSED argc, char UNUSED **argv) {
	libusb_context *ctx;
	if (libusb_init(&ctx)) {
		fprintf(stderr, "error initializing libusb\n");
		return 2;
	}
	libusb_device **list;
	ssize_t res;
	if ((res = libusb_get_device_list(ctx, &list)) < 0) {
		fprintf(stderr, "error listing devices: %zd (%s)\n", res, libusb_error_name(res));
		return 2;
	}
	libusb_device_handle *handle = 0;
	for (size_t pos = 0; list[pos]; ++pos) {
		struct libusb_device_descriptor devdesc;
		libusb_get_device_descriptor(list[pos], &devdesc);
		if (devdesc.idVendor == 0x2207 && devdesc.idProduct == 0x330c) {
			int res = libusb_open(list[pos], &handle);
			if (res) {
				fprintf(stderr, "error opening device: %d (%s)\n", res, libusb_error_name(res));
				return 2;
			}
			break;
		}
	}
	if (!handle) {
		fprintf(stderr, "no device found\n");
		return 2;
	}
	libusb_free_device_list(list, 1);
	uint8_t *buf = malloc(4096);
	if (!buf) {
		fprintf(stderr, "buffer allocation failed\n");
		abort();
	}
	for (char **arg = argv + 1; *arg; ++arg) {
		_Bool call = 0;
		if ((call = !strcmp("--call", *arg)) || !strcmp("--run", *arg)) {
			if (!*++arg) {
				fprintf(stderr, "%s needs a parameter\n", *--arg);
				return 1;
			}
			int fd = 0;
			if (strcmp("-", *arg)) {
				fd = open(*arg, O_RDONLY, 0);
				if (fd < 0) {
					fprintf(stderr, "cannot open %s\n", *arg);
					return 1;
				}
			}
			transfer(handle, fd, buf, 0, call ? 0x0471 : 0x0472);
			if (fd) {close(fd);}
		}
	}
	printf("done, closing USB handle\n");
	libusb_close(handle);
	libusb_exit(ctx);
}
