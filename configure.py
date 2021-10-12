#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
import re, sys, os
import os.path as path
from collections import namedtuple, defaultdict
from functools import partial
import argparse

escape_re = re.compile('(\\$|:| |\\n)')
def esc(s):
    return escape_re.sub('$\\1', s)

class NinjaFile:
    def __init__(self, file):
        self.file = file

    def __call__(self, out, rule, inp, deps=(), **overrides):
        if isinstance(out, (str, bytes)):
            out = (out,)
        if isinstance(inp, (str, bytes)):
            inp = (inp,)
        res = 'build {out}: {rule} {inp}'.format(
            out=" ".join(esc(s) for s in out),
            rule=rule,
            inp=" ".join(esc(s) for s in inp),
        )
        if deps:
            res += ' | ' + (esc(deps) if isinstance(deps, (str, bytes)) else " ".join(esc(s) for s in deps))
        for var, val in overrides.items():
            if val:
                res += '\n    {var} = {val}'.format(var=esc(var), val=esc(val))
        self.file.write(res + '\n')

    def comment(self, comment):
        for line in comment.split('\n'):
            self.file.write(f'# {line}\n')

    def rule(self, name, command, **kwargs):
        self.file.write(f'rule {name}\n    command = {command}\n')
        for name, value in kwargs.items():
            self.file.write(f'    {name} = {value}\n')

    def glb_var(self, name, value):
        self.file.write(f'{name} = {value}\n')

    def default(self, *targets):
        self.file.write(f'default {" ".join(esc(f) for f in targets)}\n')

shescape_re = re.compile('( |\\$|\\?|\\n|>|<|\'|")')
def shesc(s):
    return shescape_re.sub('\\\\\\1', s)

def cesc(s): return s.replace('"', '\\"')

