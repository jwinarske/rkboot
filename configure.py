#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
import re, sys, os
import os.path as path
from collections import namedtuple, defaultdict
import argparse

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

# ===== parameter parsing and checking =====
flags = defaultdict(list)

parser = argparse.ArgumentParser(description='Configure the levinboot build.')
parser.add_argument(
    '--with-tf-a-headers',
    type=str,
    dest='tf_a_headers',
    help='path to TF-A export headers'
)
parser.add_argument(
    '--full-debug',
    action='store_true',
    dest='full_debug',
    help='add full debug message output'
)
parser.add_argument(
    '--debug',
    action='store',
    type=str,
    dest='debug',
    help='modules to select debug verbosity for (comma-separated)'
)
parser.add_argument(
    '--spew',
    action='store',
    type=str,
    dest='spew',
    help='modules to select debug verbosity for (comma-separated)'
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
    '--elfloader-poll',
    action='store_true',
    dest='elfloader_poll',
    help='use polling instead of IRQs for loading payloads'
)
parser.add_argument(
    '--payload-spi',
    action='append_const',
    dest='boot_media',
    const='spi',
    help='configure dramstage to load its images from SPI flash'
)
parser.add_argument(
    '--payload-emmc',
    action='append_const',
    dest='boot_media',
    const='emmc',
    help='configure dramstage to load its images from an SD card'
)
parser.add_argument(
    '--payload-sd',
    action='append_const',
    dest='boot_media',
    const='sd',
    help='configure dramstage to load its images from an SD card'
)
parser.add_argument(
    '--payload-initcpio',
    action='store_true',
    dest='elfloader_initcpio',
    help='configure dramstage to load an initcpio'
)
parser.add_argument(
    '--payload-lz4',
    action='append_const',
    dest='decompressors',
    const='lz4',
    help='configure dramstage to decompress its payload using LZ4'
)
parser.add_argument(
    '--payload-gzip',
    action='append_const',
    dest='decompressors',
    const='gzip',
    help='configure dramstage to decompress its payload using gzip'
)
parser.add_argument(
    '--payload-zstd',
    action='append_const',
    dest='decompressors',
    const='zstd',
    help='configure dramstage to decompress its payload using zstd'
)
args = parser.parse_args()
if args.crc:
    flags['main'].append('-DCONFIG_CRC')
flags['memtest'].append(memtest_prngs[args.memtest_prng])
if args.uncached_memtest:
    flags['memtest'].append('-DUNCACHED_MEMTEST')
if args.elfloader_initcpio:
    for f in ('elfloader', 'dramstage/commit', 'dramstage/decompression'):
        flags[f].append('-DCONFIG_ELFLOADER_INITCPIO')

for f in (args.debug or '').split(','):
    flags[f].append('-DDEBUG_MSG')
for f in (args.spew or '').split(','):
    flags[f].extend(('-DDEBUG_MSG', '-DSPEW_MSG'))

boot_media = set(args.boot_media or [])
decompressors = set(args.decompressors or [])

if (bool(boot_media) or args.elfloader_initcpio) and not decompressors:
    print("WARNING: boot medium and initcpio support require decompression support, enabling zstd")
    elfloader_decompression = True
    args.elfloader_zstd = True

if args.tf_a_headers:
    flags['dramstage/commit'].append(shesc('-DTF_A_HEADER_PATH="'+cesc(path.join(args.tf_a_headers, "common/bl_common_exp.h"))+'"'))
elif boot_media or decompressors:
    print(
        "ERROR: booting a kernel requires TF-A support, which is enabled by providing --with-tf-a-headers.\n"
        + "If you just want memtest and/or usbstage, don't configure with boot medium or decompression support"
    )
    sys.exit(1)

use_irq = not args.elfloader_poll and bool(boot_media)
for m in ('elfloader', 'dramstage/blk_sd', 'rk3399_spi'):
    flags[m].append('-DCONFIG_ELFLOADER_IRQ='+('1' if use_irq else '0'))

