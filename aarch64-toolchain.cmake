#
# MIT License
#
# Copyright (c) 2019 Joel Winarske
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

include_guard()

IF(NOT TRIPLE)
    set(GCC_TOOLCHAIN_PREFIX "")
else()
    set(GCC_TOOLCHAIN_PREFIX ${TRIPLE}-)
endif()

message(STATUS "Triple ................. ${TRIPLE}")

STRING(REGEX REPLACE "^([a-zA-Z0-9]+).*" "\\1" target_arch "${TRIPLE}")
message(STATUS "Triple Arch ............ ${target_arch}")

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ${target_arch})

if(NOT APP)
    set(APP ref_app)
endif()
if(NOT NAME)
    set(NAME ${TARGET})
endif()

if(MINGW OR CYGWIN OR WIN32)
    set(UTIL_SEARCH_CMD where)
elseif(UNIX OR APPLE)
    set(UTIL_SEARCH_CMD which)
endif()

execute_process(
  COMMAND ${UTIL_SEARCH_CMD} ${GCC_TOOLCHAIN_PREFIX}g++
  OUTPUT_VARIABLE BINUTILS_PATH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
get_filename_component(TOOLCHAIN_PATH ${BINUTILS_PATH} DIRECTORY)

message(STATUS "Toolchain Path ......... ${TOOLCHAIN_PATH}")

set(CMAKE_FIND_ROOT_PATH ${TOOLCHAIN_PATH})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_AR ${GCC_TOOLCHAIN_PREFIX}ar)
set(CMAKE_ASM_COMPILER ${GCC_TOOLCHAIN_PREFIX}gcc)
set(CMAKE_C_COMPILER ${GCC_TOOLCHAIN_PREFIX}gcc)

set(NM ${GCC_TOOLCHAIN_PREFIX}nm)
set(OBJDUMP ${GCC_TOOLCHAIN_PREFIX}objdump)
set(OBJCOPY ${GCC_TOOLCHAIN_PREFIX}objcopy)
set(READELF ${GCC_TOOLCHAIN_PREFIX}readelf)
set(SIZE ${GCC_TOOLCHAIN_PREFIX}size)
set(CMAKE_EXECUTABLE_SUFFIX .elf)

set(GCCFLAGS
    -g

    -Wall
    -Wextra
    -Werror=all
    -Wno-error=unused-parameter
    -Wno-error=comment
    -Werror=incompatible-pointer-types
    -Wmissing-declarations
    -Werror=discarded-qualifiers

    -Wno-error=return-type
    -Wno-error=unused-variable
    -Wno-error=enum-compare
    -Wno-error=incompatible-pointer-types
    -Wno-error=format=
    -Wno-error=unused-but-set-variable
    -Wno-error=stringop-overflow=

    -fno-pic
    -ffreestanding
    -mgeneral-regs-only
)

set(_CFLAGS ${GCCFLAGS}
    -x c
    -march=armv8-a+crc
    -mcpu=cortex-a72.cortex-a53+crc

    -nodefaultlibs
    -nostdlib

    -Wunsuffixed-float-constants
)

set(_AFLAGS ${GCCFLAGS}
    -march=armv8-a+crc
    -mcpu=cortex-a72.cortex-a53+crc

    -nodefaultlibs
    -nostdlib
)

set(_LDFLAGS ${GCCFLAGS}
    -march=armv8-a+crc
    -mcpu=cortex-a72.cortex-a53+crc
    -x none
    -Wl,--gc-sections
    -Wl,-Map,${APP}.map
)

set(PARSE_SYMBOL_OPTIONS --print-size)

# Postbuild binutil commands
set(POSTBUILD_GEN_HEX ${OBJCOPY} -O ihex ${APP}${CMAKE_EXECUTABLE_SUFFIX} ${APP}.hex)
set(POSTBUILD_GEN_S19 ${OBJCOPY} -O srec --srec-forceS3 --srec-len=16 ${APP}${CMAKE_EXECUTABLE_SUFFIX} ${APP}.s19)
set(POSTBUILD_GEN_BIN ${OBJCOPY} -O binary ${APP}${CMAKE_EXECUTABLE_SUFFIX} ${APP}.bin)
set(POSTBUILD_GEN_INCBIN ${OBJCOPY} -I binary -O elf64-littleaarch64 -B aarch64 --rename-section .data=.rodata,alloc,load,readonly,data,contents ${APP}${CMAKE_EXECUTABLE_SUFFIX} ${APP}.incbin)

#build.rule('run', '$bin $flags <$in >$out')
#build.rule('regtool', './regtool $preflags --read $in $flags --hex >$out')
#build.rule('ldscript', f'bash {esc(src("gen_linkerscript.sh"))} $flags >$out')
#build.rule('lz4', 'lz4 -c $flags $in >$out')


# Install Files
set(MAP_FILE ${CMAKE_BINARY_DIR}/${APP}.map)
set(SYMBOL_LISTING_FILE ${CMAKE_BINARY_DIR}/${APP}_readelf.txt)
set(HEX_FILE ${CMAKE_BINARY_DIR}/${APP}.hex)
set(BIN_FILE ${CMAKE_BINARY_DIR}/${APP}.bin)

# remove list item delimeter
string(REPLACE ";" " " CMAKE_C_FLAGS "${_CFLAGS}")
string(REPLACE ";" " " CMAKE_ASM_FLAGS "${_AFLAGS}")
string(REPLACE ";" " " CMAKE_LD_FLAGS "${_LDFLAGS}")
