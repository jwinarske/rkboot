/* SPDX-License-Identifier: CC0-1.0 */
OUTPUT_FORMAT(elf64-littleaarch64)
ENTRY(main) 
start = 0x00100000;
SECTIONS {
	.text start : AT(start) {
		*(.entry)
		*(.text)
	}
	.rodata : {
		*(.rodata*)
	}
	.eh_frame : {
		*(.eh_frame)
	}
	.data : {
		*(.data)
	}
}
