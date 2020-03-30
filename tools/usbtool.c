#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <libusb.h>
#include "tools.h"

_Bool transfer(libusb_device_handle *handle, int fd, uint8_t *buf, size_t pos, size_t max, uint16_t opcode) {
	u8 rc4state[258];
	rc4_init(rc4state);
	uint16_t crc = 0xffff;
	const uint16_t poly = 0x1021;
	_Bool eof = 0;
	size_t transferred = 0;
	while (!eof && transferred < max) {
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
			printf("4096-byte 0x0471 transfer (%zu bytes)\n", pos);
			memset(buf + pos, 0, 4096 - pos);
			rc4(buf, 4096, rc4state);
			for (size_t p = 0; p < 4096; ++p) {
				uint8_t bit = 0x80, byte = buf[p];
				do {
					crc = (crc << 1) ^ (((crc & 0x8000) != 0) ? poly : 0);
					crc ^= (bit & byte) != 0 ? poly : 0;
				} while(bit >>= 1);
			}
			if (libusb_control_transfer(handle, 0x40, 0xc, 0, 0x0471, buf, 4096, 100) != 4096) {
				fprintf(stderr, "error while sending data\n");
				return 2;
			}
			transferred += 4096;
			pos = 0;
		}
	}
	buf[0] = crc >> 8;
	buf[1] = (uint8_t)crc;
	printf("2-byte 0x%x transfer\n", (unsigned)opcode);
	ssize_t res = libusb_control_transfer(handle, 0x40, 0xc, 0, opcode, buf, 2, 3000);
	if (res != 2) {
		fprintf(stderr, "error while sending CRC: %zd (%s)\n", res, libusb_error_name((int)res));
		return 2;
	}
	return 1;
}

#define Rd(n) ((n) & 31)
#define Rn(n) (((n) & 31) << 5)
#define Rt2(n) (((n) & 31) << 10)
#define Rm(n) (((n) & 31) << 16)
#define PAIR_POST(disp) (0x28800000 | ((disp) & 0x7f) << 15)
#define ADR(imm) (1 <<28 | ((imm) & 3) << 29 | ((imm) & 0x1ffffc) << 3)
#define B_HS(disp) (0x54000002 | ((disp) & 0x7ffff) << 5)
#define MOVZ(hw, val) (0x92800000 | ((hw) & 3) << 21 | ((val) & 0xffff) << 5)
#define RET(r) (0xd65f0000 | Rn(r))
#define SUB 0x4b000000
#define SIXTYFOUR (1 << 31)
#define SETFLAGS (1 << 29)
#define LOAD (1 << 22)
#define IC 0xd5087000
#define IC_IALLU (IC | 0x051f)
#define MSR 0xd5100000
#define MRS 0xd5300000
#define SYSREG_SCTLR_EL3 0xe1000
#define OR(width, length, rotate) ((width == 64 || width == 32 || width == 16 || width == 8) && (length) < (width) && (length) < (width) ? (0x32000000 | (width == 64) << 31 | (width == 64) << 22 | (rotate) << 16 | ((length - 1) | (~((width) - 1) & 0x3f)) << 10) : die(": wrong logic encoding"))

int _Noreturn die(const char *msg) {
	fprintf(stderr, "%s\n", msg);
	abort();
}

const uint32_t copy_program[14] = {
	MRS | SYSREG_SCTLR_EL3 | Rd(5),
	IC_IALLU,
	OR(64, 1, 52) | Rn(5) | Rd(6),
	MSR | SYSREG_SCTLR_EL3 | Rd(6),

	ADR(40) | Rd(0),
	PAIR_POST(2) | SIXTYFOUR | LOAD | Rn(0) | Rd(1) | Rt2(2),

	PAIR_POST(2) | SIXTYFOUR | LOAD  | Rn(0) | Rd(3) | Rt2(4),
	PAIR_POST(2) | SIXTYFOUR | Rn(1) | Rd(3) | Rt2(4),
	SUB | SETFLAGS | SIXTYFOUR | Rd(31) | Rn(2) | Rm(1),
	B_HS(-3),
	MOVZ(0, 0) | Rd(0),
	MSR | SYSREG_SCTLR_EL3 | Rd(5),
	IC_IALLU,
	RET(30),
};

void write_le32(uint8_t *buf, uint32_t val) {
	buf[0] = val & 0xff;
	buf[1] = val >> 8 & 0xff;
	buf[2] = val >> 16 & 0xff;
	buf[3] = val >> 24;
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
			transfer(handle, fd, buf, 0, 184 * 1024, call ? 0x0471 : 0x0472);
			if (fd) {close(fd);}
		} else if (!strcmp("--load", *arg)) {
			char *command = *arg;
			char *filename = *++arg;
			if (!filename) {
				fprintf(stderr, "%s needs a parameter\n", command);
				return 1;
			}
			char *addr_string = *++arg;
			uint64_t addr_;
			if (!addr_string || sscanf(addr_string, "%"PRIx64, &addr_) != 1) {
				fprintf(stderr, "%s needs a load address\n", command);
				return 1;
			}
			uint64_t load_addr = addr_;
			int fd = 0;
			if (strcmp("-", filename)) {
				fd = open(filename, O_RDONLY, 0);
				if (fd < 0) {
					fprintf(stderr, "cannot open %s\n", filename);
					return 1;
				}
			}
			struct stat statbuf;
			if (fstat(fd, &statbuf)) {
				fprintf(stderr, "failed to fstat %s\n", filename);
				return 1;
			}
			uint64_t filesize = statbuf.st_size, pos = 0;
			while (pos < filesize) {
				const size_t copy_size = sizeof(copy_program);
				const uint64_t max_per_round = 180*1024 - copy_size - 16;
				for (int i = 0; i < copy_size; i += 4) {
					write_le32(buf + i, copy_program[i/4]);
				}
				uint64_t size = filesize - pos < max_per_round ? filesize - pos : max_per_round;
				uint64_t start_addr = load_addr + pos, end_addr = load_addr + pos + size;
				printf("offset 0x%"PRIx64", loading 0x%"PRIx64" bytes to 0x%"PRIx64", end %"PRIx64"\n", pos, size, start_addr, end_addr);
				write_le32(buf + copy_size, start_addr);
				write_le32(buf + copy_size + 4, start_addr >> 32);
				write_le32(buf + copy_size + 8, end_addr);
				write_le32(buf + copy_size + 12, end_addr >> 32);
				transfer(handle, fd, buf, copy_size + 16, size, 0x0471);
				pos += size;
			}
		}
	}
	printf("done, closing USB handle\n");
	libusb_close(handle);
	libusb_exit(ctx);
}