# ===== parameter parsing and checking =====
flags = defaultdict(list)
flags.update({
    'lib/uart16550a': [
        '-DCONFIG_CONSOLE_FIFO_DEPTH=64',
    ],
    'lib/uart': ['-DCONFIG_BUF_SIZE=128'],
    'aarch64/mmu.S': ['-DASSERTIONS=1', '-DDEV_ASSERTIONS=0']
})

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
    '--payload-nvme',
    action='append_const',
    dest='boot_media',
    const='nvme',
    help='configure dramstage to load its images from an NVMe drive'
)
parser.add_argument(
    '--payload-initcpio',
    action='store_true',
    dest='dramstage_initcpio',
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
flags['rk3399/memtest'].append(memtest_prngs[args.memtest_prng])
if args.uncached_memtest:
    flags['rk3399/memtest'].append('-DUNCACHED_MEMTEST')
if args.dramstage_initcpio:
    for f in ('dramstage/main', 'dramstage/commit', 'dramstage/decompression'):
        flags[f].append('-DCONFIG_DRAMSTAGE_INITCPIO')

for f in (args.debug or '').split(','):
    flags[f].append('-DDEBUG_MSG')
for f in (args.spew or '').split(','):
    flags[f].extend(('-DDEBUG_MSG', '-DSPEW_MSG'))

boot_media = set(args.boot_media or [])
decompressors = set(args.decompressors or [])

if (bool(boot_media) or args.dramstage_initcpio) and not decompressors:
    print("WARNING: boot medium and initcpio support require decompression support, enabling zstd")
    decompressors = ['zstd']

if args.tf_a_headers:
    flags['dramstage/commit'].append(shesc('-DTF_A_BL_COMMON_PATH="'+cesc(path.join(args.tf_a_headers, "common/bl_common_exp.h"))+'"'))
    flags['dramstage/commit'].append(shesc('-DTF_A_RK_PARAMS_PATH="'+cesc(path.join(args.tf_a_headers, "plat/rockchip/common/plat_params_exp.h"))+'"'))
elif boot_media or decompressors:
    print(
        "ERROR: booting a kernel requires TF-A support, which is enabled by providing --with-tf-a-headers.\n"
        + "If you just want memtest and/or usbstage, don't configure with boot medium or decompression support"
    )
    sys.exit(1)

if bool(decompressors) and not boot_media:
    flags['dramstage/decompression'].append('-DCONFIG_DRAMSTAGE_MEMORY=1')

# ===== ninja skeleton =====
srcdir = path.dirname(sys.argv[0])
buildfile = open("build.ninja", "w", encoding='utf-8')
build = NinjaFile(buildfile)

cc = os.getenv('CC', 'cc')
build.comment(f'C compiler: {cc}')
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
	extracflags = '-fno-pic -ffreestanding -nodefaultlibs -nostdlib -march=armv8-a+crc -mgeneral-regs-only -static'
cflags = (
    extracflags
    + " -isystem . " + " ".join("-isystem " + path.join(srcdir, p) for p in (
        'include', 'compression', 'include/std', 'aarch64/include', 'rk3399/include', '.'
    ))
    + " " + cflags
    + " " + warnflags)

ldflags = os.getenv('LDFLAGS', '')
extraldflags = os.getenv('EXTRALDFLAGS', '-Wl,--gc-sections -Wl,-static')
ldflags += " " + extraldflags

src = partial(path.join, srcdir)

build.glb_var('cflags', esc(cflags))
build.glb_var('ldflags', esc(ldflags))
build.glb_var('incbin_flags', '--rename-section .data=.rodata,alloc,load,readonly,data,contents')


build.rule('buildcc',
    depfile="$out.d",
    deps="gcc",
    command = '{cc} -MD -MF $out.d {cflags} $flags $in -o $out'.format(
        cc=os.getenv('CC_BUILD', 'cc'),
        cflags=os.getenv('CFLAGS_BUILD', '')
    ),
)
build.rule('cc', f'{cc} -MD -MF $out.d {cflags} $flags -c $in -o $out',
    depfile="$out.d",
    deps="gcc",
)
build.rule('ld', '{ld} $cflags $ldflags $flags $in -o $out'.format(
    ld=cc,
))
objcopy = os.getenv('OBJCOPY', 'objcopy')
build.rule('bin', f'{objcopy} -O binary $in $out')
build.rule('incbin', f'{objcopy} -I binary -O elf64-littleaarch64 -B aarch64 $incbin_flags $flags $in $out')
build.rule('run', '$bin $flags <$in >$out')
build.rule('regtool', './regtool $preflags --read $in $flags --hex >$out')
build.rule('ldscript', f'bash {esc(src("gen_linkerscript.sh"))} $flags >$out')
build.rule('lz4', 'lz4 -c $flags $in >$out')

build('idbtool', 'buildcc', src('tools/idbtool.c'))
build('regtool', 'buildcc', [src('tools/regtool.c'), src('tools/regtool_rpn.c')])

# ===== C compile jobs =====
lib = {'lib/error', 'lib/uart', 'lib/uart16550a', 'lib/mmu', 'lib/gicv2', 'lib/sched'}
sramstage = {'sramstage/main', 'rk3399/pll', 'sramstage/pmu_cru', 'sramstage/misc_init'} | {'dram/' + x for x in ('training', 'memorymap', 'mirror', 'ddrinit')}
dramstage = {'dramstage/main', 'dramstage/transform_fdt', 'lib/rki2c', 'dramstage/commit', 'dramstage/entropy'}
boot_media_handlers = ('sramstage/main', 'dramstage/main')
if decompressors:
    flags['dramstage/main'].append('-DCONFIG_DRAMSTAGE_DECOMPRESSION')
    dramstage |= {'compression/lzcommon', 'lib/string', 'dramstage/decompression'}
if 'lz4' in decompressors:
    flags['dramstage/decompression'].append('-DHAVE_LZ4')
    dramstage |= {'compression/lz4'}
if 'gzip' in decompressors:
    flags['dramstage/decompression'].append('-DHAVE_GZIP')
    dramstage |= {'compression/inflate'}
if 'zstd' in decompressors:
    flags['dramstage/decompression'].append('-DHAVE_ZSTD')
    dramstage |= {'lib/string', 'compression/zstd', 'compression/zstd_fse', 'compression/zstd_literals', 'compression/zstd_probe_literals', 'compression/zstd_sequences'}
if 'spi' in boot_media:
    for f in boot_media_handlers:
        flags[f].append('-DCONFIG_SPI=1')
    dramstage |= {'lib/rkspi', 'rk3399/spi'}
if 'emmc' in boot_media:
    for f in boot_media_handlers:
        flags[f].append('-DCONFIG_EMMC=1')
    emmc_modules = {'lib/sdhci_common', 'rk3399/emmcphy'}
    sramstage |= emmc_modules | {'sramstage/emmc_init'}
    dramstage |= emmc_modules | {'dramstage/blk_emmc', 'lib/sdhci', 'dramstage/boot_blockdev'}
if 'sd' in boot_media:
    for f in boot_media_handlers:
        flags[f].append('-DCONFIG_SD=1')
    sdmmc_modules = {'lib/dwmmc_common', 'lib/sd'}
    sramstage |= sdmmc_modules | {'sramstage/sd_init', 'lib/dwmmc_early'}
    dramstage |= sdmmc_modules | {'dramstage/blk_sd', 'lib/dwmmc', 'lib/dwmmc_xfer', 'dramstage/boot_blockdev'}
if 'nvme' in boot_media:
    flags['sramstage/main'].append('-DCONFIG_PCIE=1')
    flags['dramstage/main'].append('-DCONFIG_NVME=1')
    sramstage |= {'sramstage/pcie_init'}
    dramstage |= {'dramstage/blk_nvme', 'lib/nvme', 'lib/nvme_xfer', 'dramstage/boot_blockdev'}
usbstage = {'rk3399/usbstage', 'lib/dwc3', 'rk3399/usbstage-spi', 'lib/rkspi'}
dramstage_embedder =  {'sramstage/embedded_dramstage', 'compression/lzcommon', 'compression/lz4', 'lib/string'}
modules = lib | sramstage | dramstage | usbstage | {'sramstage/return_to_brom', 'rk3399/teststage', 'lib/dump_fdt', 'rk3399/memtest'}
if boot_media:
    modules |= dramstage_embedder
build.comment(f'modules: {" ".join(modules)}')

if args.full_debug:
    for f in modules:
        flags[f].append('-DDEBUG_MSG')

for f in modules:
    build_flags = {'flags': " ".join(flags[f])} if f in flags else {}
    build(f+'.o', 'cc', src(f+'.c'), **build_flags)

# ===== special compile jobs =====
for x in ('gicv3', 'save_restore', 'string'):
    build(f'aarch64/{x}.o', 'cc', src(f'aarch64/{x}.S'))
    lib.add(f'aarch64/{x}')
build('aarch64/dcache-el3.o', 'cc', src('aarch64/dcache.S'), flags='-DCONFIG_EL=3')
build('aarch64/dcache-el2.o', 'cc', src('aarch64/dcache.S'), flags='-DCONFIG_EL=2')
build('aarch64/context-el3.o', 'cc', src('aarch64/context.S'), flags='-DCONFIG_EL=3')
build('aarch64/context-el2.o', 'cc', src('aarch64/context.S'), flags='-DCONFIG_EL=2')
build('aarch64/mmu.S.o', 'cc', src('aarch64/mmu.S'), flags=' '.join(flags['aarch64/mmu.S']))
build('entry-ret2brom.o', 'cc', src('rk3399/entry.S'), flags='-DCONFIG_FIRST_STAGE=2')
build('entry-first.o', 'cc', src('rk3399/entry.S'), flags='-DCONFIG_FIRST_STAGE=1')
build('entry.o', 'cc', src('rk3399/entry.S'), flags='-DCONFIG_EL=3 -DCONFIG_FIRST_STAGE=0')
build('entry-el2.o', 'cc', src('rk3399/entry.S'), flags='-DCONFIG_EL=2 -DCONFIG_FIRST_STAGE=0')
build('rk3399/debug-el3.o', 'cc', src('rk3399/debug.S'), flags='-DCONFIG_EL=3')
build('rk3399/debug-el2.o', 'cc', src('rk3399/debug.S'), flags='-DCONFIG_EL=2')
build('cpu_onoff.o', 'cc', src('rk3399/cpu_onoff.S'))
build('rk3399/handlers-el3.o', 'cc', src('rk3399/handlers.c'), flags='-DCONFIG_EL=3')
build('rk3399/handlers-el2.o', 'cc', src('rk3399/handlers.c'), flags='-DCONFIG_EL=2')
lib |= {'aarch64/dcache-el3', 'entry', 'rk3399/handlers-el3', 'rk3399/debug-el3', 'aarch64/mmu.S', 'aarch64/context-el3'}

regtool_job = namedtuple('regtool_job', ('input', 'flags', 'macros'), defaults=([],))
phy_job = lambda input, freq, flags='', range=None: regtool_job(input, flags=f'--set freq {freq} --mhz 50 800 400 '+flags+('' if range is None else f' --first {range[0]} --last {range[1]}'), macros=('phy-macros',))
regtool_targets = {
    'pctl': regtool_job('pctl', flags="--mhz 400 800 50"),
    'pi': regtool_job('pi', flags="--mhz 50 800 400"),
    'dslice': phy_job('dslice', 0, flags="--set dslice 0"),
    'aslice0': phy_job('aslice', 0, flags="--set aslice 0"),
    'aslice1': phy_job('aslice', 0, flags="--set aslice 1"),
    'aslice2': phy_job('aslice', 0, flags="--set aslice 2"),
    'adrctl': phy_job('adrctl', 0),

    'dslice5_7_f2': phy_job('dslice', 2, range=(5, 7), flags='--set dslice 0'),
    'dslice59_90_f2': phy_job('dslice', 2, range=(59, 90), flags='--set dslice 0'),
    'slave_master_delays_f2': phy_job('aslice', 2, range=(32, 37), flags='--set aslice 0'),
    'adrctl17_22_f2': phy_job('adrctl', 2, range=(17, 22)),
    'adrctl28_44_f2': phy_job('adrctl', 2, range=(28, 44)),

    'dslice5_7_f1': phy_job('dslice', 1, range=(5, 7), flags='--set dslice 0'),
    'dslice59_90_f1': phy_job('dslice', 1, range=(59, 90), flags='--set dslice 0'),
    'slave_master_delays_f1': phy_job('aslice', 1, range=(32, 37), flags='--set aslice 0'),
    'adrctl17_22_f1': phy_job('adrctl', 1, range=(17, 22)),
    'adrctl28_44_f1': phy_job('adrctl', 1, range=(28, 44)),
}
for name, job in regtool_targets.items():
    macro_files = tuple(src(f'dram/{x}.txt') for x in job.macros)
    build(
	name+'.gen.c', 'regtool',
	src("dram", job.input+'-fields.txt'),
	('regtool',) + macro_files,
	preflags = ' '.join(f'--read {f}' for f in macro_files),
	flags=job.flags
    )
build('dramcfg.o', 'cc', src('dram/dramcfg.c'), (name + ".gen.c" for name in regtool_targets))
sramstage |= {'dramcfg'}

# ===== linking and image post processing =====
sramstage = (sramstage | lib) - {'entry'}
build('dramstage.lz4', 'lz4', 'dramstage.bin', flags='--content-size')
build('dramstage.lz4.o', 'incbin', 'dramstage.lz4')
dramstage_embedder |= {'dramstage.lz4', 'entry-first'}

base_addresses = set()
def binary(name, modules, base_address):
    base_addresses.add(base_address)
    build(
        name + '.elf',
        'ld',
        tuple(x + '.o' for x in set(modules)),
        deps=base_address + '.ld',
        flags='-T {}.ld'.format(base_address)
    )
    build(name + '.bin', 'bin', name + '.elf')

binary('sramstage', sramstage | {'entry-ret2brom', 'sramstage/return_to_brom'}, 'ff8c2000')
binary('memtest', {'rk3399/memtest', 'cpu_onoff'} | lib, 'ff8c2000')
binary('usbstage', usbstage | lib, 'ff8c2000')
binary('teststage', ('rk3399/teststage', 'entry-el2', 'aarch64/dcache-el2', 'aarch64/context-el2', 'rk3399/handlers-el2', 'rk3399/debug-el2', 'aarch64/mmu.S', 'lib/uart', 'lib/uart16550a', 'lib/error', 'lib/mmu', 'lib/dump_fdt', 'lib/sched'), '00280000')
build.default('sramstage.bin', 'memtest.bin', 'usbstage.bin', 'teststage.bin')
if args.tf_a_headers:
    binary('dramstage', dramstage | lib, '04000000')
    build.default('dramstage.bin')
if boot_media:
    binary('levinboot-img', sramstage | dramstage_embedder, 'ff8c2000')
    binary('levinboot-usb', sramstage | dramstage_embedder, 'ff8c2000')
    build('levinboot-spi.img', 'run', 'levinboot-img.bin', deps='idbtool', bin='./idbtool', flags='spi')
    build('levinboot-sd.img', 'run', 'levinboot-img.bin', deps='idbtool', bin='./idbtool')
    build.default('levinboot-sd.img', 'levinboot-spi.img', 'levinboot-usb.bin')

for addr in base_addresses:
    build(addr + '.ld', 'ldscript', (), deps=src("gen_linkerscript.sh"), flags="0x"+addr)
