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

- Boot to Linux, completely from SD. This is described in _`Booting from SD`

What is intended to work by 1.0, but not implemented yet:

- Use of the correct DRAM size in later stages. Currently everything after DRAM init proper assumes 4 GB of DRAM, which should work on the PBP and 4 GB RockPro64.

General TODOs, not assigned to a milestone:

- more boot medium options, e. g. eMMC, NVMe

- basic FIT support

License
=======
levinboot is licensed under CC0, meaning that the author and contributors give up their copyright by placing the work in the public domain, in jurisdictions where that is possible, or if not, provide a non-exclusive license to do just about anything to the work to anyone who gets a copy, and waives all other legal interests of the author/contributor in the work.

See `<https://creativecommons.org/publicdomain/zero/1.0/legalcode>`__ for the legal text.

Source structure
================

Still in flux, but slowly converging towards what it should be.

Historical note: a lot of places still refer to 'elfloader'. Its current name is 'dramstage'. The transition will be completed in a future release.

Board support
=============

levinboot is tested on the RockPro64 and the Pinebook Pro. From looking at the schematics, it should behave safely and sanely on the Rock Pi 4 too, but this hasn't been tested (testers welcome!).

It makes the following assumptions about the board:

- The SoC uses LPDDR4 for DRAM
- an I²C bus is attached to the i2c4 pins of the SoC. It tries to read register 0 from address 0x62/(first byte byte 0xc5) and if it gets an ACK response, it assumes it is running on a Pinebook Pro and sets up a regulator on behalf of the kernel, which is needed on that platform.
- the main "Power" LED is attached (active-high) to GPIO0B3. It is lit up before starting the DRAM initialization.
- an auxiliary (e. g. "standby") LED is attached (active-high) to GPIO0A2. It is lit up after the payload is loaded.
- ADC1 is connected to an active-low "recovery" button.
- GPIO0A5 is connected to an active-low "power" button. This is read in a _`SPI Recovery` setup.
- if loading from SPI is configured, it tries to read using the normal 0x0b 'fast read' command with 3 address and 1 dummy byte at 50 MHz on the SPI1 pins
- if loading from SD is configured, it will configure the SD pins on GPIO4B0-5 (inclusive) and the card detect pin on GPIO0A7.

These assumptions hold as described on the RockPro64 and the Pinebook Pro. On the Rock Pi 4 (according to the schematics), the pins used for the LEDs are unconnected and GPIO0A5 is connected to the anode of a diode, which should make it read high (i. e. inactive).


Build process
=============

