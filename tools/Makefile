O ?= .

all: $(O)/idbtool $(O)/usbtool

$(O)/idbtool: idbtool.c
	$(CC) $(CFLAGS) -o $@ $^

$(O)/usbtool: usbtool.c
	$(CC) $(CFLAGS) $(shell pkg-config --libs --cflags libusb-1.0) -o $@ $^ -fno-sanitize=kernel-address

install: $(O)/idbtool $(O)/usbtool
	mkdir -p $(out)/bin
	cp $^ $(out)/bin

.PHONY: all
