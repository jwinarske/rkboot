# levinboot

## Building

install cross-compiler (gcc-aarch64).

    sudo apt install -y gcc-aarch64-linux-gnu

    sudo dnf install -y gcc-aarch64-linux-gnu

setup build variables

#### Linux

    CROSS=aarch64-linux-gnu
    export CC=$CROSS-gcc
    export OBJCOPY=$CROSS-objcopy
    export LD=$CROSS-ld

#### Mac arm64

    CROSS=aarch64-elf
    export CC=$CROSS-gcc
    export OBJCOPY=$CROSS-objcopy
    export LD=$CROSS-ld

if you want to use emmc, you have the option to enable high-speed emmc mode as a config parameter, but it was not working at the moment, so i left this out.
then I build it with CFLAG -mno-outline-atomics for GCC 10+ support

    git clone https://gitlab.com/DeltaGem/levinboot.git
    cd levinboot
    mkdir _build && cd _build
    CFLAGS="-O3 -mno-outline-atomics" ../configure.py --payload-{lz4,gzip,zstd,initcpio,sd,emmc,nvme,spi} --with-tf-a-headers ../../trusted-firmware-a/include/export --boards rp64,pbp
    ninja

### CMake

Two pass build to support Yocto Target/Native scheme

#### Host

    arch -arm64 cmake ../tools -DCMAKE_STAGING_PREFIX=`pwd`/../artifacts
    arch -arm64 make install

#### Target

    arch -arm64 cmake .. -DTRIPLE=aarch64-elf -DCMAKE_TOOLCHAIN_FILE=/Users/joel/development/levinboot/_build/../aarch64-toolchain.cmake -DCMAKE_MAKEFILE_VERBOSE=1 -DTF_A_HEADERS=`pwd`/../../trusted-firmware-a/include/export
