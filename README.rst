========================
The levinboot bootloader
========================
.. role:: src(code)
.. role:: output(code)
.. role:: command(code)
   :language: shell
.. role:: cmdargs(code)

levinboot (name always lowercase) is a bootloader for (currently) RK3399 platforms with LPDDR4 memory.

The project is currently in a β-state. Basic features work, but formats and features are still in flux. As a non-commercial project, no warranty is given for any breakage.

Important project goals include:

- *Fast iteration times*: developers should not be made to wait to see the result of their work. The project can be configured, built and run within 10 seconds with the right setup, with incremental builds usually being faster than reaching over to reset the board.

- *tooling support*: the project uses the Ninja build manager, which implements precise dependency tracking using depfiles without ugly hacks, but also creates compilation databases for free, which can be consumed by tools like clangd.

- *KISS*: complex abstractions should be avoided unless they significantly improve maintainability.

What works and what doesn't?
============================

What should work at this point:

- Boot from USB, SD or eMMC with a memtest payload. *Caveat*: the memtest tries to test the entire DRAM address space, so will probably fail with <4GB boards.

- Boot to Linux via TF-A over USB. This is described in _`Booting via USB`.

- Boot to Linux from SPI, with levinboot on SD/MMC. This is described in _`Booting from SPI`

What is intended to work by 1.0, but not implemented yet:

- Use of the correct DRAM size in later stages. Currently everything after DRAM init proper assumes 4 GB of DRAM, which should work on the PBP and 4 GB RockPro64.

- Boot completely from SPI. This requires adding zero-padding support to :command:`idbtool`.

- Payload loading from other media. SD card is critical for 0.4

- basic FIT support

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

Important command-line arguments for :src:`configure.py` are:

--with-atf-headers PATH  tells :src:`configure.py` where the ATF export headers are. Without this, the :output:`elfloader.bin` stage cannot be built, and will not be configured in the `build.ninja`.

--elfloader-lz4, --elfloader-gzip, --elfloader-zstd  enables decompression in :output:`elfloader.bin`, for the respective formats.

--elfloader-spi  configures :output:`elfloader.bin` to load payload images from SPI flash instead of expecting them preloaded at specific addresses.
  This process requires decompression support to be enabled.

--embed-elfloader  configures the :output:`levinboot-*` targets to contain the :output:`elfloader.bin` binary, copy it where it needs to be and run it, instead of just returning to the mask ROM.
  This only makes sense if :output:`elfloader.bin` has is configured to load a payload by itself (see above), since otherwise it will just fail (or behave erratically in case DRAM retention has left parts of a BL31 image in RAM).

Primary build targets are:

- :output:`levinboot-usb.bin`: this is used for _`Booting via USB`.

- :output:`levinboot.img`: this is an image that can be written to sector 64 on an SD/eMMC drive.
  Configure with :cmdargs:`--elfloader-spi --embed-elfloader` to make this useful.

- :output:`memtest.bin`: this is a very simple payload and just writes pseudorandom numbers to DRAM in 128MiB blocks and reads them back to check if the values are retained.

- :output:`elfloader.bin`: this is the payload loading stage for _`Booting via USB`.
  It expects a BL31 ELF image, a FDT blob and a Linux kernel (or a similar EL2 payload like `teststage.bin`) at (currently) hardcoded DRAM addresses (or a compressed payload blob at address 32M), loads BL31 into its correct addresses (which includes SRAM), inserts address information into the FDT, and finally jumps to BL31 with parameters telling it to start Linux (or other EL2 payload)

- :output:`teststage.bin`: this is a simple EL2 payload. Currently it only dumps the passed FDT blob, if it is detected at :code:`*X0`.

:src:`release-test.sh` contains a number of configurations that are supposed to be kept working.

The Payload Blob
================

The current payload format used by levinboot consists of 3 concatenated compression frames, in the following order: BL31 ELF file, flattened device tree, kernel image.
Depending on your configuration, arbitrary combinations of LZ4, gzip and zstd frames are supported.

If you want to use levinboot to boot actual systems, keep in mind that it does not load an initcpio and will only insert a /memory node (FIXME: which is currently hardcoded to 4GB) into the device tree, so you will need to insert command line arguments or other ways to set a root file system into the device tree blob yourself.
See :src:`overlay-example.dts` for an example overlay that could be applied (using, e. g. :command:`fdtoverlay` from the U-Boot tools) on an upstream kernel device tree, which designates the part of flash starting at 7MiB as a block device containing a squashfs root.

Booting via USB
===============

The least-setup/fastest-iteration way boot a system using levinboot is to use USB spoon feeding via RK3399 mask ROM mode.

To do this:

