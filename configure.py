#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
import re, sys, os
import os.path as path
from collections import namedtuple, defaultdict
import argparse

buildfile = open("build.ninja", "w", encoding='utf-8')

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

shescape_re = re.compile('( |\\$|\\?|\\n|>|<|\'|")')
def shesc(s):
    return shescape_re.sub('\\\\\\1', s)

def cesc(s): return s.replace('"', '\\"')

srcdir = path.dirname(sys.argv[0])
flags = defaultdict(list)

parser = argparse.ArgumentParser(description='Configure the levinboot build.')
parser.add_argument(
    '--with-atf-headers',
    type=str,
    dest='atf_headers',
    help='path to TF-A export headers'
)
parser.add_argument(
    '--full-debug',
    action='store_true',
    dest='full_debug',
    help='add full debug message output'
)
parser.add_argument(
    '--crc',
    action='store_true',
    dest='crc',
    help='compute and print a CRC32C at the beginning of each stage'
)
parser.add_argument(
    '--uncached-memtest',
    action='store_true',
    dest='uncached_memtest',
    help='configure the memtest binary to use device memory'
)
memtest_prngs = {
    'splittable': '-DMEMTEST_SPLITTABLE',
    'speck': '-DMEMTEST_SPECK',
    'chacha': '-DMEMTEST_CHACHAISH',
}
parser.add_argument(
    '--memtest-prng',
    type=str,
    dest='memtest_prng',
    choices=memtest_prngs.keys(),
    default='splittable',
    help='PRNG to use for the memtest binary'
)
parser.add_argument(
    '--embed-elfloader',
    action='store_true',
    dest='embed_elfloader',
    help='embed the elfloader stage into levinboot proper'
)
parser.add_argument(
    '--elfloader-poll',
    action='store_true',
    dest='elfloader_poll',
    help='use polling instead of IRQs for loading payloads'
)
parser.add_argument(
    '--elfloader-spi',
    action='store_true',
    dest='elfloader_spi',
    help='configure elfloader to load its images from SPI flash'
)
parser.add_argument(
    '--elfloader-sd',
    action='store_true',
    dest='elfloader_sd',
    help='configure elfloader to load its images from an SD card'
)
parser.add_argument(
    '--elfloader-initcpio',
    action='store_true',
    dest='elfloader_initcpio',
    help='configure elfloader to load an initcpio'
)
parser.add_argument(
    '--elfloader-lz4',
    action='store_true',
    dest='elfloader_lz4',
    help='configure elfloader to decompress its payload using LZ4'
)
parser.add_argument(
    '--elfloader-gzip',
    action='store_true',
    dest='elfloader_gzip',
    help='configure elfloader to decompress its payload using gzip'
)
parser.add_argument(
    '--elfloader-zstd',
    action='store_true',
    dest='elfloader_zstd',
    help='configure elfloader to decompress its payload using zstd'
)
args = parser.parse_args()
if args.atf_headers:
    flags['elfloader'].append(shesc('-DATF_HEADER_PATH="'+cesc(path.join(args.atf_headers, "common/bl_common_exp.h"))+'"'))
if args.crc:
    flags['main'].append('-DCONFIG_CRC')
flags['memtest'].append(memtest_prngs[args.memtest_prng])
if args.uncached_memtest:
    flags['memtest'].append('-DUNCACHED_MEMTEST')
if args.embed_elfloader:
    flags['main'].append('-DCONFIG_EMBED_ELFLOADER')
if args.elfloader_initcpio:
    flags['elfloader'].append('-DCONFIG_ELFLOADER_INITCPIO')

elfloader_decompression = args.elfloader_lz4 or args.elfloader_gzip or args.elfloader_zstd
if (args.elfloader_spi or args.elfloader_initcpio) and not elfloader_decompression:
    print("WARNING: --elfloader-spi and --elfloader-initcpio require decompression support, enabling zstd")
    elfloader_decompression = True
    args.elfloader_zstd = True

use_irq = not args.elfloader_poll and (args.elfloader_spi or args.elfloader_sd)
for m in ('elfloader', 'dramstage/blk_sd', 'rk3399_spi'):
    flags[m].append('-DCONFIG_ELFLOADER_IRQ='+('1' if use_irq else '0'))