flags['dramstage/blk_sd'].append("-DCONFIG_ELFLOADER_DMA=1")

if bool(decompressors) and not boot_media:
    flags['dramstage/decompression'].append('-DCONFIG_ELFLOADER_MEMORY=1')

# ===== ninja skeleton =====
srcdir = path.dirname(sys.argv[0])
buildfile = open("build.ninja", "w", encoding='utf-8')
sys.stdout = buildfile

cc = os.getenv('CC', 'cc')
is_gcc = cc.endswith('gcc')
warnflags = os.getenv('WARNFLAGS')
if not warnflags:
    warnflags = "-Wall -Wextra -Werror=all -Wno-error=unused-parameter  -Wno-error=comment -Werror=incompatible-pointer-types -Wmissing-declarations"
    if is_gcc:
        warnflags += '  -Werror=discarded-qualifiers'
cflags = os.getenv('CFLAGS')
if not cflags:
	cflags = '-O3'
	if is_gcc:
		cflags += ' -mcpu=cortex-a72.cortex-a53+crc'
extracflags = os.getenv('EXTRACFLAGS')
if not extracflags:
	extracflags = '-fno-pic -ffreestanding -nodefaultlibs -nostdlib -march=armv8-a+crc -mgeneral-regs-only'
cflags = (
    extracflags
    + " -isystem . " + " ".join("-isystem " + path.join(srcdir, p) for p in (
        'include', 'compression', 'include/std'
    ))
    + " " + cflags
    + " " + warnflags)

genld = path.join(srcdir, 'gen_linkerscript.sh')

print('''
cflags ={cflags}
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
build regtool: buildcc {src}/tools/regtool.c {src}/tools/regtool_rpn.c
'''.format(
    cflags=esc(cflags),
    ldflags=os.getenv('LDFLAGS', ''),
    src=esc(srcdir),
    cc=cc,
    ld=os.getenv('LD', 'ld'),
    buildcc=os.getenv('CC_BUILD', 'cc'),
    buildcflags=os.getenv('CFLAGS_BUILD', ''),
    objcopy=os.getenv('OBJCOPY', 'objcopy'),
    genld=esc(genld)
))

# ===== C compile jobs =====
lib = {'lib/error', 'lib/uart', 'lib/mmu', 'lib/gicv2', 'lib/sched'}
levinboot = {'main', 'pll', 'sramstage/pmu_cru', 'sramstage/misc_init'} | {'dram/' + x for x in ('odt', 'lpddr4', 'moderegs', 'training', 'memorymap', 'mirror', 'ddrinit')}
elfloader = {'elfloader', 'dramstage/transform_fdt', 'lib/rki2c', 'dramstage/commit', 'dramstage/entropy'}
boot_media_handlers = ('main', 'elfloader')
if decompressors:
    flags['elfloader'].append('-DCONFIG_ELFLOADER_DECOMPRESSION')
    elfloader |= {'compression/lzcommon', 'lib/string', 'dramstage/decompression'}
if 'lz4' in decompressors:
    flags['dramstage/decompression'].append('-DHAVE_LZ4')
    elfloader |= {'compression/lz4'}
if 'gzip' in decompressors:
    flags['dramstage/decompression'].append('-DHAVE_GZIP')
    elfloader |= {'compression/inflate'}
if 'zstd' in decompressors:
    flags['dramstage/decompression'].append('-DHAVE_ZSTD')
    elfloader |= {'lib/string', 'compression/zstd', 'compression/zstd_fse', 'compression/zstd_literals', 'compression/zstd_probe_literals', 'compression/zstd_sequences'}
if 'spi' in boot_media:
    for f in boot_media_handlers:
        flags[f].append('-DCONFIG_SPI=1')
    elfloader |= {'lib/rkspi', 'rk3399_spi'}
if 'emmc' in boot_media:
    for f in boot_media_handlers:
        flags[f].append('-DCONFIG_EMMC=1')
    emmc_modules = {'lib/sdhci_common', 'rk3399_emmcphy'}
    levinboot |= emmc_modules | {'sramstage/emmc_init'}
    elfloader |= emmc_modules | {'dramstage/blk_emmc', 'lib/sdhci', 'dramstage/boot_blockdev'}