levinboot uses a Ninja-based build system. The build is configured by running :src:`configure.py`. This can (but doesn't have to be) be done in a different directory, keeping the build files separate from the sources.

Important command-line arguments for :src:`configure.py` are:

--with-tf-a-headers PATH  tells :src:`configure.py` where the TF-A export headers are. Without this, the :output:`elfloader.bin` stage cannot be built, and will not be configured in the `build.ninja`.

--elfloader-lz4, --elfloader-gzip, --elfloader-zstd  enables decompression in :output:`elfloader.bin`, for the respective formats. TODO: the LZ4 decompressor doesn't compute check hashes yet.

--elfloader-spi, --elfloader-sd  configures :output:`elfloader.bin` to load payload images from SPI flash or SD cards (respectively) instead of expecting them preloaded at specific addresses.
  This process requires decompression support to be enabled.
  See _`Booting from SPI` and _`Booting from SD` for more information, respectively.

  The two options can be combined, in which case :output:`elfloader.bin` will try to boot from SD, and if that fails (or if the power button is pressed when loading the payload finishes) it will boot from SPI instead.
  See _`SPI Recovery`

--elfloader-initcpio  configures :output:`elfloader.bin` to load an initcpio image and pass it to the kernel.
  This process requires decompression support to be enabled.

Primary build targets are:

- :output:`sramstage.bin`: this is the first stage of levinboot, used to initialize DRAM (and potentially other hardware) for use by :output:`usbstage`, :output:`memtest.bin` and/or :output:`elfloader.bin`.

- :output:`levinboot-usb.bin`: this is used for single-stage _`Booting via USB`

- :output:`levinboot-sd.img`: this is an image that can be written to sector 64 on an SD/eMMC drive.
  Configure with :cmdargs:`--elfloader-spi --embed-elfloader` to make this useful.

- :output:`memtest.bin`: this is a very simple payload and just writes pseudorandom numbers to DRAM in 128MiB blocks and reads them back to check if the values are retained.

- :output:`elfloader.bin`: this is the payload loading stage for multi-stage _`Booting via USB`.
  It expects a BL31 ELF image, a FDT blob and a Linux kernel (or a similar EL2 payload like `teststage.bin`) at (currently) hardcoded DRAM addresses (or a compressed payload blob at address 32M), loads BL31 into its correct addresses (which includes SRAM), inserts address information into the FDT, and finally jumps to BL31 with parameters telling it to start Linux (or other EL2 payload).

- :output:`teststage.bin`: this is a simple EL2 payload. Currently it only dumps the passed FDT blob, if it is detected at :code:`*X0`.

- :output:`usbstage.bin`: this binary re-initializes the OTG USB interface and connects as a device, providing a bulk-based interface better suited for transferring large payloads than the mask ROM control-based interface.

:src:`release-test.sh` contains a number of configurations that are supposed to be kept working.

The Payload Blob
================

The current payload format used by levinboot consists of 3 concatenated compression frames, in the following order: BL31 ELF file, flattened device tree, kernel image. If configured with :cmdargs:`--elfloader-initcpio`, a compressed initcpio must be appended.
Depending on your configuration, arbitrary combinations of LZ4, gzip and zstd frames are supported.

If you want to use levinboot to boot actual systems, keep in mind that it will only insert a `/memory` node (FIXME: which is currently hardcoded to 4GB) and `/chosen/linux,initrd-{start,end}` properties into the device tree.
This means you will need to either use an initcpio or insert command line arguments or other ways to set a root file system into the device tree blob yourself.
See :src:`overlay-example.dts` for an example overlay that could be applied (using, e. g. :command:`fdtoverlay` from the U-Boot tools) on an upstream kernel device tree, which designates the part of flash starting at 7MiB as a block device containing a squashfs root.

Booting via USB
===============

The least-setup/fastest-iteration way boot a system using levinboot is to use USB spoon feeding via RK3399 mask ROM mode.

To prepare, you will need to do the following:

- build the tools, specifically :command:`usbtool`. The tools are contained in the :src:`tools/` directory and have their own :src:`tools/configure`.

- build levinboot as well as any payloads you might want to run.

- bring the system into USB mask ROM mode. This can be done by means of a 'recovery button' as implemented by levinboot and certain U-Boot builds, or by starting the system after removing or disabling any other boot sources containing a valid ID block. These can be:

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

There are several possible boot processes via USB:

- single-stage USB boot: :command:`usbtool --run levinboot-usb.bin`

  This is the simplest USB boot process, as it is equivalent to the self-boot images. Like the self-boot images, :output:`levinboot-usb.bin` can only be built if it is configured to use boot media.

  The primary purpose of this boot process is testing self-boot configurations in a situation as close as possible to self-boot, but without having to write to boot media.

- two-stage USB boot using boot media: :command:`usbtool --call sramstage.bin --load 4000000 elfloader.bin --jump 4000000 1000`

  This is functionally equivalent to the first, with the difference that sramstage does not unpack an embedded copy of dramstage (elfloader), which means that the build-process is slightly simpler and faster.

  This is useful for quickly testing dramstage changes related to boot medium handling. It is mutually exclusive with the next option:

- two-stage USB boot with mask-ROM transfer: :command:`usbtool --call sramstage.bin --load 4000000 elfloader.bin --load 4200000 path/to/bl31.elf --load 100000 path/to/fdt-blob.dtb --load 280000 teststage.bin --jump 4000000 1000` (with the paths substituted for your system)

  This should run sramstage to initialize DRAM, load all payload files into DRAM, and finally jump to :output:`elfloader.bin` which will start BL31, which will give control to :output:`teststage.bin`, which should dump the FDT header as well as its contents in DTS syntax.

  The primary use case for this boot process is testing any changes related to payload handoff, especially for small payloads.

  You can use an (uncompressed) kernel image instead of teststage, though beware that mask-ROM-based transfers are rather slow. Instead it is recommended to use the following:

- three-stage USB boot without compression: :command:`usbtool --call sramstage.bin --run usbstage.bin --load 100000 path/to/fdt-blob.dtb --pload 280000 path/to/kernel/Image --pload 4200000 path/to/bl31.elf --load 4000000 elfloader.bin --start 4000000 4102000`

  This will use faster bulk transfers to copy the payload into memory. Note that neither this nor the previous boot process can use an initcpio, since compression is needed for framing.

- three-stage USB boot with compression: :command:`usbtool --call sramstage.bin --run usbstage.bin --load 4400000 path/to/payload-blob --load 4000000 elfloader.bin --start 4000000 4102000`

  Note that usbstage can use stdin instead of a file by specifying '-'.

  The usecase for this is booting actual systems (i. e. not payloads designed to test levinboot) via USB.

You can also test DRAM by running :command:`usbtool --call sramstage.bin --run memtest.bin`. Furthermore, usbstage can also be used for _`Flashing SPI`.

Booting from SPI
================

levinboot can load its payload images from SPI flash. This way it can be used as the first stage in a kexec-based boot flow.

Configure the build with :cmdargs:`--elfloader-spi` in addition to your choice of preferred compression formats (you need at least one). This will produce :output:`levinboot-sd.img` and :output:`levinboot-usb.bin` that are self-contained in the sense that they don't require another stage to be loaded after them by the mask ROM.

You can test it over USB (see above for basic steps) with :command:`usbtool --run levinboot-usb.bin`, write :output:`levinboot-sd.img` to sector 64 or write :output: on SD/eMMC for use in self-booting.

After DRAM init, this will asynchronously read up to 16MiB of SPI flash on SPI interface 1 (which is the entire chip on a RockPro64 or Pinebook Pro) as needed, starting from address 0x40000 (256 KiB offset from start), and will decompress the payload blob from it.
The flash contents after the end of _`The Payload Blob` are not used by levinboot and may be used for a root file system.

See the notes about _`The Payload Blob` for general advice on how to create it.

Recovery Button
---------------

The "Recover" button on the RockPro64/Rock Pi 4 and inside the Pinebook Pro can be used to put the SoC in mask ROM USB gadget boot mode, which can be used to reflash it or otherwise start a different bootloader.
This button is checked very early in levinboot, allowing you to recover from SPI mis-flashes without hardware modification such as shorting the SPI clock, as long as a certain (small) part of levinboot is still intact.

The recovery button function is built in all configurations of levinboot, even though it is mostly useful for SPI images, because unlike SD cards it cannot be removed and unlike eMMC it cannot be disabled using a button or switch.

Flashing SPI
------------

You can write to SPI anytime you can boot via USB, as described above: :output:`usbstage.bin` implements a command to write a block of data (such as a levinboot image) to any erase-block-(typically 4k-)aligned address in SPI flash.
Run :command:`usbtool --call sramstage.bin --run usbstage.bin --flash 0 your.img` where `0` is the start address for the image, and `your.img` is the file you want to flash.

Booting from SD
===============

levinboot can load payload images from SDHC and SDXC cards.
Compared to SPI payload loading, this offers potentially better performance and the ability to load larger payloads (currently limited to 60 MiB compressed, with the decompressed kernel needing to stay under 61.5 MiB because of the elfloader memory layout) than e. g. the 16 MiB flash chip of the Pinebook Pro or RockPro64.

Configure the build with :cmdargs:`--elfloader-sd` in addition to your choice of preferred compression format (you need at least one).

The output images (:output:`levinboot-sd.img` and :output:`levinboot-usb.bin`) will initialize the SDMMC block and try to start an SDHC/SDXC card connected to it, currently at 25 MHz bus frequency, and load up to 60 MiB of payload starting at sector 8192 (4 MiB offset), as needed for decompression.

You can test the bootloader over USB (see _`Booting via USB` for instructions) with :command:`usbtool --run levinboot-usb.bin` or write :output:`levinboot-sd.img` to sector 64 on the SD card (or eMMC if you feel like it, but it makes little sense).

While levinboot does not read partition tables at this point, it may be advisable to create partitions starting at sectors 64 and 8192, for easier and potentially safer upgrades of levinboot and the payload, respectively.

SPI fallback
------------

If configured with both :cmdargs:`--elfloader-sd` and :cmdargs:`--elfloader-spi`, levinboot will first try to load the payload from SD cards.
When this finishes, with the power button (still) held, levinboot will load the payload from SPI instead.
The same will happen if initializing the SD card fails.
