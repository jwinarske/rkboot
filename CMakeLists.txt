cmake_minimum_required(VERSION 3.10.2)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are: Debug, Release, or MinSizeRel." FORCE)
    message(STATUS "CMAKE_BUILD_TYPE not set, defaulting to Release.")
endif ()

project(levinboot
        VERSION "0.8.7"
        DESCRIPTION "levinboot bootloader"
        LANGUAGES C ASM
        )

if(NOT CMAKE_CROSSCOMPILING)
    add_subdirectory(tools)
else()
    find_program(IDBTOOL idbtool HINTS artifacts/bin artifacts REQUIRED)
    find_program(REGTOOL regtool HINTS artifacts/bin artifacts REQUIRED)
    find_program(UNPACKTOOL unpacktool HINTS artifacts/bin artifacts REQUIRED)
    find_program(USBTOOL usbtool HINTS artifacts/bin artifacts REQUIRED)

    set(DRAMSTAGE_DECOMPRESS_DEFS)
    set(DRAMSTAGE_COMMIT_DEFS)
    set(DRAMSTAGE_BOARD_PROBE_DEFS)

    set(BUILD_RP64_BOARD "Build for rp64 board" ON)
    if (BUILD_RP64_BOARD)
        list(APPEND DRAMSTAGE_BOARD_PROBE_DEFS CONFIG_BOARD_RP64=1)
    endif()
    set(BUILD_PBP_BOARD "Build for pbp board" ON)
    if (BUILD_PBP_BOARD)
        list(APPEND DRAMSTAGE_BOARD_PROBE_DEFS CONFIG_BOARD_PBP=1)
    endif()
    set_property(SOURCE dramstage/board_probe.c PROPERTY COMPILE_DEFINITIONS ${DRAMSTAGE_BOARD_PROBE_DEFS} CONFIG_SINGLE_BOARD=0)
    get_property(DEFS SOURCE dramstage/board_probe.c PROPERTY COMPILE_DEFINITIONS)
    message(STATUS "dramstage/board_probe.c: ${DEFS}")


    set(BOOT_MEDIA)
    option(BUILD_PAYLOAD_SPI "configure dramstage to load its images from SPI flash" ON)
    if(BUILD_PAYLOAD_SPI)
        list(APPEND BOOT_MEDIA spi)
    endif()
    option(BUILD_PAYLOAD_EMMC "configure dramstage to load its images from eMMC flash" ON)
    if(BUILD_PAYLOAD_EMMC)
        list(APPEND BOOT_MEDIA emmc)
    endif()
    option(BUILD_PAYLOAD_SD "configure dramstage to load its images from an SD card" ON)
    if(BUILD_PAYLOAD_SD)
        list(APPEND BOOT_MEDIA sd)
    endif()
    option(BUILD_PAYLOAD_NVME "configure dramstage to load its images from an NVMe drive" ON)
    if(BUILD_PAYLOAD_NVME)
        list(APPEND BOOT_MEDIA nvme)
    endif()
    option(BUILD_PAYLOAD_INITCIPO "configure dramstage to load an initcpio" ON)
    if(BUILD_PAYLOAD_INITCIPO)
        list(APPEND BOOT_MEDIA initcipo)
        list(APPEND DRAMSTAGE_MAIN_C CONFIG_DRAMSTAGE_INITCPIO)
        list(APPEND DRAMSTAGE_DECOMPRESS_DEFS CONFIG_DRAMSTAGE_INITCPIO)
        list(APPEND DRAMSTAGE_COMMIT_DEFS CONFIG_DRAMSTAGE_INITCPIO)
    endif()

    set(DECOMPRESSORS)
    option(BUILD_PAYLOAD_LZ4 "configure dramstage to decompress its payload using LZ4" ON)
    if(BUILD_PAYLOAD_LZ4)
        list(APPEND DECOMPRESSORS lz4)
    endif()
    option(BUILD_PAYLOAD_GZIP "configure dramstage to decompress its payload using gzip" ON)
    if(BUILD_PAYLOAD_GZIP)
        list(APPEND DECOMPRESSORS gzip)
    endif()
    option(BUILD_PAYLOAD_ZSTD "configure dramstage to decompress its payload using zstd" ON)
    if(BUILD_PAYLOAD_ZSTD)
        list(APPEND DECOMPRESSORS zstd)
    endif()

    if (BOOT_MEDIA OR BUILD_PAYLOAD_INITCIPO AND NOT DECOMPRESSORS)
        message(WARNING "Boot medium and initcpio support require decompression support, enabling zstd")
        list(APPEND DECOMPRESSORS zstd)
    endif()

    option(BUILD_FULL_DEBUG "add full debug message output" OFF)
    # modules to select debug verbosity for (semi-colon-separated)
