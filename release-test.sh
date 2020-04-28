#!/bin/sh
atf="$1"
artifacts="$2"
src=`dirname "$0"`

set -e

echo "Source directory: $src"
echo $@

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
	ninja levinboot-usb.bin elfloader.bin teststage.bin
	testrun --call levinboot-usb.bin --load 100000 elfloader.bin --load 200000 "$artifacts/bl31.elf" --load 500000 "$artifacts/fdt.dtb" --load 680000 "$artifacts/Image" --jump 100000 1000
fi