flags['dramstage/blk_sd'].append("-DCONFIG_ELFLOADER_DMA=1")

if elfloader_decompression and not args.elfloader_spi and not args.elfloader_sd:
    flags['elfloader'].append('-DCONFIG_ELFLOADER_MEMORY=1')

sys.stdout = buildfile

cc = os.getenv('CC', 'cc')
cflags = os.getenv('CFLAGS', '-O3')
cflags += " -Wall -Wextra -Werror=all -Wno-error=unused-parameter  -Wno-error=comment -Werror=incompatible-pointer-types"
if cc.endswith('gcc'):
    cflags += '  -Werror=discarded-qualifiers'

genld = path.join(srcdir, 'gen_linkerscript.sh')

print('''
cflags = -fno-pic -ffreestanding -fno-builtin -nodefaultlibs -nostdlib -isystem {src}/include -isystem {src}/compression -isystem {src}/include/std -isystem . {cflags} -march=armv8-a+crc -mcpu=cortex-a72.cortex-a53+crc
ldflags = {ldflags}

incbin_flags = --rename-section .data=.rodata,alloc,load,readonly,data,contents

rule buildcc
    depfile = $out.d
    deps = gcc
    command = {buildcc} -MD -MF $out.d {buildcflags} $flags $in -o $out
rule cc
    depfile = $out.d
    deps = gcc
    command = {cc} $cflags -MD -MF $out.d $flags -c $in -o $out
rule ld
    command = {ld} $ldflags $flags $in -o $out
rule bin
    command = {objcopy} -O binary $in $out
rule incbin
    command = {objcopy} -I binary -O elf64-littleaarch64 -B aarch64 $incbin_flags $flags $in $out
rule run
    command = $bin $flags <$in >$out
rule regtool
    command = ./regtool --read $in $flags --hex >$out
rule ldscript
    command = bash {genld} $flags >$out
rule lz4
    command = lz4 -c $flags $in > $out

build idbtool: buildcc {src}/tools/idbtool.c
build levinboot-sd.img: run levinboot-img.bin | idbtool
    bin = ./idbtool
build levinboot-spi.img: run levinboot-img.bin | idbtool
    bin = ./idbtool
    flags = spi
build regtool: buildcc {src}/tools/regtool.c {src}/tools/regtool_rpn.c
'''.format(
    cflags=cflags,
    ldflags=os.getenv('LDFLAGS', ''),
    src=esc(srcdir),
    cc=cc,
    ld=os.getenv('LD', 'ld'),
    buildcc=os.getenv('CC_BUILD', 'cc'),
    buildcflags=os.getenv('CFLAGS_BUILD', ''),
    objcopy=os.getenv('OBJCOPY', 'objcopy'),
    genld=esc(genld)
))

lib = {'lib/error', 'lib/uart', 'lib/mmu', 'lib/gicv2', 'lib/sched'}
levinboot = {'main', 'pll', 'sramstage/pmu_cru'} | {'dram/' + x for x in ('odt', 'lpddr4', 'moderegs', 'training', 'memorymap', 'mirror', 'ddrinit')}
if args.embed_elfloader:
    levinboot |= {'compression/lzcommon', 'compression/lz4'}
elfloader = {'elfloader', 'transform_fdt', 'lib/rki2c'}
if elfloader_decompression:
    flags['elfloader'].append('-DCONFIG_ELFLOADER_DECOMPRESSION')
    elfloader |= {'compression/lzcommon', 'lib/string'}
    if args.elfloader_lz4:
        flags['elfloader'].append('-DHAVE_LZ4')
        elfloader |= {'compression/lz4'}
    if args.elfloader_gzip:
        flags['elfloader'].append('-DHAVE_GZIP')
        elfloader |= {'compression/inflate'}
    if args.elfloader_zstd:
        flags['elfloader'].append('-DHAVE_ZSTD')
        elfloader |= {'lib/string', 'compression/zstd', 'compression/zstd_fse', 'compression/zstd_literals', 'compression/zstd_probe_literals', 'compression/zstd_sequences'}