#    set(BUILD_DEBUG "")
    # iterate list adding -DDEBUG_MSG
#    set(BUILD_SPEW "")
    # iterate list adding -DDEBUG_MSG -DSPEW_MSG

    set(SRAMSTAGE_MEMTEST_DEFS)
    option(BUILD_UNCACHED_MEMTEST "configure the memtest binary to use device memory" OFF)
    if(BUILD_UNCACHED_MEMTEST)
        list(APPEND SRAMSTAGE_MEMTEST_DEFS UNCACHED_MEMTEST)
    endif()

    set(BUILD_MEMTEST_PRNGS "splittable" CACHE STRING "Choose the type of PRNG to use for the memtest binary, options are: splittable, speck, or chacha." FORCE)
    if ("splittable" STREQUAL BUILD_MEMTEST_PRNGS)
        list(APPEND SRAMSTAGE_MEMTEST_DEFS MEMTEST_SPLITTABLE)
    elseif ("speck" STREQUAL MEMTEST_SPECK)
        list(APPEND SRAMSTAGE_MEMTEST_DEFS MEMTEST_SPECK)
    elseif ("chacha" STREQUAL MEMTEST_SPECK)
        list(APPEND SRAMSTAGE_MEMTEST_DEFS MEMTEST_CHACHAISH)
    endif()

    set_property(SOURCE sramstage/memtest.c PROPERTY COMPILE_DEFINITIONS ${SRAMSTAGE_MEMTEST_DEFS})
    get_property(DEFS SOURCE sramstage/memtest.c PROPERTY COMPILE_DEFINITIONS)
    message(STATUS "sramstage/memtest.c: ${DEFS}")


    include_directories(
        include
        compression
        include/std
        aarch64/include
        rk3399/include
        ${CMAKE_BINARY_DIR}
        .
    )

    set(LIB_SRC
        lib/error.c
        lib/uart.c
        lib/uart16550a.c
        lib/mmu.c
        lib/gicv2.c
        lib/sched.c
    )

    set(SRAMSTAGE_SRC
        sramstage/main.c
        rk3399/pll.c
        sramstage/pmu_cru.c
        sramstage/misc_init.c
        dram/training.c
        dram/memorymap.c
        dram/mirror.c
        dram/ddrinit.c
        rk3399/entry.S
    )

    set(DRAMSTAGE_SRC
        dramstage/main.c
        dramstage/transform_fdt.c
        lib/rki2c.c
        dramstage/commit.c
        dramstage/entropy.c
        dramstage/board_probe.c
        dram/read_size.c
    )

    set(DRAMSTAGE_EMBEDDER_SRC
        sramstage/embedded_dramstage.c
        compression/lzcommon.c
        compression/lz4.c
        lib/string.c
    )

    set(USB_LOADER_SRC
        sramstage/usb_loader.c
        lib/dwc3.c
        sramstage/usb_loader-spi.c
        lib/rkspi.c
    )

    set(MEMTEST_SRC
        sramstage/memtest.c
        dram/read_size.c
        rk3399/cpu_onoff.S
    )

    set_property(SOURCE lib/uart.c        PROPERTY COMPILE_DEFINITIONS CONFIG_BUF_SIZE=128)
    set_property(SOURCE lib/uart16550a.c  PROPERTY COMPILE_DEFINITIONS CONFIG_CONSOLE_FIFO_DEPTH=64)
    set_property(SOURCE aarch64/mmu_asm.S PROPERTY COMPILE_DEFINITIONS ASSERTIONS=1 DEV_ASSERTIONS=0)

    get_property(DEFS SOURCE lib/uart.c        PROPERTY COMPILE_DEFINITIONS)
    get_property(DEFS SOURCE lib/uart16550a.c  PROPERTY COMPILE_DEFINITIONS)
    get_property(DEFS SOURCE aarch64/mmu_asm.S PROPERTY COMPILE_DEFINITIONS)

    if(TF_A_HEADERS)
        list(APPEND DRAMSTAGE_COMMIT_DEFS
            TF_A_BL_COMMON_PATH="${TF_A_HEADERS}/common/bl_common_exp.h"
            TF_A_RK_PARAMS_PATH="${TF_A_HEADERS}/plat/rockchip/common/plat_params_exp.h"
        )
    elseif(BOOT_MEDIA OR DECOMPRESSORS)
        message(FATAL_ERROR "Booting a kernel requires TF-A support, which is enabled by providing -DTF_A_HEADERS={path}.\n"
                      "If you just want memtest and/or the USB loader, don't configure with boot medium or decompression support")
    endif()

    set_property(SOURCE dramstage/commit.c PROPERTY COMPILE_DEFINITIONS ${DRAMSTAGE_COMMIT_DEFS})
    get_property(DEFS SOURCE dramstage/commit.c PROPERTY COMPILE_DEFINITIONS)


    if (DECOMPRESSORS AND NOT BOOT_MEDIA)
        list(APPEND DRAMSTAGE_DECOMPRESS_DEFS CONFIG_DRAMSTAGE_MEMORY=1)
    endif()

    if(DECOMPRESSORS)
        list(APPEND DRAMSTAGE_MAIN_C CONFIG_DRAMSTAGE_DECOMPRESSION)
        list(APPEND DRAMSTAGE_SRC
            compression/lzcommon.c
            lib/string.c
            dramstage/decompression.c)
    endif()
    if ("lz4"  IN_LIST DECOMPRESSORS)
        list(APPEND DRAMSTAGE_DECOMPRESS_DEFS HAVE_LZ4)
        list(APPEND DRAMSTAGE_SRC compression/lz4.c)
    endif()
    if ("gzip" IN_LIST DECOMPRESSORS)
        list(APPEND DRAMSTAGE_DECOMPRESS_DEFS HAVE_GZIP)
        list(APPEND DRAMSTAGE_SRC compression/inflate.c)
    endif()
    if ("zstd" IN_LIST DECOMPRESSORS)
        list(APPEND DRAMSTAGE_DECOMPRESS_DEFS HAVE_ZSTD)
        list(APPEND DRAMSTAGE_SRC
            lib/string.c
            compression/zstd.c
            compression/zstd_fse.c
            compression/zstd_literals.c
            compression/zstd_probe_literals.c
            compression/zstd_sequences.c)
    endif()

    set_property(SOURCE dramstage/decompression.c PROPERTY COMPILE_DEFINITIONS ${DRAMSTAGE_DECOMPRESS_DEFS})
    get_property(DEFS SOURCE dramstage/decompression.c PROPERTY COMPILE_DEFINITIONS)

    set(SRAMSTAGE_MAIN_DEFS)
    set(DRAMSTAGE_MAIN_DEFS)
    if ("spi"  IN_LIST BOOT_MEDIA)
        list(APPEND SRAMSTAGE_MAIN_DEFS CONFIG_SPI=1)
        list(APPEND DRAMSTAGE_MAIN_DEFS CONFIG_SPI=1)
        list(APPEND DRAMSTAGE_SRC lib/rkspi.c rk3399/spi.c)
    endif()
    if ("emmc" IN_LIST BOOT_MEDIA)
        list(APPEND SRAMSTAGE_MAIN_DEFS CONFIG_EMMC=1)
        list(APPEND DRAMSTAGE_MAIN_DEFS CONFIG_EMMC=1)
        list(APPEND SRAMSTAGE_SRC lib/sdhci_common.c rk3399/emmcphy.c sramstage/emmc_init.c)
        list(APPEND DRAMSTAGE_SRC lib/sdhci_common.c rk3399/emmcphy.c dramstage/blk_emmc.c lib/sdhci.c dramstage/boot_blockdev.c)
    endif()
    if ("sd"   IN_LIST BOOT_MEDIA)
        list(APPEND SRAMSTAGE_MAIN_DEFS CONFIG_SD=1)
        list(APPEND DRAMSTAGE_MAIN_DEFS CONFIG_SD=1)
        list(APPEND SRAMSTAGE_SRC lib/dwmmc_common.c lib/sd.c sramstage/sd_init.c lib/dwmmc_early.c)
        list(APPEND DRAMSTAGE_SRC lib/dwmmc_common.c lib/sd.c dramstage/blk_sd.c lib/dwmmc.c lib/dwmmc_xfer.c dramstage/boot_blockdev.c)
    endif()
    if ("nvme" IN_LIST BOOT_MEDIA)
        list(APPEND SRAMSTAGE_MAIN_DEFS CONFIG_PCIE=1)
        list(APPEND DRAMSTAGE_MAIN_DEFS CONFIG_PCIE=1)
        list(APPEND SRAMSTAGE_SRC sramstage/pcie_init.c)
        list(APPEND DRAMSTAGE_SRC dramstage/blk_nvme.c lib/nvme.c lib/nvme_xfer.c dramstage/boot_blockdev.c)
    endif()

    set_property(SOURCE sramstage/main.c PROPERTY COMPILE_DEFINITIONS ${SRAMSTAGE_MAIN_DEFS})
    set_property(SOURCE dramstage/main.c PROPERTY COMPILE_DEFINITIONS ${DRAMSTAGE_MAIN_DEFS})

    get_property(DEFS SOURCE sramstage/main.c PROPERTY COMPILE_DEFINITIONS)
    get_property(DEFS SOURCE dramstage/main.c PROPERTY COMPILE_DEFINITIONS)

    add_custom_command(OUTPUT dram_cfg/pctl.gen.c COMMENT "Generating dram_cfg/pctl.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/pctl-fields.txt --mhz 400 800 50 --hex > dram_cfg/pctl.gen.c)
    add_custom_command(OUTPUT dram_cfg/pi.gen.c COMMENT "Generating dram_cfg/pi.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/pi-fields.txt --mhz 50 800 400 --hex > dram_cfg/pi.gen.c)
    add_custom_command(OUTPUT dram_cfg/dslice.gen.c COMMENT "Generating dram_cfg/dslice.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/dslice-fields.txt --set freq 0 --mhz 50 800 400 --set dslice 0 --hex > dram_cfg/dslice.gen.c)
    add_custom_command(OUTPUT dram_cfg/aslice.gen.c COMMENT "Generating dram_cfg/aslice.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/aslice-fields.txt --set freq 0 --mhz 50 800 400 --set aslice 0 --hex > dram_cfg/aslice0.gen.c)
    add_custom_command(OUTPUT dram_cfg/aslice1.gen.c COMMENT "Generating dram_cfg/aslice1.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/aslice-fields.txt --set freq 0 --mhz 50 800 400 --set aslice 1 --hex > dram_cfg/aslice1.gen.c)
    add_custom_command(OUTPUT dram_cfg/aslice2.gen.c COMMENT "Generating dram_cfg/aslice2.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/aslice-fields.txt --set freq 0 --mhz 50 800 400 --set aslice 2 --hex > dram_cfg/aslice2.gen.c)
    add_custom_command(OUTPUT dram_cfg/adrctl.gen.c COMMENT "Generating dram_cfg/adrctl.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/adrctl-fields.txt --set freq 0 --mhz 50 800 400 --hex > dram_cfg/adrctl.gen.c)
    add_custom_command(OUTPUT dram_cfg/dslice5_7_f2.gen.c COMMENT "Generating dram_cfg/dslice5_7_f2.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/dslice-fields.txt --set freq 2 --mhz 50 800 400 --set dslice 0 --first 5 --last 7 --hex > dram_cfg/dslice5_7_f2.gen.c)
    add_custom_command(OUTPUT dram_cfg/dslice59_90_f2.gen.c COMMENT "Generating dram_cfg/dslice59_90_f2.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/dslice-fields.txt --set freq 2 --mhz 50 800 400 --set dslice 0 --first 59 --last 90 --hex > dram_cfg/dslice59_90_f2.gen.c)
    add_custom_command(OUTPUT dram_cfg/slave_master_delays_f2.gen.c COMMENT "Generating dram_cfg/slave_master_delays_f2.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/aslice-fields.txt --set freq 2 --mhz 50 800 400 --set aslice 0 --first 32 --last 37 --hex > dram_cfg/slave_master_delays_f2.gen.c)
    add_custom_command(OUTPUT dram_cfg/adrctl17_22_f2.gen.c COMMENT "Generating dram_cfg/adrctl17_22_f2.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/adrctl-fields.txt --set freq 2 --mhz 50 800 400 --first 17 --last 22 --hex > dram_cfg/adrctl17_22_f2.gen.c)
    add_custom_command(OUTPUT dram_cfg/adrctl28_44_f2.gen.c COMMENT "Generating dram_cfg/adrctl28_44_f2.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/adrctl-fields.txt --set freq 2 --mhz 50 800 400 --first 28 --last 44 --hex > dram_cfg/adrctl28_44_f2.gen.c)
    add_custom_command(OUTPUT dram_cfg/dslice5_7_f1.gen.c COMMENT "Generating dram_cfg/dslice5_7_f1.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/dslice-fields.txt --set freq 1 --mhz 50 800 400 --set dslice 0 --first 5 --last 7 --hex > dram_cfg/dslice5_7_f1.gen.c)
    add_custom_command(OUTPUT dram_cfg/dslice59_90_f1.gen.c COMMENT "Generating dram_cfg/dslice59_90_f1.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/dslice-fields.txt --set freq 1 --mhz 50 800 400 --set dslice 0 --first 59 --last 90 --hex > dram_cfg/dslice59_90_f1.gen.c)
    add_custom_command(OUTPUT dram_cfg/slave_master_delays_f1.gen.c COMMENT "Generating dram_cfg/slave_master_delays_f1.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/aslice-fields.txt --set freq 1 --mhz 50 800 400 --set aslice 0 --first 32 --last 37 --hex > dram_cfg/slave_master_delays_f1.gen.c)
    add_custom_command(OUTPUT dram_cfg/adrctl17_22_f1.gen.c COMMENT "Generating dram_cfg/adrctl17_22_f1.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/adrctl-fields.txt --set freq 1 --mhz 50 800 400 --first 17 --last 22 --hex > dram_cfg/adrctl17_22_f1.gen.c)
    add_custom_command(OUTPUT dram_cfg/adrctl28_44_f1.gen.c COMMENT "Generating dram_cfg/adrctl28_44_f1.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/adrctl-fields.txt --set freq 1 --mhz 50 800 400 --first 28 --last 44 --hex > dram_cfg/adrctl28_44_f1.gen.c)
    
    add_custom_target(gen_dram_cfg
      DEPENDS 
      dram_cfg/pctl.gen.c
      dram_cfg/pi.gen.c
      dram_cfg/dslice.gen.c
      dram_cfg/aslice.gen.c
      dram_cfg/aslice1.gen.c
      dram_cfg/aslice2.gen.c
      dram_cfg/adrctl.gen.c
      dram_cfg/dslice5_7_f2.gen.c
      dram_cfg/dslice59_90_f2.gen.c
      dram_cfg/slave_master_delays_f2.gen.c
      dram_cfg/adrctl17_22_f2.gen.c
      dram_cfg/adrctl28_44_f2.gen.c
      dram_cfg/dslice5_7_f1.gen.c
      dram_cfg/dslice59_90_f1.gen.c
      dram_cfg/slave_master_delays_f1.gen.c
      dram_cfg/adrctl17_22_f1.gen.c
      dram_cfg/adrctl28_44_f1.gen.c
    )
    add_library(dram_cfg STATIC dram/dramcfg.c ${GEN_DRAM_CFG})
    add_dependencies(dram_cfg gen_dram_cfg)
    target_include_directories(dram_cfg PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/dram_cfg)

    set(MODULES
        ${LIB_SRC}
        ${SRAMSTAGE_SRC}
        ${DRAMSTAGE_SRC}
        ${USB_LOADER_SRC}
        ${MEMTEST_SRC}
        rk3399/teststage.c
        lib/dump_fdt.c)
    if (BOOT_MEDIA)
        list(APPEND MODULES ${DRAMSTAGE_EMBEDDER_SRC})
    endif()

    if (BUILD_FULL_DEBUG)
        foreach(file IN LISTS MODULES)
            get_property(DEFS SOURCE ${file} PROPERTY COMPILE_DEFINITIONS)
            set_property(SOURCE ${file} PROPERTY COMPILE_DEFINITIONS ${DEFS} DEBUG_MSG)
            get_property(DEFS SOURCE ${file} PROPERTY COMPILE_DEFINITIONS)
            message(STATUS "${file} - ${DEFS}")
        endforeach(file)
    endif()

    add_library(modules STATIC ${MODULES})

    set(ENTRY_COMMON
        # must be shorter than 64 bytes
        CONFIG_GREETING=\"\\"levinboot/${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}\\r\\n\\"\"
        # base clock 1.5MHz
        CONFIG_UART_CLOCK_DIV=1
    )

    add_library(lib STATIC
        ${LIB_SRC}
        aarch64/dcache.S
        aarch64/mmu_asm.S
        aarch64/context.S
        aarch64/gicv3.S
        aarch64/save_restore.S
        aarch64/string.S
        rk3399/entry.S
        rk3399/handlers.c
        rk3399/debug.S
    )
    target_compile_definitions(lib PRIVATE CONFIG_FIRST_STAGE=0 PRIVATE CONFIG_EL=3)

    macro(post_build app)
        add_custom_command(TARGET ${app} POST_BUILD
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating Post Build Artifacts for ${app}"
            # generate assembly listing
            COMMAND ${OBJDUMP} -h -S ${app}${CMAKE_EXECUTABLE_SUFFIX} > ${app}.lss
            # parsing symbols with nm to generate: ${app}_nm.txt
            COMMAND ${NM} --numeric-sort ${PARSE_SYMBOL_OPTIONS} ${app}${CMAKE_EXECUTABLE_SUFFIX} > ${app}_nm.txt
            # creating size summary table with size to generate: ${APP}_size.txt
            COMMAND ${SIZE} ${SIZE_OPTIONS} ${app}${CMAKE_EXECUTABLE_SUFFIX} > ${app}_size.txt
            # creating hex module: ${app}.bin
            COMMAND ${POSTBUILD_GEN_BIN}
        )
        install(FILES
            ${CMAKE_BINARY_DIR}/${app}${CMAKE_EXECUTABLE_SUFFIX}
            ${CMAKE_BINARY_DIR}/${app}.lss
            ${CMAKE_BINARY_DIR}/${app}_nm.txt
            ${CMAKE_BINARY_DIR}/${app}_size.txt
            ${CMAKE_BINARY_DIR}/${app}.bin
            DESTINATION
            ${CMAKE_CURRENT_SOURCE_DIR}/artifacts/${app}-${NAME}-${CMAKE_BUILD_TYPE}
        )
    endmacro()

    # ff8c2000
    add_executable(sramstage-usb ${SRAMSTAGE_SRC} ${USB_LOADER_SRC}) 
    target_compile_definitions(sramstage-usb PRIVATE ${ENTRY_COMMON} CONFIG_FIRST_STAGE=1)
#    post_build(sramstage-usb)

    # ff8c2000
    add_executable(memtest ${SRAMSTAGE_SRC} ${MEMTEST_SRC})
    target_compile_definitions(memtest PRIVATE ${ENTRY_COMMON} CONFIG_FIRST_STAGE=1)

    # 00280000
    add_executable(teststage 
        rk3399/teststage.c
        rk3399/entry.S
        aarch64/dcache.S
        aarch64/context.S
        rk3399/handlers.c
        rk3399/debug.S
        aarch64/mmu_asm.S
        lib/uart.c
        lib/uart16550a.c
        lib/error.c
        lib/mmu.c
        lib/dump_fdt.c
        lib/sched.c
        lib/string.c
    )
    target_compile_definitions(teststage PRIVATE CONFIG_FIRST_STAGE=0 CONFIG_EL=2)

    if (TF_A_HEADERS)
        # 04000000
        add_executable(dramstage ${DRAMSTAGE_SRC} ${LIB_SRC}
            aarch64/dcache.S
            aarch64/mmu_asm.S
            aarch64/context.S
            aarch64/gicv3.S
            aarch64/save_restore.S
            aarch64/string.S
            rk3399/entry.S
            rk3399/handlers.c
            rk3399/debug.S)
        # build.default('dramstage.bin')
        target_compile_definitions(dramstage PRIVATE CONFIG_FIRST_STAGE=0 PRIVATE CONFIG_EL=3)
    endif()

    if (BOOT_MEDIA)
        # ff8c2000
        add_executable(levinboot-usb ${SRAMSTAGE_SRC} ${DRAMSTAGE_EMBEDDER_SRC})
        target_compile_definitions(levinboot-usb PRIVATE ${ENTRY_COMMON} CONFIG_FIRST_STAGE=1)
    endif()

endif()
