.. SPDX-License-Identifier: CC0-1.0

.. role:: artifact(code)
.. |project| replace:: levinboot

Image types
-----------

The RK3399 mask boot ROM supports 4 boot flows:

1. image on SPI-NOR
2. image on eMMC
3. image on SD
4. transfer via USB

From these, eMMC and SD use the same image format. The image format on SPI-NOR differs from that in that every 2 KiB of useful code, 2 KiB of padding must be inserted.

This gives rise to 3 groups of artifacts produced by the |project| build process:

- SPI image: :artifact:`levinboot-spi.img`
- SD/eMMC image: :artifact:`levinboot-sdmmc.img`
- USB boot binaries: :artifact:`loader-usb.bin`, :artifact:`sramstage-usb.bin`

Boot stages
-----------

|project| boots the RK3399 in 6 stages:

1. BROM – This is etched into the SoC and is not part of |project|.
2. loader – This is the first stage of |project| and does some CPU initialization before loading sramstage, more efficiently than the BROM could.
3. sramstage – second stage of |project|, does most early hardware initialization, most importantly brings up DRAM.
4. dramstage – third stage of |project|, puts the payload pieces into place, potentially with user interaction
5. BL31 – resident firmware provided by TF-A, implements PSCI
6. OS

loader
......

The loader stage performs the following tasks (in rough order):

- bring the boot CPU into a sane state

  - set SCTLR and SCR (most importantly this enables the instruction cache)
  - set chicken bits and other model-specific registers

- set up the architectural counter
- give a sign of life over UART
- on boards with a recovery key: jump to mask ROM USB code if asserted
- make as much space as possible at the start of main SRAM

  - move relevant BROM data structures to higher addresses
  - set up a new stack at a higher address

- set up initial page tables (at top of main SRAM) and enable MMU and data caches
- load and start sramstage
