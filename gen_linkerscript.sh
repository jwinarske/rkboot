#!/usr/bin/env bash
# SPDX-License-Identifier: CC0-1.0
memory=SRAM
if [[ 0x$1 -lt 0xf8000000 ]] ; then
	memory=DRAM
fi
cat <<END
OUTPUT_FORMAT(elf64-littleaarch64)
END
echo __start__ = "0x$1;"
cat <<END
ENTRY(__start__)
MEMORY {
       SRAM : ORIGIN = 0xff8c0000, LENGTH = 192K
       DRAM : ORIGIN = 0x00000000, LENGTH = 0xf800000
}
SECTIONS {
	.text __start__ : AT(__start__) {
		*(.entry)
		*(.text)
	} >${memory}
	.rodata : {
		*(.rodata*)
		*(.data.rel.ro.local)
	} >${memory}
	.eh_frame : {
		*(.eh_frame)
	} >${memory}
	__ro_end__ = .;
	.data : ALIGN(0x1000){
		*(.data)
		__data_end__ = .;
	} >${memory}
	.bss : {
		__bss_start__ = .;
		*(.bss*)
		__bss_end__ = .;
	} >${memory}
	__end__ = .;
}
END
