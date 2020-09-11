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

- Starting the system using the USB mask ROM mode.

  - running a memtest.
  - Booting to Linux without a boot medium. This is described in _`Booting via USB`.

- Booting Linux from a boot medium. levinboot is currently agnostic about what medium it itself was loaded from (there might be optimization potential in adding medium-specific code to take over from mask ROM earlier – TODO: evaluate).

  - from SPI. This is described in _`Booting from SPI`
  - from SD. This is described in _`Booting from SD`
  - from eMMC. This is described in _`Booting from eMMC`

- providing entropy to the kernel (KASLR and RNG seeds) via the DTB

What is intended to work by 1.0, but not implemented yet:

- Use of the correct DRAM size in later stages. Currently everything after DRAM init proper assumes 4 GB of DRAM, which should work on the PBP and 4 GB RockPro64.

- a new payload format, allowing uncompressed segments and moving certain memory layout decisions from pre-build or runtime to payload creation time.

- booting from NVMe.

General TODOs and feature ideas, post-1.0:

- USB keyboard support (for selecting the boot medium)

- more boot medium options: USB mass storage?

- more image format options: filesystems, boot configurations, FIT containers, …

- cryptographic verification of payloads

- non-LPDDR4 DRAM init

- along with non-LPDDR4 support, support for more boards (hardware donations welcome!)

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
- if loading from eMMC is configured, it will configure GPIO0A5 as the eMMC power enable pin.

These assumptions hold as described on the RockPro64 and the Pinebook Pro. On the Rock Pi 4 (according to the schematics), the pins used for the LEDs are unconnected and GPIO0A5 is connected to the anode of a diode, which should make it read high (i. e. inactive).


Build process
=============

