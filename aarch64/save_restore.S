#include <asm.h>

TEXTSECTION(.text.asm.save_locals)
asm_save_locals: .global asm_save_locals
	stp x0, x1, [x29, #0x90]
asm_save_locals2: .global asm_save_locals2
	stp x3, x2, [x29, #0x80]
asm_save_locals4: .global asm_save_locals4
	stp x5, x4, [x29, #0x70]
asm_save_locals6: .global asm_save_locals6
	stp x7, x6, [x29, #0x60]
asm_save_locals8: .global asm_save_locals8
	stp x9, x8, [x29, #0x50]
asm_save_locals10: .global asm_save_locals10
	stp x11, x10, [x29, #0x40]
asm_save_locals12: .global asm_save_locals12
	stp x13, x12, [x29, #0x30]
asm_save_locals14: .global asm_save_locals14
	stp x15, x14, [x29, #0x20]
	stp x17, x16, [x29, #0x10]
	ret

TEXTSECTION(.text.asm.restore_locals)
asm_restore_locals: .global asm_restore_locals
	ldp x0, x1, [x29, #0x90]
asm_restore_locals2: .global asm_restore_locals2
	ldp x3, x2, [x29, #0x80]
asm_restore_locals4: .global asm_restore_locals4
	ldp x5, x4, [x29, #0x70]
asm_restore_locals6: .global asm_restore_locals6
	ldp x7, x6, [x29, #0x60]
asm_restore_locals8: .global asm_restore_locals8
	ldp x9, x8, [x29, #0x50]
asm_restore_locals10: .global asm_restore_locals10
	ldp x11, x10, [x29, #0x40]
asm_restore_locals12: .global asm_restore_locals12
	ldp x13, x12, [x29, #0x30]
asm_restore_locals14: .global asm_restore_locals14
	ldp x15, x14, [x29, #0x20]
	ldp x17, x16, [x29, #0x10]
	ret

#define ODD(odd, even)\
	TEXTSECTION(.text.asm.save_locals##odd);\
	asm_save_locals##odd: .global asm_save_locals##odd;\
		str x##odd, [x29, #(0x90 - 8*odd)]; b asm_save_locals##even;\
	TEXTSECTION(.text.asm.restore_locals##odd);\
	asm_restore_locals##odd: .global asm_restore_locals##odd;\
		ldr x##odd, [x29, #(0x90 - 8*odd)]; b asm_restore_locals##even

ODD(1, 2)
ODD(3, 4)
ODD(5, 6)
ODD(7, 8)
ODD(9, 10)
ODD(11, 12)
ODD(13, 14)
