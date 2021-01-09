/* SPDX-License-Identifier: CC0-1.0 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <libusb.h>
#include <assert.h>
#include "rc4.h"

uint16_t crc16(uint8_t *buf, size_t len, uint16_t crc) {
	const uint16_t poly = 0x1021;
	for (size_t p = 0; p < len; ++p) {
		uint8_t bit = 0x80, byte = buf[p];
		do {
			crc = (crc << 1) ^ (((crc & 0x8000) != 0) ? poly : 0);
			crc ^= (bit & byte) != 0 ? poly : 0;
		} while(bit >>= 1);
	}
	return crc;
}

static const uint32_t crc32c_poly = 0x82f63b78;
static uint32_t crc32_byte(uint32_t crc, uint32_t poly) {
	for (int i = 0; i < 8; ++i) {
		crc = (crc >> 1) ^ (crc & 1 ? poly : 0);
	}
	return crc;
}
static uint32_t crc32(uint8_t *buf, size_t len, uint32_t crc, uint32_t poly) {
	for (size_t p = 0; p < len; ++p) {
		crc ^= buf[p];
		crc = crc32_byte(crc, poly);
	}
	return crc;
}

static void write_le32(uint8_t *buf, uint32_t val) {
	buf[0] = val & 0xff;
	buf[1] = val >> 8 & 0xff;
	buf[2] = val >> 16 & 0xff;
	buf[3] = val >> 24;
}

_Bool final_transfer(libusb_device_handle *handle, uint16_t crc, uint16_t opcode) {
	uint8_t buf[2];
	buf[0] = crc >> 8;
	buf[1] = (uint8_t)crc;
	printf("2-byte 0x%x transfer\n", (unsigned)opcode);
	ssize_t res = libusb_control_transfer(handle, 0x40, 0xc, 0, opcode, buf, 2, 20000);
	if (res != 2) {
		fprintf(stderr, "error while sending CRC: %zd (%s)\n", res, libusb_error_name((int)res));
		return 0;
	}
	return 1;
}

_Bool block_transfer(libusb_device_handle *handle, uint8_t *buf, uint16_t *crc, uint8_t *rc4state) {
	printf("4096-byte 0x0471 transfer\n");
	rc4(buf, 4096, rc4state);
	*crc = crc16(buf, 4096, *crc);
	if (libusb_control_transfer(handle, 0x40, 0xc, 0, 0x0471, buf, 4096, 100) != 4096) {
		fprintf(stderr, "error while sending data\n");
		return 0;
	}
	return 1;
}

size_t read_file(int fd, uint8_t *buf, size_t size) {
	size_t pos = 0;
	while (pos < size) {
		ssize_t res = read(fd, buf + pos, size - pos);
		if (res == 0) {
			return pos;
		}
		if (res < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {continue;}
			perror("Error while reading input file");
			exit(1);
		}
		pos += res;
	}
	return pos;
}

_Bool transfer(libusb_device_handle *handle, uint8_t *buf, size_t size, uint16_t opcode) {
	u8 rc4state[258];
	rc4_init(rc4state);
	uint16_t crc = 0xffff;
	assert(!(size & 0xfff));
	for (size_t pos = 0; pos < size; pos += 4096) {
		printf("flushing %zu bytes\n", pos);
		if (!block_transfer(handle, buf + pos, &crc, rc4state)) {return 0;}
	}
	return final_transfer(handle, crc, opcode);
}

int _Noreturn die(const char *msg) {
	fprintf(stderr, "%s\n", msg);
	abort();
}

struct device_discovery {
	libusb_device *device;
	libusb_device_handle *handle;
};

static struct device_discovery  find_device(libusb_context *ctx) {
	libusb_device **list;
	ssize_t err;
	if ((err = libusb_get_device_list(ctx, &list)) < 0) {
		fprintf(stderr, "error listing devices: %zd (%s)\n", err, libusb_error_name(err));
		exit(2);
	}
	struct device_discovery res = {.device = 0, .handle = 0};
	for (size_t pos = 0; list[pos]; ++pos) {
		struct libusb_device_descriptor devdesc;
		libusb_get_device_descriptor(list[pos], &devdesc);
		if (devdesc.idVendor == 0x2207 && devdesc.idProduct == 0x330c) {
			int err = libusb_open(list[pos], &res.handle);
			res.device = list[pos];
			if (err) {
				fprintf(stderr, "error opening device: %d (%s)\n", err, libusb_error_name(err));
				exit(2);
			}
			break;
		}
	}
	libusb_free_device_list(list, 1);
	return res;
}

enum usbstage_command {
	CMD_LOAD = 0,
	CMD_CALL,
	CMD_START,
	CMD_FLASH,
	NUM_CMD
};

void bulk_mode(libusb_device_handle *handle, char **arg) {
	const size_t buf_size = 1024 * 1024;
	uint8_t *buf = malloc(buf_size);
	if (!buf) {
		fprintf(stderr, "buffer allocation failed\n");
		abort();
	}
	int err = libusb_claim_interface(handle, 0);
	if (err) {
		fprintf(stderr, "error claiming interface: %d (%s)\n", err, libusb_error_name(err));
		exit(2);
	}
	while (*++arg) {
		_Bool call, load;
		if ((load = !strcmp("--load", *arg)) || !strcmp("--flash", *arg)) {
			char *command = *arg;
			char *addr_string = *++arg;
			uint64_t addr_;
			if (!addr_string || sscanf(addr_string, "%"PRIx64, &addr_) != 1) {
				fprintf(stderr, "%s needs a load address\n", command);
				exit(1);;
			}
			uint64_t load_addr = addr_;
			char *filename = *++arg;
			if (!filename) {
				fprintf(stderr, "%s needs a file name\n", command);
				exit(1);
			}
			int fd = 0;
			if (strcmp("-", filename)) {
				fd = open(filename, O_RDONLY, 0);
				if (fd < 0) {
					fprintf(stderr, "cannot open %s\n", filename);
					exit(1);
				}
			}
			while (1) {
				int size = read_file(fd, buf, buf_size);
				if (!size) {break;}
				if (size & 0x1ff) {
					memset(buf + size, 0, 0x200 - (size & 0x1ff));
					size = (size + 0x1ff) & ~(size_t)0x1ff;
				}
				printf("loading 0x%x bytes to 0x%"PRIx64"\n", size, load_addr);
				u8 header[512];
				memset(header, 0, sizeof(header));
				write_le32(header + 0, load ? CMD_LOAD : CMD_FLASH);
				write_le32(header + 8, size);
				write_le32(header + 12, 0);
				write_le32(header + 16, load_addr);
				write_le32(header + 20, load_addr >> 32);

				int transferred_bytes = 0;
				err = libusb_bulk_transfer(handle, 2, header, 512, &transferred_bytes, 1000);
				if (err) {
					fprintf(stderr, "error while sending header: %d (%s)\n", err, libusb_error_name(err));
					exit(2);
				}
				err = libusb_bulk_transfer(handle, 2, buf, size, &transferred_bytes, 1000);
				if (err) {
					fprintf(stderr, "error while sending data: %d (%s)\n", err, libusb_error_name(err));
					exit(2);
				}
				load_addr += size;
				if (size < buf_size) {break;}
			}
		} else if  ((call = !strcmp("--call", *arg)) || !strcmp("--start", *arg)) {
			uint64_t entry_addr, stack_addr;
			char *command = *arg, *entry_str = *++arg;
			if (!entry_str || sscanf(entry_str, "%"SCNx64, &entry_addr) != 1) {
				fprintf(stderr, "%s needs an entry point address\n", command);
				exit(1);
			}
			char *stack_str = *++arg;
			if (!stack_str || sscanf(stack_str, "%"SCNx64, &stack_addr) != 1) {
				fprintf(stderr, "%s needs a stack address\n", command);
				exit(1);
			}
			u8 header[512];
			memset(header, 0, sizeof(header));
			write_le32(header + 0, call ? CMD_CALL : CMD_START);
			write_le32(header + 8, (uint32_t)entry_addr);
			write_le32(header + 12, entry_addr >> 32);
			write_le32(header + 16, (uint32_t)stack_addr);
			write_le32(header + 24, stack_addr >> 32);
			int transferred_bytes = 0;
			err = libusb_bulk_transfer(handle, 2, header, 512, &transferred_bytes, 1000);
			if (err) {
				fprintf(stderr, "error while sending run command: %d (%s)\n", err, libusb_error_name(err));
				exit(2);
			}
			if (!call) {break;}
		} else {
			fprintf(stderr, "unknown command line argument %s", *arg);
			exit(1);
		}
	}
}

int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "no commands given, not initializing USB\n");
		return 0;
	}
	libusb_context *ctx;
	if (libusb_init(&ctx)) {
		fprintf(stderr, "error initializing libusb\n");
		return 2;
	}
	struct device_discovery discovery = find_device(ctx);
	libusb_device_handle *handle = discovery.handle;
	if (!handle) {
		fprintf(stderr, "no device found\n");
		return 2;
	}
	if (0 == strcmp("--bulk", argv[1])) {
		bulk_mode(handle, argv + 1);
		goto out;
	}
	const size_t buf_size = 180 * 1024;
	uint8_t *buf = malloc(buf_size);
	if (!buf) {
		fprintf(stderr, "buffer allocation failed\n");
		abort();
	}
	char **arg = argv;
	while (*++arg) {
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
			size_t size = read_file(fd, buf, buf_size);
			size_t rem = 4096 - (size & 0xfff);
			memset(buf + size, 0, rem);
			size += rem;
			fprintf(stderr, "CRC32C: %08"PRIx32"\n", ~crc32(buf, size, ~(uint32_t)0, crc32c_poly));
			if (!transfer(handle, buf, size, call ? 0x0471 : 0x0472)) {
				return 1;
			}
			if (fd) {close(fd);}
			if (!call) {break;}
		} else {
			fprintf(stderr, "unknown command line argument %s", *arg);
			return 1;
		}
	}
	free(buf);
	if (arg[1]) {
		usleep(300000);	/* need to wait for usbstage to take over the USB controller */
		puts("more commands, switching to bulk mode");
		bulk_mode(handle, arg);
	}
out:
	libusb_close(handle);
	libusb_exit(ctx);
	return 0;
}
