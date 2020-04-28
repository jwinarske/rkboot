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
#include "tools.h"

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
		block_transfer(handle, buf + pos, &crc, rc4state);
	}
	return final_transfer(handle, crc, opcode);
}

#define Rd(n) ((n) & 31)
#define Rn(n) (((n) & 31) << 5)
#define Rt2(n) (((n) & 31) << 10)
#define Rm(n) (((n) & 31) << 16)
#define PAIR(disp) (((disp) & 0x7f) << 15)
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

enum {
	ADD64IMM = 0x91000000,
};

#define ADD_IMM(n) ((n) << 12 & 0x003ff000)
#define ADD_IMM_SHIFTED(n) ((n) & 0x003ff000)

enum pair_const {
	PAIR_BASE = 5 << 27,
	PAIR_POSTIDX = 1 << 23,
	PAIR_PREIDX = 3 << 23,
	PAIR_OFFSET = 2 << 23,
};

enum pair_inst {
	STP32_POST = PAIR_BASE | PAIR_POSTIDX,
	STP64_POST = PAIR_BASE | PAIR_POSTIDX | SIXTYFOUR,
	LDP32_POST = PAIR_BASE | PAIR_POSTIDX | LOAD,
	LDP64_POST = PAIR_BASE | PAIR_POSTIDX | LOAD | SIXTYFOUR,
	STP32_PRE = PAIR_BASE | PAIR_PREIDX,
	STP64_PRE = PAIR_BASE | PAIR_PREIDX | SIXTYFOUR,
	LDP32_PRE = PAIR_BASE | PAIR_PREIDX | LOAD,
	LDP64_PRE = PAIR_BASE | PAIR_PREIDX | LOAD | SIXTYFOUR,
	STP32_OFF = PAIR_BASE | PAIR_OFFSET,
	STP64_OFF = PAIR_BASE | PAIR_OFFSET | SIXTYFOUR,
	LDP32_OFF = PAIR_BASE | PAIR_OFFSET | LOAD,
	LDP64_OFF = PAIR_BASE | PAIR_OFFSET | LOAD | SIXTYFOUR,
};

enum binop {
	ADD32 = 0x0b000000,
	ADD64 = 0x8b000000,
};

enum branch_reg {
	BR = 0xd61f0000,
	BLR = 0xd63f0000,
	RET = 0xd65f0000,
};

enum barriers {ISB = 0xd5033fdf};

int _Noreturn die(const char *msg) {
	fprintf(stderr, "%s\n", msg);
	abort();
}

#define LDR64(imm) (0x58000000 | ((imm) << 3 & 0x00ffffe0))

const uint32_t copy_program[24] = {
	ADR(sizeof(copy_program)) | Rd(0),

	MRS | SYSREG_SCTLR_EL3 | Rd(5),
	OR(64, 1, 52) | Rn(5) | Rd(6),
	MSR | SYSREG_SCTLR_EL3 | Rd(6),

	LDP64_POST | PAIR(2) | Rn(0) | Rd(1) | Rt2(2),
	SUB | SETFLAGS | SIXTYFOUR | Rd(31) | Rn(1) | Rm(2),
	B_HS(7),

	LDP64_POST | PAIR(2) | Rn(0) | Rd(3) | Rt2(4),
	STP64_POST | PAIR(2) | Rn(1) | Rd(3) | Rt2(4),
	SUB | SETFLAGS | SIXTYFOUR | Rd(31) | Rn(2) | Rm(1),
	B_HS(-3),

	MOVZ(0, 0) | Rd(0),
	RET | Rn(30),

	LDR64(56) | Rd(0),
	ADD64IMM | Rd(0) | Rn(31) | ADD_IMM(0),
	ADD64IMM | Rd(31) | Rn(1) | ADD_IMM(0),
	STP64_PRE | PAIR(-2) | Rn(31) | Rd(30) | Rt2(2),
	IC_IALLU,
	ISB,
	BLR | Rn(0),
	LDP64_OFF | PAIR(0) | Rn(31) | Rd(30) | Rt2(2),
	ADD64IMM | Rd(31) | Rn(2) | ADD_IMM(0),
	RET(30),
	0,
};

