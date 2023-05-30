cmake_minimum_required(VERSION 3.10.2)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are: Debug, Release, or MinSizeRel." FORCE)
    message(STATUS "CMAKE_BUILD_TYPE not set, defaulting to Release.")
endif ()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

project(levinboot
        VERSION "0.8.7"
        DESCRIPTION "levinboot bootloader"
        LANGUAGES C ASM
        )

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS TRUE)
set(CMAKE_C_STANDARD_REQUIRED TRUE)

if(NOT CMAKE_CROSSCOMPILING)
    add_subdirectory(tools)
else()
    find_program(IDBTOOL idbtool HINTS artifacts/bin artifacts REQUIRED)
    find_program(REGTOOL regtool HINTS artifacts/bin artifacts REQUIRED)
    find_program(UNPACKTOOL unpacktool HINTS artifacts/bin artifacts REQUIRED)
    find_program(USBTOOL usbtool HINTS artifacts/bin artifacts REQUIRED)
    find_program(LZ4 lz4 REQUIRED)

    include(options)

    include_directories(
      include
      include/std
      aarch64/include
      rk3399/include
      .
    )

    add_library(lib STATIC
      lib/error.c
      lib/uart.c
      lib/uart16550a.c
      lib/mmu.c
      lib/gicv2.c
      lib/sched.c
    )
    set_property(SOURCE lib/uart16550a.c PROPERTY COMPILE_DEFINITIONS CONFIG_CONSOLE_FIFO_DEPTH=${CONFIG_CONSOLE_FIFO_DEPTH})
    set_property(SOURCE lib/uart.c PROPERTY COMPILE_DEFINITIONS CONFIG_BUF_SIZE=${CONFIG_BUF_SIZE})

    add_library(sramstage STATIC
      sramstage/main.c
      rk3399/pll.c
      sramstage/pmu_cru.c
      sramstage/misc_init.c
      dram/training.c
      dram/memorymap.c
      dram/mirror.c
      dram/ddrinit.c
    )
    add_library(dramstage STATIC
      dramstage/main.c
      dramstage/transform_fdt.c
      lib/rki2c.c
      dramstage/commit.c
      dramstage/entropy.c
      dramstage/board_probe.c 
      dram/read_size.c
    )
    set_property(SOURCE dramstage/commit.c
        PROPERTY COMPILE_DEFINITIONS
        TF_A_BL_COMMON_PATH="${tf-a-headers}/common/bl_common_exp.h"
        TF_A_RK_PARAMS_PATH="${tf-a-headers}/plat/rockchip/common/plat_params_exp.h")

    add_library(dramstage_embedder STATIC
      sramstage/embedded_dramstage.c
      compression/lzcommon.c
      compression/lz4.c
      lib/string.c
    )
    add_library(usb_loader STATIC
      sramstage/usb_loader.c
      lib/dwc3.c
      sramstage/usb_loader-spi.c
      lib/rkspi.c
    )    

    if (decompressors)
        set_property(SOURCE dramstage/main.c PROPERTY COMPILE_DEFINITIONS CONFIG_DRAMSTAGE_DECOMPRESSION)
        target_sources(dramstage PRIVATE compression/lzcommon.c lib/string.c dramstage/decompression.c)
        if ("lz4" IN_LIST decompressors)
            set_property(SOURCE dramstage/decompression.c PROPERTY COMPILE_DEFINITIONS HAVE_LZ4)
            target_sources(dramstage PRIVATE compression/lz4.c)
        endif ()
        if ("gzip" IN_LIST decompressors)
            set_property(SOURCE dramstage/decompression.c PROPERTY COMPILE_DEFINITIONS HAVE_GZIP)
            target_sources(dramstage PRIVATE compression/inflate.c)
        endif ()
        if ("zstd" IN_LIST decompressors)
            set_property(SOURCE dramstage/decompression.c PROPERTY COMPILE_DEFINITIONS HAVE_ZSTD)
            target_sources(dramstage PRIVATE compression/zstd.c compression/zstd_fse.c compression/zstd_literals.c compression/zstd_probe_literals.c compression/zstd_sequences.c)
        endif ()
    endif ()

    set(boot_media_handlers sramstage/main.c dramstage/main.c)
    if ("spi" IN_LIST boot_media)
        foreach(f ${boot_media_handlers})
            set_property(SOURCE f PROPERTY COMPILE_DEFINITIONS CONFIG_SPI=1)
        endforeach ()
        target_sources(dramstage PRIVATE lib/rkspi.c rk3399/spi.c)
    endif ()
    if ("emmc" IN_LIST boot_media)
        foreach(f ${boot_media_handlers})
            set_property(SOURCE f PROPERTY COMPILE_DEFINITIONS CONFIG_EMMC=1)
        endforeach ()
        set(emmc_modules lib/sdhci_common.c rk3399/emmcphy.c)
        target_sources(sramstage PRIVATE ${emmc_modules} sramstage/emmc_init.c)
        target_sources(dramstage PRIVATE ${emmc_modules} dramstage/blk_emmc.c lib/sdhci.c dramstage/boot_blockdev.c)
    endif ()
    if ("sd" IN_LIST boot_media)
        foreach(f ${boot_media_handlers})
            set_property(SOURCE f PROPERTY COMPILE_DEFINITIONS CONFIG_SD=1)
        endforeach ()
        set(sdmmc_modules lib/dwmmc_common.c lib/sd.c)
        target_sources(sramstage PRIVATE ${sdmmc_modules} sramstage/sd_init.c lib/dwmmc_early.c)
        target_sources(dramstage PRIVATE ${sdmmc_modules} dramstage/blk_sd.c lib/dwmmc.c lib/dwmmc_xfer.c dramstage/boot_blockdev.c)
    endif ()
    if ("nvme" IN_LIST boot_media)
        set_property(SOURCE sramstage/main.c PROPERTY COMPILE_DEFINITIONS CONFIG_PCIE=1)
        set_property(SOURCE dramstage/main.c PROPERTY COMPILE_DEFINITIONS CONFIG_NVME=1)
        target_sources(sramstage PRIVATE sramstage/pcie_init.c)
        target_sources(dramstage PRIVATE dramstage/blk_nvme.c lib/nvme.c lib/nvme_xfer.c dramstage/boot_blockdev.c)
    endif ()

    if ("rp64" IN_LIST boards)
        list(APPEND CONFIG_BOARD_DEFS CONFIG_BOARD_RP64=1)
    else()
        list(APPEND CONFIG_BOARD_DEFS CONFIG_BOARD_RP64=0)
    endif()

    if ("pbp" IN_LIST boards)
        list(APPEND CONFIG_BOARD_DEFS CONFIG_BOARD_PBP=1)
    else()
        list(APPEND CONFIG_BOARD_DEFS CONFIG_BOARD_PBP=0)
    endif()
    
    if (CONFIG_BOARD_RP64 AND CONFIG_BOARD_PBP)
        list(APPEND CONFIG_BOARD_DEFS CONFIG_SINGLE_BOARD=1)
    else()
        list(APPEND CONFIG_BOARD_DEFS CONFIG_SINGLE_BOARD=0)
    endif()
    set_property(SOURCE dramstage/board_probe.c PROPERTY COMPILE_DEFINITIONS ${CONFIG_BOARD_DEFS})


    set(modules lib sramstage dramstage usb_loader memtest rk3399/teststage.c lib/dump_fdt.c)
    if (boot_media)
        list(APPEND modules dramstage_embedder)
    endif()

    add_library(entry-first STATIC rk3399/entry.S)
    target_compile_definitions(entry-first PRIVATE CONFIG_FIRST_STAGE=1 CONFIG_GREETING=${CONFIG_GREETING} CONFIG_UART_CLOCK_DIV=${CONFIG_UART_CLOCK_DIV})

    add_library(entry STATIC rk3399/entry.S)
    target_compile_definitions(entry PRIVATE CONFIG_FIRST_STAGE=0 CONFIG_EL=3)

    add_library(entry-el2 STATIC rk3399/entry.S)
    target_compile_definitions(entry-el2 PRIVATE CONFIG_FIRST_STAGE=0 CONFIG_EL=2)

    add_library(dcache-el2 STATIC aarch64/dcache.S)
    target_compile_definitions(dcache-el2 PRIVATE CONFIG_EL=2)
    add_library(dcache-el3 STATIC aarch64/dcache.S)
    target_compile_definitions(dcache-el3 PRIVATE CONFIG_EL=3)

    add_library(context-el2 STATIC aarch64/context.S)
    target_compile_definitions(context-el2 PRIVATE CONFIG_EL=2)
    add_library(context-el3 STATIC aarch64/context.S)
    target_compile_definitions(context-el3 PRIVATE CONFIG_EL=3)

    add_library(debug-el2 STATIC rk3399/debug.S)
    target_compile_definitions(debug-el2 PRIVATE CONFIG_EL=2)
    add_library(debug-el3 STATIC rk3399/debug.S)
    target_compile_definitions(debug-el3 PRIVATE CONFIG_EL=3)

    add_library(handlers-el2 STATIC rk3399/handlers.c)
    target_compile_definitions(handlers-el2 PRIVATE CONFIG_EL=2)
    add_library(handlers-el3 STATIC rk3399/handlers.c)
    target_compile_definitions(handlers-el3 PRIVATE CONFIG_EL=3)

    target_sources(lib PRIVATE aarch64/mmu_asm.S aarch64/gicv3.S aarch64/save_restore.S aarch64/string.S)
    set_property(SOURCE aarch64/mmu_asm.S PROPERTY COMPILE_DEFINITIONS ASSERTIONS=1 DEV_ASSERTIONS=0)
    add_dependencies(lib entry handlers-el3 debug-el3 dcache-el3 context-el3)

    set(DRAM_CONFIG_DIR "dram_cfg")
    if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/${DRAM_CONFIG_DIR})
        message(STATUS "Creating output directory '${CMAKE_CURRENT_BINARY_DIR}/${DRAM_CONFIG_DIR}'")
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${DRAM_CONFIG_DIR}")
    endif()

    add_custom_command(OUTPUT dram_cfg/pctl.gen.c COMMENT "Generating dram_cfg/pctl.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/pctl-fields.txt --mhz 400 800 50 --hex >${DRAM_CONFIG_DIR}/pctl.gen.c)
    add_custom_command(OUTPUT dram_cfg/pi.gen.c COMMENT "Generating dram_cfg/pi.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/pi-fields.txt --mhz 50 800 400 --hex >${DRAM_CONFIG_DIR}/pi.gen.c)
    add_custom_command(OUTPUT dram_cfg/dslice.gen.c COMMENT "Generating dram_cfg/dslice.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/dslice-fields.txt --set freq 0 --mhz 50 800 400 --set dslice 0 --hex >${DRAM_CONFIG_DIR}/dslice.gen.c)
    add_custom_command(OUTPUT dram_cfg/aslice.gen.c COMMENT "Generating dram_cfg/aslice.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/aslice-fields.txt --set freq 0 --mhz 50 800 400 --set aslice 0 --hex >${DRAM_CONFIG_DIR}/aslice0.gen.c)
    add_custom_command(OUTPUT dram_cfg/aslice1.gen.c COMMENT "Generating dram_cfg/aslice1.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/aslice-fields.txt --set freq 0 --mhz 50 800 400 --set aslice 1 --hex >${DRAM_CONFIG_DIR}/aslice1.gen.c)
    add_custom_command(OUTPUT dram_cfg/aslice2.gen.c COMMENT "Generating dram_cfg/aslice2.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/aslice-fields.txt --set freq 0 --mhz 50 800 400 --set aslice 2 --hex >${DRAM_CONFIG_DIR}/aslice2.gen.c)
    add_custom_command(OUTPUT dram_cfg/adrctl.gen.c COMMENT "Generating dram_cfg/adrctl.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/adrctl-fields.txt --set freq 0 --mhz 50 800 400 --hex >${DRAM_CONFIG_DIR}/adrctl.gen.c)
    add_custom_command(OUTPUT dram_cfg/dslice5_7_f2.gen.c COMMENT "Generating dram_cfg/dslice5_7_f2.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/dslice-fields.txt --set freq 2 --mhz 50 800 400 --set dslice 0 --first 5 --last 7 --hex >${DRAM_CONFIG_DIR}/dslice5_7_f2.gen.c)
    add_custom_command(OUTPUT dram_cfg/dslice59_90_f2.gen.c COMMENT "Generating dram_cfg/dslice59_90_f2.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/dslice-fields.txt --set freq 2 --mhz 50 800 400 --set dslice 0 --first 59 --last 90 --hex >${DRAM_CONFIG_DIR}/dslice59_90_f2.gen.c)
    add_custom_command(OUTPUT dram_cfg/slave_master_delays_f2.gen.c COMMENT "Generating dram_cfg/slave_master_delays_f2.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/aslice-fields.txt --set freq 2 --mhz 50 800 400 --set aslice 0 --first 32 --last 37 --hex >${DRAM_CONFIG_DIR}/slave_master_delays_f2.gen.c)
    add_custom_command(OUTPUT dram_cfg/adrctl17_22_f2.gen.c COMMENT "Generating dram_cfg/adrctl17_22_f2.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/adrctl-fields.txt --set freq 2 --mhz 50 800 400 --first 17 --last 22 --hex >${DRAM_CONFIG_DIR}/adrctl17_22_f2.gen.c)
    add_custom_command(OUTPUT dram_cfg/adrctl28_44_f2.gen.c COMMENT "Generating dram_cfg/adrctl28_44_f2.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/adrctl-fields.txt --set freq 2 --mhz 50 800 400 --first 28 --last 44 --hex >${DRAM_CONFIG_DIR}/adrctl28_44_f2.gen.c)
    add_custom_command(OUTPUT dram_cfg/dslice5_7_f1.gen.c COMMENT "Generating dram_cfg/dslice5_7_f1.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/dslice-fields.txt --set freq 1 --mhz 50 800 400 --set dslice 0 --first 5 --last 7 --hex >${DRAM_CONFIG_DIR}/dslice5_7_f1.gen.c)
    add_custom_command(OUTPUT dram_cfg/dslice59_90_f1.gen.c COMMENT "Generating dram_cfg/dslice59_90_f1.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/dslice-fields.txt --set freq 1 --mhz 50 800 400 --set dslice 0 --first 59 --last 90 --hex >${DRAM_CONFIG_DIR}/dslice59_90_f1.gen.c)
    add_custom_command(OUTPUT dram_cfg/slave_master_delays_f1.gen.c COMMENT "Generating dram_cfg/slave_master_delays_f1.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/aslice-fields.txt --set freq 1 --mhz 50 800 400 --set aslice 0 --first 32 --last 37 --hex >${DRAM_CONFIG_DIR}/slave_master_delays_f1.gen.c)
    add_custom_command(OUTPUT dram_cfg/adrctl17_22_f1.gen.c COMMENT "Generating dram_cfg/adrctl17_22_f1.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/adrctl-fields.txt --set freq 1 --mhz 50 800 400 --first 17 --last 22 --hex >${DRAM_CONFIG_DIR}/adrctl17_22_f1.gen.c)
    add_custom_command(OUTPUT dram_cfg/adrctl28_44_f1.gen.c COMMENT "Generating dram_cfg/adrctl28_44_f1.gen.c"
        COMMAND ${REGTOOL} --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/phy-macros.txt --read ${CMAKE_CURRENT_SOURCE_DIR}/dram/adrctl-fields.txt --set freq 1 --mhz 50 800 400 --first 28 --last 44 --hex >${DRAM_CONFIG_DIR}/adrctl28_44_f1.gen.c)
    
    set(GEN_DRAM_CFG
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
    add_custom_target(gen_dram_cfg DEPENDS ${GEN_DRAM_CFG})
    add_library(dram_cfg STATIC dram/dramcfg.c)
    add_dependencies(dram_cfg gen_dram_cfg)
    target_include_directories(dram_cfg PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/dram_cfg)

    target_link_libraries(sramstage
       entry-first
       handlers-el3
       debug-el3
       dcache-el3
       context-el3
       dram_cfg
       lib)

    # build.rule('lz4', 'lz4 -c $flags $in >$out')
    # build.rule('incbin', f'{objcopy} -I binary -O elf64-littleaarch64 -B aarch64 $incbin_flags $flags $in $out')

    # build('dramstage.lz4', 'lz4', 'dramstage.bin', flags='--content-size')
    # build('dramstage.lz4.o', 'incbin', 'dramstage.lz4')
    # dramstage_embedder |= {'dramstage.lz4', 'entry-first'}

    add_custom_command(OUTPUT dramstage.lz4
       COMMENT "Creating lz4 image of dramstage"
       COMMAND ${LZ4} -v -c --content-size libdramstage.a >dramstage.lz4)

    add_custom_command(OUTPUT dramstage.lz4.o
       COMMENT "Creating dramstage.lz4.o"
       DEPENDS dramstage.lz4
       COMMAND ${OBJCOPY} -I binary -O elf64-littleaarch64 -B aarch64 dramstage.lz4 dramstage.lz4.o)

    add_custom_target(gen_dramstage_lz4_obj DEPENDS dramstage.lz4 dramstage.lz4.o)
    add_dependencies(gen_dramstage_lz4_obj dramstage)

    target_link_libraries(dramstage_embedder PRIVATE entry-first dramstage.lz4.o)
    add_dependencies(dramstage_embedder entry-first gen_dramstage_lz4_obj)

    #
    # Executables
    #
    file(WRITE null.c "")
    add_executable(sramstage-usb null.c)
    target_link_libraries(sramstage-usb PRIVATE sramstage usb_loader)
#    binary('sramstage-usb', sramstage | usb_loader, 'ff8c2000')
    
    add_executable(memtest sramstage/memtest.c dram/read_size.c rk3399/cpu_onoff.S)
    target_link_libraries(memtest PRIVATE sramstage)
#    binary('memtest', sramstage | memtest, 'ff8c2000')

    add_executable(teststage 
        rk3399/teststage.c
        aarch64/mmu_asm.s
        lib/uart.c
        lib/uart16550a.c
        lib/error.c
        lib/mmu.c
        lib/dump_fdt.c
        lib/sched.c
        lib/string.c)
    target_link_libraries(teststage PRIVATE
        entry-el2
        dcache-el2
        context-el2
        handlers-el2
        debug-el2)

    if (tf_a_headers)
    endif ()
#    build('memtest-sd.img', 'run', 'memtest.bin', deps='idbtool', bin='./idbtool')
#    build.default('sramstage-usb.bin', 'memtest.bin', 'teststage.bin', 'memtest-sd.img')
#    if args.tf_a_headers:
#        binary('dramstage', dramstage | lib, '04000000')
#        build.default('dramstage.bin')
#    if boot_media:
#        binary('levinboot-usb', sramstage | dramstage_embedder, 'ff8c2000')

endif()