if 'sd' in boot_media:
    for f in boot_media_handlers:
        flags[f].append('-DCONFIG_SD=1')
    sdmmc_modules = {'lib/dwmmc_common', 'lib/sd'}
    levinboot |= sdmmc_modules | {'sramstage/sd_init', 'lib/dwmmc_early'}
    elfloader |= sdmmc_modules | {'dramstage/blk_sd', 'lib/dwmmc', 'lib/dwmmc_xfer', 'dramstage/boot_blockdev'}
usbstage = {'usbstage', 'lib/dwc3', 'usbstage-spi', 'lib/rkspi'}
dramstage_embedder =  {'sramstage/embedded_dramstage', 'compression/lzcommon', 'compression/lz4', 'lib/string'}
modules = lib | levinboot | elfloader | usbstage | {'sramstage/return_to_brom', 'teststage', 'lib/dump_fdt', 'memtest'}
if boot_media:
    modules |= dramstage_embedder

if args.full_debug:
    for f in modules:
        flags[f].append('-DDEBUG_MSG')

for f in modules:
    d, sep, base = f.rpartition("/")
    build_flags = {'flags': " ".join(flags[f])} if f in flags else {}
    src = path.join(srcdir, f+'.c')
    print(build(base+'.o', 'cc', src, **build_flags))

# ===== special compile jobs =====
print('build dcache.o: cc {}'.format(esc(path.join(srcdir, 'lib/dcache.S'))))
print(build('exc_handlers.o', 'cc', path.join(srcdir, 'lib/exc_handlers.S')))
print(build('gicv3.o', 'cc', path.join(srcdir, 'lib/gicv3.S')))
print(build('sched_aarch64.o', 'cc', path.join(srcdir, 'lib/sched_aarch64.S')))
print(build('string_aarch64.o', 'cc', path.join(srcdir, 'lib/string_aarch64.S')))
print(build('entry.o', 'cc', path.join(srcdir, 'entry.S')))
print(build('entry-first.o', 'cc', path.join(srcdir, 'entry.S'), flags='-DFIRST_STAGE'))
lib |= {'dcache', 'entry', 'exc_handlers', 'gicv3', 'sched_aarch64', 'string_aarch64'}
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

# ===== linking and image post processing =====
levinboot = (levinboot | lib | {'entry-first'}) - {'entry'}
print(build('dramstage.lz4', 'lz4', 'elfloader.bin', flags='--content-size'))
print(build('dramstage.lz4.o', 'incbin', 'dramstage.lz4'))
dramstage_embedder |= {'dramstage.lz4'}

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

binary('sramstage', levinboot | {'sramstage/return_to_brom'}, 'ff8c2000')
binary('memtest', {'memtest'} | lib, 'ff8c2000')
binary('usbstage', usbstage | lib, 'ff8c2000')
binary('teststage', ('teststage', 'uart', 'error', 'dump_fdt'), '00680000')
print("default sramstage.bin memtest.bin usbstage.bin teststage.bin")
if args.tf_a_headers:
    binary('elfloader', elfloader | lib, '04000000')
    print("default elfloader.bin")
if boot_media:
    binary('levinboot-img', levinboot | dramstage_embedder, 'ff8c2004')
    binary('levinboot-usb', levinboot | dramstage_embedder, 'ff8c2000')
    print(build('levinboot-spi.img', 'run', 'levinboot-img.bin', deps='idbtool', bin='./idbtool', flags='spi'))
    print(build('levinboot-sd.img', 'run', 'levinboot-img.bin', deps='idbtool', bin='./idbtool'))
    print("default levinboot-sd.img levinboot-spi.img levinboot-usb.bin")

for addr in base_addresses:
    print(build(addr + '.ld', 'ldscript', (), deps=genld, flags="0x"+addr))
