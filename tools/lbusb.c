// SPDX-License-Identifier: CC0-1.0
#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libusb.h>

#include <levinboot/rc4.h>
#include <hw/usb.h>

#include "load_file.h"

#define ENUM_LBUSB_STATE(X) X(NO_DEVICE) X(RK_MASKROM)

typedef enum {
#define X(n) LBUSB_##n,
	ENUM_LBUSB_STATE(X)
#undef X
	NUM_LBUSB_STATE
} LbusbState;

const char lbusb_state_names[NUM_LBUSB_STATE][16] = {
#define X(n) #n,
	ENUM_LBUSB_STATE(X)
#undef X
};

typedef struct {
	LbusbState st;
	libusb_context *usb_ctx;
	libusb_device *usb_device;
	libusb_device_handle *usb_handle;
} LbusbContext;

static void usage(const char *name) {
	fprintf(stderr,
"Usage: %s [commands]\n\n"
"Available commands:\n"
"  help  display this help text and exit\n"
"  rk3399  connect to RK3399 maskrom\n",
		name
	);
}

static int help(const char *name) {
	fprintf(stderr, "%s – USB bootloader development helper tool\n", name);
	usage(name);
	return 0;
}

static int connectRk3399(LbusbContext *ctx) {
	if (!ctx->usb_ctx) {
		if (libusb_init(&ctx->usb_ctx)) {
			fprintf(stderr, "error initializing libusb\n");
			return 2;
		}
	}
	libusb_device **list;
	ssize_t err;
	if ((err = libusb_get_device_list(ctx->usb_ctx, &list)) < 0) {
		fprintf(stderr, "error listing devices: %zd (%s)\n", err, libusb_error_name(err));
		return 2;
	}
	for (size_t pos = 0; list[pos]; ++pos) {
		struct libusb_device_descriptor devdesc;
		libusb_get_device_descriptor(list[pos], &devdesc);
		if (devdesc.idVendor == 0x2207 && devdesc.idProduct == 0x330c) {
			int err = libusb_open(list[pos], &ctx->usb_handle);
			ctx->usb_device = list[pos];
			ctx->st = LBUSB_RK_MASKROM;
			if (err) {
				fprintf(stderr, "error opening device: %d (%s)\n", err, libusb_error_name(err));
				return 2;
			}
			break;
		}
	}
	libusb_free_device_list(list, 1);
	if (ctx->st != LBUSB_RK_MASKROM) {
		fprintf(stderr, "No RK3399 in mask ROM mode (USB 2207:330c) found\n");
		return 3;
	}
	return 0;
}

static uint16_t crc16(uint8_t *buf, size_t len, uint16_t crc, uint16_t poly) {
	for (size_t p = 0; p < len; ++p) {
		uint8_t bit = 0x80, byte = buf[p];
		do {
			crc = (crc << 1) ^ (((crc & 0x8000) != 0) ? poly : 0);
			crc ^= (bit & byte) != 0 ? poly : 0;
		} while(bit >>= 1);
	}
	return crc;
}

static int rk3399MaskRomTransfer(LbusbContext *ctx, const char *buf, size_t size, uint16_t request) {
	assert(request == 0x471 || request == 0x472);

	uint8_t xfer_buf[4096];
	uint8_t rc4_st[258];
	uint16_t crc = 0xffff;
	rc4_init(rc4_st, RC4_RK_KEY, sizeof(RC4_RK_KEY));
	for (size_t offset = 0; offset < size; offset += 4096) {
		size_t blocklen = size - offset;
		if (blocklen > 0x1000) {blocklen = 0x1000;}
		memcpy(xfer_buf, buf, blocklen);
		if (blocklen == 0xffe || blocklen == 0xfff) {
			// the mask ROM interprets a transfer shorter
			// than 4 KiB as completion of code loading.
			// for 4094 B we must pad to include the required
			// CRC and signal completion, for 4095 we do it
			// out of convenience (don't have to split CRC
			// over 2 blocks)
			memset(xfer_buf + blocklen, 0, 0x1000 - blocklen);
			blocklen = 0x1000;
		}
		rc4(xfer_buf, blocklen, rc4_st);
		crc = crc16(xfer_buf, blocklen, crc, 0x1021);
		if (blocklen < 0xffe) {
			xfer_buf[blocklen++] = crc >> 8;
			xfer_buf[blocklen++] = (uint8_t)crc;
		}
		fprintf(stderr, "%zu-byte 0x%"PRIx16" request\n", blocklen, request);
		ssize_t res = libusb_control_transfer(
			ctx->usb_handle,
			USB_VENDOR_REQ | USB_DEV_REQ, 0xc,
			0, request,
			xfer_buf, blocklen, blocklen == 0x1000 ? 100 : 10000
		);
		if (res != (ssize_t)blocklen) {
			fprintf(stderr, "error while sending data: %zd (%s)\n", res, libusb_error_name((int)res));
			return 2;
		}
		if (blocklen != 0x1000) {return 0;}
	}
	fprintf(stderr, "2-byte 0x%"PRIx16" request\n", request);
	xfer_buf[0] = crc >> 8;
	xfer_buf[1] = (uint8_t)crc;
	ssize_t res = libusb_control_transfer(
		ctx->usb_handle,
		USB_VENDOR_REQ | USB_DEV_REQ, 0xc,
		0, request,
		xfer_buf, 2, 10000
	);
	if (res != 2) {
		fprintf(stderr, "error while sending CRC: %zd (%s)\n", res, libusb_error_name((int)res));
		return 2;
	}
	return 0;
}

static int cliStateError(const char *name, const char *cmd, LbusbState st) {
	fprintf(stderr, "Error: %s command in state %s\n", cmd, lbusb_state_names[st]);
	usage(name);
	return 1;
}

int main(int argc, char **argv) {
	if (argc < 1) {return 0;}
	if (argc == 1) {return help(argv[0]);}

	LbusbContext ctx = {
		.st = LBUSB_NO_DEVICE,
		.usb_ctx = NULL,
	};
	char **cmd = argv + 1;
	int res;
	while (*cmd) {
		if (0 == strcmp("help", *cmd)) {
			return help(argv[0]);
		} else if (0 == strcmp("rk3399", *cmd)) {
			if (ctx.st != LBUSB_NO_DEVICE) {
				return cliStateError(argv[0], *cmd, ctx.st);
			}
			if ((res = connectRk3399(&ctx))) {return res;}
		} else if (0 == strcmp("stub", *cmd)) {
			if (ctx.st != LBUSB_RK_MASKROM) {
				return cliStateError(argv[0], *cmd, ctx.st);
			}
			char *filename = *++cmd;
			LoadFile file;
			if (!loadFile(filename, &file)) {return 2;}
			if ((res = rk3399MaskRomTransfer(
				&ctx, file.buf, file.size, 0x471
			))) {return res;}
			unloadFile(&file);
		} else {
			fprintf(stderr, "Error: unknown command %s in state %s\n", *cmd, lbusb_state_names[ctx.st]);
			usage(argv[0]);
			return 1;
		}
		cmd++;
	}
	return 0;
}