- build the tools, specifically :command:`usbtool`. The tools are contained in the :src:`tools/` directory and have their own :src:`tools/configure`.

- build levinboot as well as any payloads you might want to run.

- remove or disable any other boot sources containing a valid ID block. These can be:

  - a SPI flash chip. On the RockPro64, this can be disabled by shorting pins 23 and 25 on the PI-2 header.
    Note that neither RockPro64 nor Pinebook Pro currently ship with an ID block on the SPI chip, so this is not necessary by default.
  
  - an eMMC chip. On the RockPro64 and Pinebook Pro, these come as removable modules.
    Removal isn't necessary though (and should be avoided because of wear on the connector) because they can be disabled by a switch right next to the module (on the Pinebook Pro) or by shorting the 2-pin header right next to the eMMC module and SPI chip (on the RockPro64).

  - an SD card.

- connect a USB OTG port (for the Pinebook Pro and RockPro64, this is the USB-C port) of your RK3399 device with a USB host port of your development host. Make sure your OS gives you access to USB devices of ID 2207:330c (RK3399 in Mask ROM mode).

  You should also connect a serial console to UART2, so you can observe the boot process.
  This is pins 6, 8, 10 on the RockPro64 (ground, TX and RX respectively) and the headphone jack on the Pinebook Pro (keep in mind this has to be activated using a switch on the board).
  Both of these use 3.3V, with levinboot setting 1.5MBaud (8 bits, no parity, no flow control) transfer rate by default (this can be changed in :src:`config.h` setting a different clock divider, i. e. 13 for 115200 baud).
  Keep in mind that BL31 by default uses 115200 baud by default, so unless you change that (in :code:`plat/rockchip/rk3399/rk3399_def.h` in the TF-A source tree or in levinboot as described before), you will not get any output from that stage.

  - (re-)start the system. Both the RockPro64 and the Pinebook Pro have a reset button on the PCB, making this a quick and simple process.

  - tell :command:`usbtool` to run levinboot and its payload. There are multiple currently working constellations for this, and if you are just getting started, you should try them in order (while resetting the system inbetween, of course).

    - levinboot and `memtest.bin`: run :command:`usbtool --call levinboot-usb.bin --run memtest.bin`.
      This should run levinboot and then start testing memory.

    - levinboot and BL31 with `teststage.bin`: run :command:`usbtool --call levinboot-usb.bin --load 100000 elfloader.bin --load 2000000 path/to/bl31.elf --load 500000 path/to/fdt-blob.dtb --load 680000 teststage.bin --jump 100000 2000` (with the paths substituted for your system).

      This should run levinboot to initialize DRAM, load all payload files into DRAM, and finally jump to :output:`elfloader.bin` which will start BL31, which will give control to :output:`teststage.bin`, which should dump the FDT header as well as its contents in DTS syntax.

    - levinboot and BL31 with a Linux kernel: this is basically the same as the previous command, just with your (uncompressed) kernel image instead of :output:`teststage.bin`.

      Beware that the loading step will take a while, because :command:`usbtool` currently uses the mask ROM code to transfer the files, which is anything but fast at receiving (or most likely, verifying) data sent over USB.

    - either of the previous two with compression: configure the build with :cmdargs:`--elfloader-decompression` and run :command:`usbtool --call levinboot-usb.bin --load 100000 elfloader.bin --load 2000000 path/to/payload-blob --jump 100000 1000` where the payload blob is constructed as described in _`The Payload Blob`, with either a 'real' kernel or :output:`teststage.bin`. This may save transfer time.

Booting from SPI
================

levinboot can load its payload images from SPI flash. This way it can be used as the first stage in a kexec-based boot flow.
Currently the build system can only produce images usable on SD or eMMC chips, not for SPI flash itself.
This is probably for the best since right now levinboot is not considered production-ready yet and as such it makes sense to store the critical part on easily-removed/-disabled storage in case it breaks.

Configure the build with :cmdargs:`--elfloader-spi --embed-elfloader` in addition to your choice of preferred compression formats (you need at least one). This will produce :output:`levinboot.img` and :output:`levinboot-usb.bin` that are self-contained in the sense that they don't require another stage to be loaded after them by the mask ROM.

You can test it over USB (see above for basic steps) with :command:`usbtool --run levinboot-usb.bin` or write :output:`levinboot.img` to sector 64 on SD/eMMC for use in self-booting.

After DRAM init, this will asynchronously read up to 16MiB of SPI flash on SPI interface 1 (which is the entire chip on a RockPro64 or Pinebook Pro) as needed, and will decompress the payload blob from it.
The flash contents after the end of _`The Payload Blob` are not used by levinboot and may be used for a root file system.

See the notes about _`The Payload Blob` for general advice on how to create it.