levinboot uses a Ninja-based build system. The build is configured by running :src:`configure.py`. This can (but doesn't have to be) be done in a different directory, keeping the build files separate from the sources.

Important command-line arguments for :src:`configure.py` are:

--with-tf-a-headers PATH  tells :src:`configure.py` where the TF-A export headers are. Without this, the :output:`elfloader.bin` stage cannot be built, and will not be configured in the `build.ninja`.

--payload-lz4, --payload-gzip, --payload-zstd  enables decompression in :output:`elfloader.bin`, for the respective formats. TODO: the LZ4 decompressor doesn't compute check hashes yet.

--payload-spi, --payload-sd, --payload-emmc  configures :output:`elfloader.bin` to load payload images from SPI flash, SD cards or eMMC storage (respectively) instead of expecting them preloaded at specific addresses.
  This process requires decompression support to be enabled.
  See _`Booting from SPI`, _`Booting from SD` and  _`Booting from eMMC` for more information.

  These options can be combined. See _`Boot Order` for a description for which payload is loaded in which case.

--payload-initcpio  configures :output:`elfloader.bin` to load an initcpio image and pass it to the kernel.
  This process requires decompression support to be enabled.

Primary build targets are:

- :output:`sramstage.bin`: this is the first stage of levinboot, used to initialize DRAM (and potentially other hardware) for use by :output:`usbstage`, :output:`memtest.bin` and/or :output:`elfloader.bin`.

- :output:`levinboot-usb.bin`: this is used for single-stage _`Booting via USB`

- :output:`levinboot-sd.img`: this is an image that can be written to sector 64 on an SD/eMMC drive.
  This target is only available if a boot medium is configured.

- :output:`memtest.bin`: this is a very simple payload and just writes pseudorandom numbers to DRAM in 128MiB blocks and reads them back to check if the values are retained.

- :output:`elfloader.bin`: this is the payload loading stage for multi-stage _`Booting via USB`.
  Depending on the configuration it can behave in different ways:

  - if no compression format is configured: starting a kernel (or similar EL2 payload like :output:`teststage.bin`) pre-loaded at 0x00280000 with a BL31 ELF pre-loaded at 0x04200000 and a DTB pre-loaded at 0x00100000.
  - if compression but no boot media are configured: decompressing and starting a compressed payload blob pre-loaded at 0x04400000.
  - if a boot medium is configured: booting from the configured boot media, like in self-boot.

- :output:`teststage.bin`: this is a simple EL2 payload. Currently it only dumps the passed FDT blob, if it is detected at :code:`*X0`.

- :output:`usbstage.bin`: this binary re-initializes the OTG USB interface and connects as a device, providing a bulk-based interface better suited for transferring large payloads than the mask ROM control-based interface.

:src:`release-test.sh` contains a number of configurations that are supposed to be kept working.

The Payload Blob
================

The current payload format used by levinboot consists of 3 or 4 concatenated compression frames, in the following order: BL31 ELF file, flattened device tree, kernel image. If configured with :cmdargs:`--elfloader-initcpio`, a compressed initcpio must be appended.
Depending on your configuration, arbitrary combinations of LZ4, gzip and zstd frames are supported.

If you want to use levinboot to boot actual systems, keep in mind that it will only insert a `/memory` node (FIXME: which is currently hardcoded to 4GB) and `/chosen/linux,initrd-{start,end}` properties into the device tree.
This means you will need to either use an initcpio or insert command line arguments or other ways to set a root file system into the device tree blob yourself.
See :src:`overlay-example.dts` for an example overlay that could be applied (using, e. g. :command:`fdtoverlay` from the U-Boot tools) on an upstream kernel device tree, which designates the part of flash starting at 7MiB as a block device containing a squashfs root.

Boot order
==========

While levinboot tries to initialize the different boot media concurrently, it does have a notion of priority, which is defined by the `DEFINE_BOOT_MEDIUM` macro in :src:`include/rk3399/dramstage.h`. The default order is SD, eMMC, SPI.

Boot media are 'cued` in priority order. Without user intervention, levinboot will 'commit' to the first payload it can successfully load. This can be prevented for all except the last configured boot medium by holding the power button at the moment when loading is complete. levinboot will give the user at least 500 ms to let go of the button to prevent accidental override.

The primary use case for this mechanism is to force booting from SPI without having to disassemble a Pinebook Pro to disable eMMC, by holding the power button until the SPI payload comes up.

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

Like all other boot media, you can test the bootloader over USB (see _`Booting via USB` for instructions) with :command:`usbtool --run levinboot-usb.bin` or write :output:`levinboot-sd.img` to sector 64 on the SD card or eMMC, or flashing :output:`levinboot-spi.img` to the start of SPI flash (see below for a way to do that without a working OS).

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

Like all other boot media, you can test the bootloader over USB (see _`Booting via USB` for instructions) with :command:`usbtool --run levinboot-usb.bin` or write :output:`levinboot-sd.img` to sector 64 on the SD card or eMMC, or flashing :output:`levinboot-spi.img` to the start of SPI flash.

While levinboot does not read partition tables on SD at this point, it may be advisable to create partitions starting at sectors 64 and 8192, for easier and potentially safer upgrades of levinboot and the payload, respectively.

Booting from eMMC
=================

levinboot can load payload images from eMMC storage. Configure it with :cmdargs:`--payload-emmc` to enable this.

The drive has to be partitioned using GPT. levinboot will then load a compressed payload blob from a partition with one of these special partition type GUIDs (not partition UUIDs!):

- payload A: e5ab07a0-8e5e-46f6-9ce8-41a518929b7c
- payload B: 5f04b556-c920-4b6d-bd77-804efe6fae01
- payload C: c195cc59-d766-4b78-813f-a0e1519099d8

Partition type GUIDs can be set in :cmd:`fdisk` by just pasting them instead of a partition type number from the list when setting partition type. The type will then be displayed as 'unknown'.

For each type, it will ignore all but the first one present in partition table order. If only one of these is present, it will load from that, if all three are present, it will take A, If 2 are present, it uses these rules:

- if A and B are present, it uses A.
- if B and C are present, it uses B.
- if C and A are present, it uses C.

It might be apparent from the enumeration that these are cyclical. The idea behind this rule set is to allow the following scheme to update payloads atomically by using 2 payload partitions: write the new payload to the partition that is currently unused, then (atomically) change the type of the old payload partition to the type that was not present before.

As with SD and USB compressed payload booting, the maximum size is 60 MiB, so reserving more space for the partitions does not make sense (typical payloads tend to stay under 30MB).

Like all other boot media, you can test the bootloader over USB (see _`Booting via USB` for instructions) with :command:`usbtool --run levinboot-usb.bin` or write :output:`levinboot-sd.img` to sector 64 on the SD card or eMMC, or flashing :output:`levinboot-spi.img` to the start of SPI flash.
