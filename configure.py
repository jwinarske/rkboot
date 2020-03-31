#!/usr/bin/env python3
import re, sys, os
import os.path as path

build = open("build.ninja", "w", encoding='utf-8')

escape_re = re.compile('\\$|:| |\\n')
def esc(s):
    return escape_re.sub('$\\0', s)

srcdir = path.dirname(sys.argv[0])

cc = os.getenv('CC', 'cc')
cflags = os.getenv('CFLAGS', '-O3')
cflags += " -Wall -Wextra -Werror=all -Wno-error=unused-parameter  -Wno-error=comment -Werror=incompatible-pointer-types"
if cc.endswith('gcc'):
	cflags += '  -Werror=discarded-qualifiers'

print('''
cflags = -ffreestanding -fno-builtin -nodefaultlibs -nostdlib -isystem {src}/include {cflags}
ldflags = {ldflags}

rule buildcc
    command = {buildcc} {buildcflags} $flags $in -o $out
rule cc
    depfile = $out.d
    deps = gcc
    command = {cc} $cflags -MD -MF $out.d $flags -c $in -o $out
rule ld
    command = {ld} $ldflags $flags $in -o $out
rule bin
    command = {objcopy} -O binary $in $out
rule run
    command = $bin <$in >$out

build idbtool: buildcc {src}/tools/idbtool.c
build levinboot.img: run levinboot-sd.bin | idbtool
    bin = ./idbtool

default levinboot.img
'''.format(
    cflags=cflags,
    ldflags=os.getenv('LDFLAGS', ''),
    src=esc(srcdir),
    cc=cc,
    ld=os.getenv('LD', 'ld'),
    buildcc=os.getenv('CC_BUILD', 'cc'),
    buildcflags=os.getenv('CFLAGS_BUILD', ''),
    objcopy=os.getenv('OBJCOPY', 'objcopy'),
))

lib = ('timer', 'uart', 'error', 'mmu')
levinboot = ('main', 'pll', 'ddrinit', 'odt', 'lpddr4', 'moderegs', 'training', 'memorymap', 'mirror')
modules = levinboot + lib + ('memtest', 'elfloader')
flags = {}

for f in modules:
    print('build {}.o: cc {}'.format(f, esc(path.join(srcdir, f + '.c'))))
    if f in flags:
	    print(' flags =', flags[f])

print('build dcache.o: cc {}'.format(esc(path.join(srcdir, 'dcache.S'))))
lib += ('dcache',)

def binary(name, modules, linkerscript):
	print(
'''build {name}.elf: ld {modules} | {script}
    flags = -T {script}
build {name}.bin: bin {name}.elf'''
    .format(
        name=esc(name),
        modules=' '.join(esc(x + '.o') for x in modules),
        script=esc(path.join(srcdir, 'ld', linkerscript))
    ))

binary('levinboot-usb', levinboot + lib, 'ff8c2000.ld')
binary('levinboot-sd', levinboot + lib, 'ff8c2004.ld')
binary('memtest', ('memtest',) + lib, 'ff8c2000.ld')
binary('elfloader', ('elfloader',) + lib, '00100000.ld')
binary('elfloader-sram', ('elfloader',) + lib, 'ff8c2000.ld')
