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

default levinboot.img
'''.format(
    cflags=os.getenv('CFLAGS', '-Og -Wall -Wextra -Werror=all -Wno-error=unused-parameter -Wno-error=comment -Werror=discarded-qualifiers -Werror=incompatible-pointer-types'),
    ldflags=os.getenv('LDFLAGS', ''),
    src=esc(srcdir),
    cc=os.getenv('CC', 'cc'),
    ld=os.getenv('LD', 'ld'),
    buildcc=os.getenv('CC_BUILD', 'gcc'),
    buildcflags=os.getenv('CFLAGS_BUILD', ''),
    objcopy=os.getenv('OBJCOPY', 'objcopy'),
))

lib = ('timer', 'uart')
levinboot = ('main', 'pll', 'ddrinit', 'odt', 'lpddr4', 'moderegs', 'training')
modules = levinboot + lib

for f in modules:
    print('build {}.o: cc {}'.format(f, esc(path.join(srcdir, f + '.c'))))
print('''build levinboot.elf: ld {} | {script}
    flags = -T {script}'''.format(
    ' '.join(esc(x + '.o') for x in levinboot + lib),
    script=esc(path.join(srcdir, "linkerscript.ld"))
))
