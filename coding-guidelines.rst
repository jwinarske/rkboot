levinboot Coding Guidelines
===========================

Note that, as of now, many of these guidelines are widely violated within the code base.
This document is at this point only definition of goals, so new code should conform to these guidelines, but old code may not.

Code organization and global names
----------------------------------

levinboot code is divided into three rough layers: generic, architecture and platform.
The term `facilities` is used as a general term for notions such as types, constants, functions or more general linker symbols.
Facilities may be namespaced, typically by prefixing their name.
Header files declaring these facilities should also have this prefix in the include path.
Some of these namespaces are described in the layer definitions below.

Generic code
    Code that is (at least in theory) portable between CPU architectures and may possibly even work in a hosted environment. Types, constants and functions that need to have architecture- or platform-specific implementations but should be widely available may be referenced from architecture or platform code in the namespaces `arch` and `plat` respectively.

Architecture code
    Code that is portable between platforms but specific to a CPU architecture (in a broad sense), including non-platform-specific assembly code (e. g. register save/restore helpers), MMU code and interrupt handling code (e. g. IRQ masking, interrupt routing).
    This includes code for specific CPU cores or interrupt controllers used in multiple platforms.
    May implement facilities in the `arch` namespace used by generic code.
    Code implementing facilities that are not available, or substantially differ, on other architectures should use either the architecture's own namespace or a more specific one (e. g. for the specific interrupt controller or CPU core).
    Global assembly symbols that do not conform to a normal calling convention should be namespaced under the prefix `asm`.
    This code may, in addition to all namespaces it implements, reference generic code directly.
    Platform facilities can be accessed through the `plat` namespace.

Platform code
    Chip- or board-specific code.
    May implement facilities in the `plat` namespace.
    It does not need to conform to strict namespacing rules (as long as it does not infringe on other namespaces used on this platform), though for better organization namespacing is still useful, e. g. if the code is in different directories.

`lib/`
    generic code

`drivers/`
    drivers for hardware available on multiple platforms and which fulfill the standards of generic code.

`aarch64/`
    architecture code for aarch64 and, for now, other ARM facilities.

    Namespaces:

    `aarch64`
        namespace for everything that does not fit elsewhere
    `gicv[23]`
        interrupt controllers
    `mmu`
        page tables and other MMU handling
