#!/usr/bin/env python3
import re, sys, os
import os.path as path

build = open("build.ninja", "w", encoding='utf-8')

escape_re = re.compile('\\$|:| |\\n')
def esc(s):
    return escape_re.sub('$\\0', s)

srcdir = path.dirname(sys.argv[0])

print('''
cflags = -ffreestanding -fno-builtin -nodefaultlibs -nostdlib -DENV_STAGE -isystem {src}/include {cflags}
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
build levinboot.bin: bin levinboot.elf
build levinboot.img: run levinboot.bin | idbtool
    bin = ./idbtool
build memtest.bin: bin memtest.elf

default levinboot.img
'''.format(
    cflags=os.getenv('CFLAGS', '-O3 -Wall -Wextra -Werror=all -Wno-error=unused-parameter -Wno-error=comment -Werror=discarded-qualifiers -Werror=incompatible-pointer-types'),
    ldflags=os.getenv('LDFLAGS', ''),
    src=esc(srcdir),
    cc=os.getenv('CC', 'cc'),
    ld=os.getenv('LD', 'ld'),
    buildcc=os.getenv('CC_BUILD', 'gcc'),
    buildcflags=os.getenv('CFLAGS_BUILD', ''),
    objcopy=os.getenv('OBJCOPY', 'objcopy'),
))

lib = ('timer', 'uart', 'error', 'mmu')
levinboot = ('main', 'pll', 'ddrinit', 'odt', 'lpddr4', 'moderegs', 'training', 'memorymap', 'mirror')
modules = levinboot + lib + ('memtest',)
flags = {}

for f in modules:
    print('build {}.o: cc {}'.format(f, esc(path.join(srcdir, f + '.c'))))
    if f in flags:
	    print(' flags =', flags[f])

print('build dcache.o: cc {}'.format(esc(path.join(srcdir, 'dcache.S'))))
lib += ('dcache',)

def binary(name, modules):
	print(
'''build {}: ld {} | {script}
    flags = -T {script}'''
    .format(
        esc(name),
        ' '.join(esc(x + '.o') for x in modules),
        script=esc(path.join(srcdir, "linkerscript.ld"))
    ))

binary('levinboot.elf', levinboot + lib)
binary('memtest.elf', ('memtest',) + lib)
