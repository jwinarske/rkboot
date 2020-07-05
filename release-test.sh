#!/bin/sh
atf="$1"
artifacts="$2"
src=`dirname "$0"`

set -e

echo "Source directory: $src"

if [ -z "$atf" -o -z "$artifacts" ]; then
	echo "usage: $0 path/to/atf/headers path/to/external/artifacts"
	echo "see $src/artifacts/what-is-this.rst for a list of artifacts to supply"
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

prompt() {
	read -p "reset the board, then press enter, or input a number to skip to that test: " skip
	test "$skip"
}

read -p "enter a configuration number, or press enter to start from the beginning: " skip

if [ -z "$skip" -o "$skip" = "0" ]; then
	echo "Configuration 0: wipe SPI ID block. Not needed if there is no bootloader in SPI, will require some kind of recovery button mechanism to get into mask ROM mode if there is."
	"$src/configure.py"
	ninja levinboot-usb.bin spi-flasher.bin
	until prompt || echo -en "\\xff" | usbtool --call levinboot-usb.bin --load 4100000 spi-flasher.bin --dramcall 4100000 1000 --pload 0 -; do true; done
fi

if [ -z "$skip" -o "$skip" == "1" ]; then
	echo "Configuration 1: only levinboot + memtest"
	"$src/configure.py"
	ninja levinboot-usb.bin memtest.bin
	until prompt || usbtool --call levinboot-usb.bin --run memtest.bin; do true; done
fi

if [ -z "$skip" -o "$skip" == "2" ]; then
	echo "Configuration 2: levinboot + uncached memtest"
	"$src/configure.py" --uncached-memtest
	ninja levinboot-usb.bin memtest.bin
	until prompt || usbtool --call levinboot-usb.bin --run memtest.bin; do true; done
fi

if [ -z "$skip" -o "$skip" == "3" ]; then
	echo "Configuration 3: levinboot + elfloader + teststage"
	"$src/configure.py" --with-atf-headers "$atf"
	ninja levinboot-usb.bin elfloader.bin teststage.bin
	until prompt || usbtool --call levinboot-usb.bin --load 4000000 elfloader.bin --load 4200000 "$artifacts/bl31.elf" --load 100000 "$artifacts/fdt.dtb" --load 280000 teststage.bin --jump 4000000 1000; do true; done
fi

if [ -z "$skip" -o "$skip" == "4" ]; then
	echo "Configuration 4: levinboot + elfloader + kernel"
	"$src/configure.py" --with-atf-headers "$atf"
	ninja levinboot-usb.bin elfloader.bin
	until prompt || usbtool --call levinboot-usb.bin --load 4000000 elfloader.bin --load 4200000 "$artifacts/bl31.elf" --load 100000 "$artifacts/fdt.dtb" --load 280000 "$artifacts/Image" --jump 4000000 1000; do true; done
fi

if [ -z "$skip" -o "$skip" == "5" ]; then
	echo "Configuration 5: levinboot + elfloader + teststage, gzip compression"
	"$src/configure.py" --with-atf-headers "$atf" --elfloader-gzip
	ninja levinboot-usb.bin elfloader.bin teststage.bin
	gzip -k teststage.bin
	until prompt || dtc -@ "$src/overlay-example.dts" -I dts -O dtb -o - | \
		fdtoverlay -i "$artifacts/fdt.dtb" -o - - | gzip | cat "$artifacts/bl31.gz" - teststage.bin.gz | \
		usbtool --call levinboot-usb.bin --load 4000000 elfloader.bin --load 4400000 -  --jump 4000000 1000; do true; done
	rm teststage.bin.gz
fi

if [ -z "$skip" -o "$skip" == "6" ]; then
	echo "Configuration 6: levinboot + brompatch + elfloader + kernel, mixed compression"
	"$src/configure.py" --with-atf-headers "$atf" --elfloader-gzip --elfloader-lz4 --elfloader-zstd
	ninja levinboot-usb.bin brompatch.bin elfloader.bin teststage.bin
	until prompt || dtc -@ "$src/overlay-example.dts" -I dts -O dtb -o - | \
		fdtoverlay -i "$artifacts/fdt.dtb" -o - - | lz4 | cat "$artifacts/bl31.gz" - "$artifacts/Image.zst" | usbtool --call levinboot-usb.bin --load 4100000 brompatch.bin --dramcall 4100000 1000 --pload 4400000 - --pstart 4000000 elfloader.bin; do true; done
fi

if [ -z "$skip" -o "$skip" == "7" ]; then
	echo "Configuration 7: levinboot with embedded SPI elfloader (gzip decompression)"
	"$src/configure.py" --with-atf-headers "$atf" --embed-elfloader --elfloader-spi --elfloader-gzip
	ninja levinboot-usb.bin
	until prompt || usbtool --run levinboot-usb.bin;do true; done
fi

if [ -z "$skip" -o "$skip" == "8" ]; then
	echo "Configuration 8: levinboot SD image with zstd decompressor"
	"$src/configure.py" --with-atf-headers "$atf" --embed-elfloader --elfloader-spi --elfloader-zstd
	ninja levinboot-sd.img
	echo "Build successful"
fi

if [ -z "$skip" -o "$skip" == "9" ]; then
	echo "Configuration 9: levinboot with embedded SD elfloader, configured for initcpio use (gzip decompression)"
	"$src/configure.py" --with-atf-headers "$atf" --embed-elfloader --elfloader-sd --elfloader-gzip --elfloader-initcpio
	ninja levinboot-usb.bin
	until prompt || usbtool --run levinboot-usb.bin;do true; done
fi

if [ -z "$skip" -o "$skip" == "10" ]; then
	echo "Configuration 10: flash levinboot SPI image configured for SD and SPI boot with initcpio (lz4, gzip and zstd decompression)"
	"$src/configure.py" --with-atf-headers "$atf" --embed-elfloader --elfloader-{sd,spi,lz4,gzip,zstd,initcpio}
	ninja levinboot-spi.img
	echo "Image build successful, building flasher"
	"$src/configure.py"
	ninja levinboot-usb.bin spi-flasher.bin
	until prompt || usbtool --call levinboot-usb.bin --load 4100000 spi-flasher.bin --dramcall 4100000 1000 --pload 0 levinboot-spi.img;do true; done
	read -p "now reset the board to try it out (both boot from SD and SPI), press enter to continue"
fi