if args.elfloader_spi:
    flags['elfloader'].append('-DCONFIG_ELFLOADER_SPI=1')
    elfloader |= {'lib/rkspi', 'rk3399_spi'}
if args.elfloader_sd:
    sdmmc_modules = {'lib/dwmmc_common', 'lib/sd'}
    levinboot |= sdmmc_modules | {'rk3399_sdmmc', 'lib/dwmmc_early'}
    flags['main'].append('-DCONFIG_SD=1')
    elfloader |= sdmmc_modules | {'dramstage/blk_sd', 'lib/dwmmc'}
    flags['elfloader'].append('-DCONFIG_ELFLOADER_SD=1')
spi_flasher = {'brompatch-spi', 'lib/rkspi', 'brompatch'}
usbstage = {'usbstage', 'lib/dwc3', 'usbstage-spi', 'lib/rkspi'}
modules = lib | levinboot | elfloader | {'teststage', 'lib/dump_fdt'}
if not args.embed_elfloader:
    modules |= usbstage | spi_flasher | {'memtest', 'brompatch-mem'}

if args.full_debug:
    for f in modules:
        flags[f].append('-DDEBUG_MSG')

for f in modules:
    d, sep, base = f.rpartition("/")
    build_flags = {'flags': " ".join(flags[f])} if f in flags else {}
    src = path.join(srcdir, f+'.c')
    print(build(base+'.o', 'cc', src, **build_flags))

print('build dcache.o: cc {}'.format(esc(path.join(srcdir, 'lib/dcache.S'))))
print(build('exc_handlers.o', 'cc', path.join(srcdir, 'lib/exc_handlers.S')))
print(build('gicv3.o', 'cc', path.join(srcdir, 'lib/gicv3.S')))
print(build('sched_aarch64.o', 'cc', path.join(srcdir, 'lib/sched_aarch64.S')))
print(build('entry.o', 'cc', path.join(srcdir, 'entry.S')))
print(build('entry-first.o', 'cc', path.join(srcdir, 'entry.S'), flags='-DFIRST_STAGE'))
lib |= {'dcache', 'entry', 'exc_handlers', 'gicv3', 'sched_aarch64'}
spi_flasher |= {'exc_handlers'}
usbstage |= {'exc_handlers'}

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
    print(build(name+'.gen.c', 'regtool', path.join(srcdir, "dram", job.input+'-fields.txt'), 'regtool', flags=job.flags))
print(build('dramcfg.o', 'cc', path.join(srcdir, 'dram/dramcfg.c'), (name + ".gen.c" for name in regtool_targets)))
levinboot |= {'dramcfg'}

levinboot = (set(levinboot) | lib | {'entry-first'}) - {'entry'}
if args.embed_elfloader:
    print(build('elfloader.lz4', 'lz4', 'elfloader.bin', flags='--content-size'))
    print(build('elfloader.lz4.o', 'incbin', 'elfloader.lz4'))
    levinboot |= {'elfloader.lz4'}

base_addresses = set()
def binary(name, modules, base_address):
    base_addresses.add(base_address)
    print(build(
        name + '.elf',
        'ld',
        tuple(x.rpartition("/")[2] + '.o' for x in set(modules)),
        deps=base_address + '.ld',
        flags='-T {}.ld'.format(base_address)
    ))
    print(build(name + '.bin', 'bin', name + '.elf'))

binary('levinboot-usb', levinboot, 'ff8c2000')
binary('levinboot-img', levinboot, 'ff8c2004')
if not args.embed_elfloader:
    binary('memtest', {'memtest'} | lib, 'ff8c2000')
    binary('usbstage', usbstage | lib, 'ff8c2000')
    binary('brompatch', {'brompatch-mem', 'brompatch', 'exc_handlers'} | lib, '04100000')
    binary('spi-flasher', spi_flasher | lib, '04100000')
binary('teststage', ('teststage', 'uart', 'error', 'dump_fdt'), '00680000')
print("default levinboot-sd.img levinboot-spi.img levinboot-usb.bin teststage.bin")
if args.atf_headers:
    binary('elfloader', elfloader | lib, '04000000')
    print("default elfloader.bin")

for addr in base_addresses:
    print(build(addr + '.ld', 'ldscript', (), deps=genld, flags="0x"+addr))
