#!/usr/bin/env python3
import re, sys, os
import os.path as path
from collections import namedtuple

build = open("build.ninja", "w", encoding='utf-8')

escape_re = re.compile('(\\$|:| |\\n)')
def esc(s):
    return escape_re.sub('$\\1', s)

def build(out, rule, inp, deps=(), **overrides):
    res = 'build {out}: {rule} {inp}'.format(
        out=esc(out) if isinstance(out, (str, bytes)) else " ".join(esc(s) for s in out),
        rule=rule,
        inp=esc(inp) if isinstance(inp, (str, bytes)) else " ".join(esc(s) for s in inp),
    )
    if deps:
        res += ' | ' + (esc(deps) if isinstance(deps, (str, bytes)) else " ".join(esc(s) for s in deps))
    for var, val in overrides.items():
        if val:
            res += '\n    {var} = {val}'.format(var=esc(var), val=esc(val))
    return res

srcdir = path.dirname(sys.argv[0])

cc = os.getenv('CC', 'cc')
cflags = os.getenv('CFLAGS', '-O3')
cflags += " -Wall -Wextra -Werror=all -Wno-error=unused-parameter  -Wno-error=comment -Werror=incompatible-pointer-types"
if cc.endswith('gcc'):
	cflags += '  -Werror=discarded-qualifiers'

print('''
cflags = -ffreestanding -fno-builtin -nodefaultlibs -nostdlib -isystem {src}/include -isystem . {cflags}
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
rule regtool
    command = ./regtool --read $in $flags --hex >$out

build idbtool: buildcc {src}/tools/idbtool.c
build levinboot.img: run levinboot-sd.bin | idbtool
    bin = ./idbtool
build regtool: buildcc {src}/tools/regtool.c

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

lib = ('timer', 'error', 'uart', 'mmu')
levinboot = ('main', 'pll', 'odt', 'lpddr4', 'moderegs', 'training', 'memorymap', 'mirror', 'ddrinit')
modules = levinboot + lib + ('memtest', 'elfloader', 'teststage')
flags = {}

for f in modules:
    print('build {}.o: cc {}'.format(f, esc(path.join(srcdir, f + '.c'))))
    if f in flags:
	    print(' flags =', flags[f])

print('build dcache.o: cc {}'.format(esc(path.join(srcdir, 'dcache.S'))))
lib += ('dcache',)

regtool_job = namedtuple('regtool_job', ('input', 'flags'), defaults=(None,))
regtool_targets = {
    'pctl': regtool_job('pctl', flags="--mhz 400 800 50"),
    'pi': regtool_job('pi', flags="--mhz 50 800 400"),
    'dslice': regtool_job('dslice', flags="--set freq 0 --set dslice 0 --mhz 50 800 400"),
    'aslice0': regtool_job('aslice', flags="--set freq 0 --set aslice 0 --mhz 50 800 400"),
    'aslice1': regtool_job('aslice', flags="--set freq 0 --set aslice 1 --mhz 50 800 400"),
    'aslice2': regtool_job('aslice', flags="--set freq 0 --set aslice 2 --mhz 50 800 400"),
    'adrctl': regtool_job('adrctl', flags='--set freq 0 --mhz 50 800 400'),

    'dslice_f2': regtool_job('dslice', flags="--set freq 2 --set dslice 0 --mhz 50 800 400 --first 59 --last 90"),
    'slave_master_delays_f2': regtool_job('aslice', flags="--set freq 2 --set aslice 0 --mhz 50 800 400 --first 32 --last 37"),
    'grp_slave_delay_f2': regtool_job('adrctl', flags='--set freq 2 --mhz 50 800 400 --first 20 --last 22'),

    'dslice_f1': regtool_job('dslice', flags="--set freq 1 --set dslice 0 --mhz 50 800 400 --first 59 --last 90"),
    'slave_master_delays_f1': regtool_job('aslice', flags="--set freq 1 --set aslice 0 --mhz 50 800 400 --first 32 --last 37"),
    'grp_slave_delay_f1': regtool_job('adrctl', flags='--set freq 1 --mhz 50 800 400 --first 20 --last 22'),
}
for name, job in regtool_targets.items():
	print(build(name+'.gen.c', 'regtool', path.join(srcdir, job.input+'-fields.txt'), 'regtool', flags=job.flags))
print('build dramcfg.o: cc {src}/dramcfg.c | {deps}'.format(
	src=esc(srcdir),
	deps=" ".join(esc(name + ".gen.c") for name in regtool_targets.keys())
))
levinboot += ('dramcfg',)

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
binary('teststage', ('teststage', 'uart', 'error'), '00600000.ld')
