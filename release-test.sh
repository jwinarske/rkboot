#!/bin/sh
atf="$1"
artifacts="$2"
src=`dirname "$0"`

set -e

echo "Source directory: $src"

if [ -z "$atf" -o -z "$artifacts" ]; then
	echo "usage: $0 path/to/atf/headers path/to/external/artifacts"
	exit 1
fi

testrun() {
	while true; do
		read -p "reset the board, then press enter, or input a number to skip to that test: " skip
		if [ -z "$skip" ]; then
			if usbtool $@; then
				break
			fi
		else
			break
		fi
	done
}

if [ -z "$skip" -o "$skip" == "1" ]; then
	echo "Configuration 1: only levinboot + memtest"
	"$src/configure.py"
	ninja levinboot-usb.bin memtest.bin
	testrun --call levinboot-usb.bin --run memtest.bin
fi

if [ -z "$skip" -o "$skip" == "2" ]; then
	echo "Configuration 2: levinboot + uncached memtest"
	"$src/configure.py" --uncached-memtest
	ninja levinboot-usb.bin memtest.bin
	testrun --call levinboot-usb.bin --run memtest.bin
fi

if [ -z "$skip" -o "$skip" == "3" ]; then
	echo "Configuration 3: levinboot + elfloader + teststage"
	"$src/configure.py" --with-atf-headers "$atf"
	ninja levinboot-usb.bin elfloader.bin teststage.bin
	testrun --call levinboot-usb.bin --load 100000 elfloader.bin --load 200000 "$artifacts/bl31.elf" --load 500000 "$artifacts/fdt.dtb" --load 680000 teststage.bin --jump 100000 1000
fi

if [ -z "$skip" -o "$skip" == "4" ]; then
	echo "Configuration 4: levinboot + elfloader + kernel"
	"$src/configure.py" --with-atf-headers "$atf"
	ninja levinboot-usb.bin elfloader.bin
	testrun --call levinboot-usb.bin --load 100000 elfloader.bin --load 200000 "$artifacts/bl31.elf" --load 500000 "$artifacts/fdt.dtb" --load 680000 "$artifacts/Image" --jump 100000 1000
fi

if [ -z "$skip" -o "$skip" == "5" ]; then
	echo "Configuration 5: levinboot + elfloader + teststage, gzip compression"
	"$src/configure.py" --with-atf-headers "$atf" --elfloader-gzip
	ninja levinboot-usb.bin elfloader.bin teststage.bin
	gzip -k teststage.bin
	trap 'rm blob.fifo' ERR
	mkfifo blob.fifo
	dtc -@ "$src/overlay-example.dts" -I dts -O dtb -o - | fdtoverlay -i "$artifacts/fdt.dtb" -o - - | gzip | cat "$artifacts/bl31.gz" - teststage.bin.gz >blob.fifo & testrun --call levinboot-usb.bin --load 100000 elfloader.bin --load 2000000 blob.fifo --jump 100000 1000
	rm blob.fifo
	trap '' ERR
	rm teststage.bin.gz
fi

if [ -z "$skip" -o "$skip" == "6" ]; then
	echo "Configuration 6: levinboot + elfloader + kernel, mixed compression"
	"$src/configure.py" --with-atf-headers "$atf" --elfloader-gzip --elfloader-lz4 --elfloader-zstd
	ninja levinboot-usb.bin elfloader.bin teststage.bin
	trap 'rm blob.fifo' ERR
	mkfifo blob.fifo
	dtc -@ "$src/overlay-example.dts" -I dts -O dtb -o - | fdtoverlay -i "$artifacts/fdt.dtb" -o - - | lz4 | cat "$artifacts/bl31.gz" - "$artifacts/Image.zst" >blob.fifo & testrun --call levinboot-usb.bin --load 100000 elfloader.bin --load 2000000 blob.fifo --jump 100000 1000
	rm blob.fifo
	trap '' ERR
fi

if [ -z "$skip" -o "$skip" == "7" ]; then
	echo "Configuration 7: levinboot SD image with zstd decompressor"
	"$src/configure.py" --with-atf-headers "$atf" --embed-elfloader --elfloader-spi --elfloader-zstd
	ninja levinboot.img
	echo "Build successful"
fi