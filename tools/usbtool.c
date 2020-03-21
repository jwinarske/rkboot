#include <stdio.h>
#include <string.h>
#include <libusb-1.0/libusb.h>
#include "tools.h"

#define POLY 0x1021
uint16_t crc = 0xffff;
uint8_t buf[4096];
u8 rc4state[258];

int main(int UNUSED argc, char UNUSED **argv) {
	rc4_init(rc4state);
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
	while (!feof(stdin) && !ferror(stdin)) {
		size_t read = fread(&buf[0], 1, 4096, stdin);
		memset(&buf[0] + read, 0, 4096 - read);
		rc4(buf, 4096, rc4state);
		for (size_t pos = 0; pos < 4096; ++pos) {
			uint8_t bit = 0x80, byte = buf[pos];
			do {
				crc = (crc << 1) ^ (((crc & 0x8000) != 0) ? POLY : 0);
				crc ^= (bit & byte) != 0 ? POLY : 0;
			} while(bit >>= 1);
		}
		if (libusb_control_transfer(handle, 0x40, 0xc, 0, 0x0471, &buf[0], 4096, 100) != 4096) {
			fprintf(stderr, "error while sending data\n");
			return 2;
		}
	}
	buf[0] = crc >> 8;
	buf[1] = (uint8_t)crc;
	if ((res = libusb_control_transfer(handle, 0x40, 0xc, 0, 0x0471, &buf[0], 2, 1000)) != 2) {
		fprintf(stderr, "error while sending CRC: %zd (%s)\n", res, libusb_error_name((int)res));
		return 2;
	}
	libusb_close(handle);
	libusb_exit(ctx);
}
