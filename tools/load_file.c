// SPDX-License-Identifier: CC0-1.0
#include "load_file.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

_Bool loadFile(const char *name, LoadFile *res) {
	int fd = 0;
	if (0 != strcmp(name, "-")) {
		fd = open(name, O_RDONLY);
		if (fd < 0) {
			perror("While opening file");
			return 0;
		}
	}
	struct stat statbuf;
	if (fstat(fd, &statbuf) < 0) {
		perror("while stat-ing file");
		return 0;
	}
	if (!S_ISREG(statbuf.st_mode)) {
		fprintf(stderr, "reading file\n");
		size_t cap = 512;
		char *buf = malloc(cap);
		if (!buf) {
			perror("while allocating buffer");
			return 0;
		}
		res->size = 0;
		while (1) {
			if (cap - res->size < 512) {
				cap <<= 1;
				buf = realloc(buf, cap);
				if (!buf) {
					perror("while growing buffer");
					return 0;
				}
			}
			ssize_t rres = read(fd, buf + res->size, cap - res->size);
			if (!rres) {
				break;
			} else if (rres < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
				perror("while reading file");
				return 0;
			} else if (rres > 0) {
				res->size += rres;
			}
		}
		res->buf = buf;
		res->mmapped = 0;
		return 1;
	}
	if ((off_t)(size_t)statbuf.st_size != statbuf.st_size) {
		fprintf(stderr, "file larger than SIZE_MAX");
		return 0;
	}
	res->size = statbuf.st_size;
	res->buf = mmap(0, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (!res->buf) {
		perror("while mapping file");
		return 0;
	}
	res->mmapped = 1;
	return 1;
}

void unloadFile(LoadFile *file) {
	if (file->mmapped) {
		if (munmap(file->buf, file->size) < 0) {
			perror("while unmapping file");
			abort();
		}
	} else {
		free(file->buf);
	}
}
