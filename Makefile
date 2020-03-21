ERRFLAGS ?= -Wall -Wextra -Werror=all -Wno-error=unused-parameter -Wno-error=comment -Werror=discarded-qualifiers -Werror=incompatible-pointer-types
EXTRAFLAGS = -ffreestanding -fno-builtin -nodefaultlibs -nostdlib -DENV_STAGE -isystem include $(ERRFLAGS) -DDEBUG_MSG
CFLAGS ?= -Og -march=armv8-a+nosimd -fstack-protector-all
# -mcmodel=tiny -fstack-usage -fsanitize=kernel-address -DDEBUG_MSG -fsanitize=undefined
BUILD_CFLAGS ?= -Os 
LDFLAGS ?= 
O ?= .
OBJECTS = $(O)/main.o $(O)/timer.o $(O)/uart.o $(O)/pll.o $(O)/ddrinit.o $(O)/odt.o $(O)/lpddr4.o $(O)/moderegs.o $(O)/training.o

default: $(O)/levinboot.img
all: $(O)/levinboot.img $(O)/levinboot.bin

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

$(O)/idbtool: tools/idbtool.c
	$(BUILD_CC) $(BUILD_CFLAGS) -o $@ $^

install: $(O)/levinboot.img $(O)/levinboot.elf $(O)/levinboot.bin
	cp $^ $(out)

clean:
	rm -f $(O)/*.o $(O)/{levinboot.img,levinboot.elf,levinboot.bin,idbtool}

.PHONY: install all default boot clean
