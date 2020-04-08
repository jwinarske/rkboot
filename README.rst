========================
The levinboot bootloader
========================
.. role:: src(code)
.. role:: output(code)
.. role:: command(code)
   :language: shell

levinboot (name always lowercase) is a bootloader for (currently) RK3399 platforms with LPDDR4 memory.

The project is currently in an α-state, as indicated by its version number (0.1).

Important project goals include:

- *Fast iteration times*: developers should not be made to wait to see the result of their work. The project can be configured, built and run within 10 seconds with the right setup, with incremental builds usually being faster than reaching over to reset the board.

- *tooling support*: the project uses the Ninja build manager, which implements precise dependency tracking using depfiles without ugly hacks, but also creates compilation databases for free, which can be consumed by tools like clangd.

- *KISS*: complex abstractions should be avoided unless they significantly improve maintainability.

What works and what doesn't?
============================

What should work at this point:

- Boot from USB, SD or eMMC with a memtest payload. *Caveat*: the memtest tries to test the entire DRAM address space, so will probably fail with <4GB boards.

- Boot to Linux via TF-A over USB. This is described in _`Booting via USB`.

What is intended to work at some point, but not implemented yet:

- Boot from SPI. This requires adding zero-padding support to :command:`idbtool`, as well as probably adding payload loading support for SPI.

- Payload loading. Currently, levinboot cannot read any boot media to find further boot stages, meaning that currently the only way to get further stages is by spoon-feeding them through :command:`usbtool`.

License
=======
levinboot is licensed under CC0, meaning that the author and contributors give up their copyright by placing the work in the public domain, in jurisdictions where that is possible, or if not, provide a non-exclusive license to do just about anything to the work to anyone who gets a copy, and waives all other legal interests of the author/contributor in the work.

See `<https://creativecommons.org/publicdomain/zero/1.0/legalcode>`__ for the legal text.

Source structure
================
TBD

Build process
=============

levinboot uses a Ninja-based build system. The build is configured by running :src:`configure.py`. This can (but doesn't have to be) be done in a different directory, keeping the build files separate from the sources.

Primary build targets are:

- :output:`levinboot-usb.bin`: this is used for _`Booting via USB`.

- :output:`levinboot-sd.img`: this is an image that can be written to sector 64 on an SD/eMMC drive. (FIXME: currently has no payload and is untested)

- :output:`memtest.bin`: this is a very simple payload and just writes pseudorandom numbers to DRAM in 128MiB blocks and reads them back to check if the values are retained.

- :output:`elfloader.bin`: this is the payload loading stage for _`Booting via USB`.
  It expects a BL31 ELF image, a FDT blob and a Linux kernel (or a similar EL2 payload like `teststage.bin`) at (currently) hardcoded DRAM addresses, loads BL31 into its correct addresses (which includes SRAM), inserts address information into the FDT, and finally jumps to BL31 with parameters telling it to start Linux (or other EL2 payload)

- :output:`teststage.bin`: this is a simple EL2 payload. Currently it only dumps the passed FDT blob, if it is detected at :code:`*X0`.

Booting via USB
===============

The only way to currently boot a system using levinboot is to use USB spoon feeding via RK3399 mask ROM mode.

To do this:

- build the tools, specifically :command:`usbtool`. The tools are contained in the :src:`tools/` directory and have their own :src:`tools/configure`.

- build levinboot as well as any payloads you might want to run.

- remove or disable any other boot sources containing a valid ID block. These can be:

  - a SPI flash chip. On the RockPro64, this can be disabled by shorting pins 23 and 25 on the PI-2 header.
    Note that neither RockPro64 nor Pinebook Pro currently ship with an ID block on the SPI chip, so this is not necessary by default.
  
  - an eMMC chip. On the RockPro64 and Pinebook Pro, these come as removable modules.
    Removal isn't necessary though (and should be avoided because of wear on the connector) because they can be disabled by a switch right next to the module (on the Pinebook Pro) or by shorting the 2-pin header right next to the eMMC module and SPI chip (on the RockPro64).

  - an SD card.

- connect a USB OTG port (for the Pinebook Pro and RockPro64, this is the USB-C port) of your RK3399 device with a USB host port of your development host.

  You should also connect a serial console to UART2, so you can observe the boot process.
  This is pins 6, 8, 10 on the RockPro64 (ground, TX and RX respectively) and the headphone jack on the Pinebook Pro (keep in mind this has to be activated using a switch on the board).
  Both of these use 3.3V, with levinboot setting 1.5MBaud (8 bits, no parity, no flow control) transfer rate by default (this can be changed in :src:`config.h` setting a different clock divider, i. e. 13 for 115200 baud).
  Keep in mind that BL31 by default uses 115200 baud by default, so unless you change that (in :code:`plat/rockchip/rk3399/rk3399_def.h` in the TF-A source tree or in levinboot as described before), you will not get any output from that stage.

  - (re-)start the system. Both the RockPro64 and the Pinebook Pro have a reset button on the PCB, making this a quick and simple process.

  - tell :command:`usbtool` to run levinboot and its payload. There are multiple currently working constellations for this, and if you are just getting started, you should try them in order (while resetting the system inbetween, of course).

    - levinboot and `memtest.bin`: run :command:`usbtool --call levinboot-usb.bin --run memtest.bin`.
      This should run levinboot and then start testing memory.

    - levinboot and BL31 with `teststage.bin`: run :command:`usbtool --call levinboot-usb.bin --load 100000 elfloader.bin --load 200000 path/to/bl31.elf --load 500000 path/to/fdt-blob.dtb --load 680000 teststage.bin --jump 100000 2000` (with the paths substituted for your system).

      This should run levinboot to initialize DRAM, load all payload files into DRAM, and finally jump to :output:`elfloader.bin` which will start BL31, which will give control to :output:`teststage.bin`, which should dump the FDT header as well as its contents in DTS syntax.

    - levinboot and BL31 with a Linux kernel: this is basically the same as the previous command, just with your (uncompressed) kernel image instead of :output:`teststage.bin`.

      Beware that the loading step will take a while, because :command:`usbtool` currently uses the mask ROM code to transfer the files, which is anything but fast at receiving (or most likely, verifying) data sent over USB.
      levinboot also does not clock up the boot CPU, so an additional delay of a few seconds between end of the transfer and getting kernel output over serial
