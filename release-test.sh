#!/bin/sh
atf="$1"
artifacts="$2"
src=`dirname "$0"`

set -e

echo "Source directory: $src"

if [ -z "$atf" -o -z "$artifacts" ]; then
	echo "usage: $0 path/to/tf-a/headers path/to/external/artifacts"
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
	ninja sramstage.bin usbstage.bin
	until prompt || echo -en "\\xff" | usbtool --call sramstage.bin --run usbstage.bin --flash 0 -; do true; done
fi

if [ -z "$skip" -o "$skip" == "1" ]; then
	echo "Configuration 1: sramstage + memtest"
	"$src/configure.py"
	ninja sramstage.bin memtest.bin
	until prompt || usbtool --call sramstage.bin --run memtest.bin; do true; done
fi

if [ -z "$skip" -o "$skip" == "2" ]; then
	echo "Configuration 2: sramstage + uncached memtest"
	"$src/configure.py" --uncached-memtest
	ninja sramstage.bin memtest.bin
	until prompt || usbtool --call sramstage.bin --run memtest.bin; do true; done
fi

if [ -z "$skip" -o "$skip" == "3" ]; then
	echo "Configuration 3: sramstage + elfloader + teststage"
	"$src/configure.py" --with-tf-a-headers "$atf"
	ninja sramstage.bin elfloader.bin teststage.bin
	until prompt || usbtool --call sramstage.bin --load 4000000 elfloader.bin --load 4200000 "$artifacts/bl31.elf" --load 100000 "$artifacts/fdt.dtb" --load 280000 teststage.bin --jump 4000000 1000; do true; done
fi

if [ -z "$skip" -o "$skip" == "4" ]; then
	echo "Configuration 4: sramstage + elfloader + kernel"
	"$src/configure.py" --with-tf-a-headers "$atf"
	ninja sramstage.bin elfloader.bin
	until prompt || usbtool --call sramstage.bin --load 4000000 elfloader.bin --load 4200000 "$artifacts/bl31.elf" --load 100000 "$artifacts/fdt.dtb" --load 280000 "$artifacts/Image" --jump 4000000 1000; do true; done
fi

if [ -z "$skip" -o "$skip" == "5" ]; then
	echo "Configuration 5: sramstage + elfloader + teststage, gzip compression"
	"$src/configure.py" --with-tf-a-headers "$atf" --elfloader-gzip
	ninja sramstage.bin elfloader.bin teststage.bin
	gzip -k teststage.bin
	until prompt || dtc -@ "$src/overlay-example.dts" -I dts -O dtb -o - | \
		fdtoverlay -i "$artifacts/fdt.dtb" -o - - | gzip | cat "$artifacts/bl31.gz" - teststage.bin.gz | \
		usbtool --call sramstage.bin --load 4000000 elfloader.bin --load 4400000 -  --jump 4000000 1000; do true; done
	rm teststage.bin.gz
fi

if [ -z "$skip" -o "$skip" == "6" ]; then
	echo "Configuration 6: sramstage + usbstage + elfloader + kernel, mixed compression"
	"$src/configure.py" --with-tf-a-headers "$atf" --elfloader-gzip --elfloader-lz4 --elfloader-zstd
	ninja sramstage.bin usbstage.bin elfloader.bin teststage.bin
	until prompt || dtc -@ "$src/overlay-example.dts" -I dts -O dtb -o - | \
		fdtoverlay -i "$artifacts/fdt.dtb" -o - - | lz4 | cat "$artifacts/bl31.gz" - "$artifacts/Image.zst" | usbtool --call sramstage.bin --run usbstage.bin --load 4400000 - --load 4000000 elfloader.bin --start 4000000 4102000; do true; done
fi

if [ -z "$skip" -o "$skip" == "7" ]; then
	echo "Configuration 7: levinboot with embedded SPI elfloader (zstd decompression)"
	"$src/configure.py" --with-tf-a-headers "$atf" --elfloader-{spi,zstd}
	ninja levinboot-usb.bin
	until prompt || usbtool --run levinboot-usb.bin;do true; done
fi

if [ -z "$skip" -o "$skip" == "8" ]; then
	echo "Configuration 8: levinboot SD image with gzip decompressor"
	"$src/configure.py" --with-tf-a-headers "$atf" --elfloader-{spi,gzip}
	ninja levinboot-sd.img
	echo "Build successful"
fi

if [ -z "$skip" -o "$skip" == "9" ]; then
	echo "Configuration 9: levinboot with embedded SD elfloader, configured for initcpio use (gzip decompression)"
	"$src/configure.py" --with-tf-a-headers "$atf" --elfloader-{sd,gzip,initcpio}
	ninja levinboot-usb.bin
	until prompt || usbtool --run levinboot-usb.bin;do true; done
fi

if [ -z "$skip" -o "$skip" == "10" ]; then
	echo "Configuration 10: flash levinboot SPI image configured for SD and SPI boot with initcpio (lz4, gzip and zstd decompression)"
	"$src/configure.py" --with-tf-a-headers "$atf" --elfloader-{sd,spi,lz4,gzip,zstd,initcpio}
	ninja levinboot-spi.img sramstage.bin usbstage.bin
	until prompt || usbtool --call sramstage.bin --run usbstage.bin --flash 0 levinboot-spi.img;do true; done
	read -p "now reset the board to try it out (both boot from SD and SPI), press enter to continue"
fi