static void write_le32(uint8_t *buf, uint32_t val) {
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
	const size_t buf_size = 180 * 1024;
	uint8_t *buf = malloc(buf_size);
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
			size_t size = read_file(fd, buf, buf_size);
			size_t rem = 4096 - (size & 0xfff);
			memset(buf + size, 0, rem);
			size += rem;
			if (!transfer(handle, buf, size, call ? 0x0471 : 0x0472)) {return 1;}
			if (fd) {close(fd);}
		} else if (!strcmp("--load", *arg)) {
			char *command = *arg;
			char *addr_string = *++arg;
			uint64_t addr_;
			if (!addr_string || sscanf(addr_string, "%"PRIx64, &addr_) != 1) {
				fprintf(stderr, "%s needs a load address\n", command);
				return 1;
			}
			uint64_t load_addr = addr_;
			char *filename = *++arg;
			if (!filename) {
				fprintf(stderr, "%s needs a file name\n", command);
				return 1;
			}
			int fd = 0;
			if (strcmp("-", filename)) {
				fd = open(filename, O_RDONLY, 0);
				if (fd < 0) {
					fprintf(stderr, "cannot open %s\n", filename);
					return 1;
				}
			}
			const size_t copy_size = sizeof(copy_program), header_size = copy_size + 16;
			for (int i = 0; i < copy_size; i += 4) {
				write_le32(buf + i, copy_program[i/4]);
			}
			size_t size, total_loaded = 0;
			do {
				size = read_file(fd, buf + header_size, buf_size - header_size);
				size_t rem = (header_size + size) & 0xfff ? 4096 - ((header_size + size) & 0xfff) : 0;
				memset(buf + header_size + size, 0, rem);
				uint64_t end_addr = load_addr + size;
				printf("offset 0x%"PRIx64", loading 0x%"PRIx64" bytes to 0x%"PRIx64", end %"PRIx64"\n", total_loaded, size, load_addr, end_addr);
				write_le32(buf + copy_size, load_addr);
				write_le32(buf + copy_size + 4, load_addr >> 32);
				write_le32(buf + copy_size + 8, end_addr);
				write_le32(buf + copy_size + 12, end_addr >> 32);
				if (!transfer(handle, buf, header_size + size + rem, 0x0471)) {return 1;}
				load_addr = end_addr;
			} while (size + header_size >= buf_size);
		} else if ((call = !strcmp("--dramcall", *arg)) || !strcmp("--jump", *arg)) {
			uint64_t entry_addr, stack_addr;
			char *command = *arg, *entry_str = *++arg;
			if (!entry_str || sscanf(entry_str, "%"SCNx64, &entry_addr) != 1) {
				fprintf(stderr, "%s needs an entry point address\n", command);
				return 1;
			}
			char *stack_str = *++arg;
			if (!stack_str || sscanf(stack_str, "%"SCNx64, &stack_addr) != 1) {
				fprintf(stderr, "%s needs a stack address\n", command);
				return 1;
			}
			const size_t jump_size = sizeof(copy_program);
			for (int i = 0; i < jump_size; i += 4) {
				write_le32(buf + i, copy_program[i/4]);
			}
			write_le32(buf + jump_size, entry_addr);
			write_le32(buf + jump_size + 4, entry_addr >> 32);
			write_le32(buf + jump_size + 8, entry_addr);
			write_le32(buf + jump_size + 12, entry_addr >> 32);
			write_le32(buf + jump_size + 16, stack_addr);
			write_le32(buf + jump_size + 20, stack_addr >> 32);
			memset(buf + jump_size + 24, 0, 4096 - jump_size - 24);
			uint8_t rc4state[258];
			rc4_init(rc4state);
			uint16_t crc = 0xffff;
			if (!block_transfer(handle, buf, &crc, rc4state) || !final_transfer(handle, crc, call ? 0x0471 : 0x0472)) {return 1;}
		} else {
			fprintf(stderr, "unknown command line argument %s", *arg);
			return 1;
		}
	}
	printf("done, closing USB handle\n");
	libusb_close(handle);
	libusb_exit(ctx);
}
