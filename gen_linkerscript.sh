#!/usr/bin/env bash
# SPDX-License-Identifier: CC0-1.0

self="$0"
cat <<END
OUTPUT_FORMAT(elf64-littleaarch64)
brom_recovery_mode_entry = 0xffff0100;
MEMORY {
       SRAM : ORIGIN = 0xff8c0000, LENGTH = 192K
       DRAM : ORIGIN = 0x00000000, LENGTH = 0xf800000
}
ENTRY(entry_point)
END
sections="/DISCARD/ : {*(.note*)}"
while test $# -gt 0; do
	case "$1" in
		0x*) addr="$1"
		shift
		if test $# -gt 0; then
			prefix="$1_"
			name="$1"
			shift
			objs=""
			while test $# -gt 0; do
				case "$1" in
					0x*) break;;
					*) objs="$objs $1"; shift;;
				esac
			done
		else
			prefix=""
			name="out"
			objs="*"
		fi
		echo __${prefix}start__ = "$addr;" >&2
		echo "    $objs"  >&2
		echo __${prefix}start__ = "$addr;"
		memory=SRAM
		if [[ $addr -lt 0xf8000000 ]] ; then
			memory=DRAM
		fi
		sections="$sections
	.text.$name __${prefix}start__ : AT(__${prefix}start__) {
		$objs(.text.entry)
		$objs(.text* .rodata* .eh_frame)
		__${prefix}ro_end__ = ALIGN(0x1000);
	} >${memory}
	.data.$name __${prefix}ro_end__  : {
		$objs(.data*)
		__${prefix}data_end__ = ALIGN(16);
	} >${memory}
	.bss.$name __${prefix}data_end__  : {
		__${prefix}bss_start__ = .;
		$objs(.bss)
		__${prefix}bss_noinit__ = .;
		$objs(.bss.noinit)
		__${prefix}end__ = .;
	} >${memory}
";;
		*) echo "unrecognized argument $0" >&2; exit 1;;
	esac
done
echo "SECTIONS {${sections}}"
