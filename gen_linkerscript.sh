#!/bin/sh
# SPDX-License-Identifier: CC0-1.0
cat <<END
OUTPUT_FORMAT(elf64-littleaarch64)
END
echo __start__ = "0x$1;"
cat <<END
ENTRY(__start__)
SECTIONS {
	.text __start__ : AT(__start__) {
		*(.entry)
		*(.text)
	}
	.rodata : {
		*(.rodata*)
		*(.data.rel.ro.local)
	}
	.eh_frame : {
		*(.eh_frame)
	}
	__ro_end__ = .;
	.data : ALIGN(0x1000){
		*(.data)
		__data_end__ = .;
	}
	.bss : {
		__bss_start__ = .;
		*(.bss*)
		__bss_end__ = .;
	}
	__end__ = .;
}
END
