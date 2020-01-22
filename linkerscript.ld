OUTPUT_FORMAT(elf64-littleaarch64)
ENTRY(main) 
start = 0xff8c2004;
SECTIONS {
	.text start : AT(start) {
		code = .;
		*(.entry)
		*(.text)
		*(.text)
		*(.rodata*)
	}
}
