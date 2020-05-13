This directory is .gitignored because it is where test artifacts for `release-test.sh` may be stored.
Artifacts used by `release-test.sh` include:

fdt.dtb
  A flattened device tree

Image
  an uncompressed kernel image

Image.zst
  a zstd-compressed kernel image

bl31.elf
  a BL31 ELF file from TF-A

bl31.gz
  a gzip-compressed BL31 ELF file
