cmake_minimum_required(VERSION 3.10.2)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are: Debug, Release, or MinSizeRel." FORCE)
    message(STATUS "CMAKE_BUILD_TYPE not set, defaulting to Release.")
endif ()

project(levinboot_tools
        VERSION "0.8.7"
        DESCRIPTION "levinboot bootloader tools"
        LANGUAGES C ASM
        )

#
# System Packages
#
find_package(PkgConfig REQUIRED)
pkg_check_modules(USB REQUIRED libusb-1.0)

#
# Check for Functions
#
include(CheckFunctionExists)
check_function_exists(memmem HAVE_MEMMEM)

#
# Targets
#
add_executable(idbtool idbtool.c)

add_executable(regtool regtool.c regtool_rpn.c)
if(HAVE_MEMMEM)
    target_compile_definitions(regtool PRIVATE HAVE_MEMMEM)
endif()

add_executable(unpacktool 
    unpacktool.c
    ../compression/lz4.c
    ../compression/lzcommon.c
    ../compression/inflate.c
    ../compression/zstd.c
    ../compression/zstd_fse.c
    ../compression/zstd_literals.c
    ../compression/zstd_probe_literals.c
    ../compression/zstd_sequences.c
)
add_compile_definitions(unpacktool PRIVATE HAVE_LZ4 HAVE_GZIP HAVE_ZSTD)

add_executable(usbtool usbtool.c)
target_include_directories(usbtool PRIVATE ${USB_INCLUDE_DIRS})
target_link_libraries(usbtool PRIVATE ${USB_LINK_LIBRARIES})

#
# Install
#
install(TARGETS idbtool DESTINATION bin)
install(TARGETS regtool DESTINATION bin)
install(TARGETS unpacktool DESTINATION bin)
install(TARGETS usbtool DESTINATION bin)
