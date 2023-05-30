
if (decompressors)
    string (REPLACE "," ";" decompressors "${decompressors}")
    message(STATUS "decompressors: ${decompressors}")
    include_directories(compression)
endif ()
if (NOT boot_media OR dramstage_initcpio AND NOT decompressors)
    MESSAGE(WARN "boot medium and initcpio support require decompression support, enabling zstd")
    list(APPEND decompressors zstd)
endif ()

if (boards)
    string (REPLACE "," ";" boards "${boards}")
    message(STATUS "boards: ${boards}")
else ()
    message(STATUS "no boards selected, assuming 'rp64,pbp'.")
    set(boards "rp64;pbp")
endif ()

if (boot_media)
    string (REPLACE "," ";" boot_media "${boot_media}")
    message(STATUS "boot_media: ${boot_media}")
else ()
    message(STATUS "no boot_media selected, assuming none.")
    set(boot_media "")
endif ()

if (decompressors AND NOT boot_media)
    set_property(SOURCE dramstage/decompression.c PROPERTY COMPILE_DEFINITIONS CONFIG_DRAMSTAGE_MEMORY=1)
endif ()

if (NOT CONFIG_CONSOLE_FIFO_DEPTH)
    set(CONFIG_CONSOLE_FIFO_DEPTH 64)
endif ()
message(STATUS "CONFIG_CONSOLE_FIFO_DEPTH=${CONFIG_CONSOLE_FIFO_DEPTH}")

if (NOT CONFIG_BUF_SIZE)
    set(CONFIG_BUF_SIZE 128)
endif ()
message(STATUS "CONFIG_BUF_SIZE=${CONFIG_BUF_SIZE}")

if (NOT CONFIG_GREETING)
    set(CONFIG_GREETING \"\\"levinboot/0.8\\r\\n\\"\")
endif ()
message(STATUS "CONFIG_GREETING=${CONFIG_GREETING}")

if (NOT CONFIG_UART_CLOCK_DIV)
    set(CONFIG_UART_CLOCK_DIV 1)
endif ()
message(STATUS "CONFIG_UART_CLOCK_DIV=${CONFIG_UART_CLOCK_DIV}")

if (with-tf-a-headers)
    set(tf-a-headers ${with-tf-a-headers})
elseif(tf-a-headers)
elseif (boot_media OR decompressors)
    message(FATAL_ERROR "booting a kernel requires TF-A support, which is enabled by providing -Dwith-tf-a-headers.\n"
        "If you just want memtest and/or the USB loader, don't configure with boot medium or decompression support")
endif ()

#
# Developer Debug options
#
if (debug OR full_debug OR spew)
   add_compile_definitions(DEBUG_MSG)
endif ()

if (spew)
   add_compile_definitions(SPEW_MSG)
endif ()
