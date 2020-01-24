EXTRAFLAGS = -ffreestanding -fno-builtin -nodefaultlibs -nostdlib
CFLAGS ?= -Os -march=armv8-a+nosimd -Wall -Wextra -DENV_STAGE -isystem include -Werror=all -Wno-error=unused-parameter -Wno-error=comment -fstack-protector-all -fsanitize=undefined
# -mcmodel=tiny -fstack-usage -fsanitize=kernel-address
BUILD_CFLAGS ?= -Os 
LDFLAGS ?= 
O ?= .
OBJECTS = $(O)/main.o $(O)/timer.o $(O)/uart.o $(O)/pll.o $(O)/ddrinit.o $(O)/odt.o $(O)/lpddr4.o $(O)/moderegs.o

default: $(O)/levinboot.img
all: $(O)/levinboot.img $(O)/levinboot.bin $(O)/usbtool

$(O)/%.o: %.c
	$(CC) $(EXTRAFLAGS) $(CFLAGS) -c -o $@ $^

$(O)/%.o: %.bin
	$(OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 --rename-section .data=.rodata $< $@

$(O)/levinboot.img: $(O)/idbtool $(O)/levinboot.bin
	$(O)/idbtool <$(O)/levinboot.bin >$@

$(O)/levinboot.elf: $(OBJECTS) linkerscript.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS) -T linkerscript.ld

$(O)/levinboot.bin: $(O)/levinboot.elf
	$(OBJCOPY) -O binary $< $@

$(O)/idbtool: idbtool.c
	$(BUILD_CC) $(BUILD_CFLAGS) -o $@ $^

$(O)/usbtool: usbtool.c
	$(CC) $(CFLAGS) $(shell pkg-config --libs --cflags libusb-1.0) -o $@ $^ -fno-sanitize=kernel-address

boot: $(O)/usbtool $(O)/levinboot.bin
	$< <"$(O)/levinboot.bin"

install: $(O)/levinboot.img $(O)/levinboot.elf $(O)/levinboot.bin $(O)/usbtool
	cp $^ $(out)

clean:
	rm -f $(O)/*.o $(O)/{levinboot.img,levinboot.elf,levinboot.bin,idbtool,usbtool}

.PHONY: install all default boot clean
