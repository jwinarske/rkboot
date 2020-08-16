# SPDX-License-Identifier: CC0-1.0
{pkgs ? import <nixos> {}}:
let
  host = pkgs;
  aarch64 =  if builtins.currentSystem == "aarch64-linux"
    then pkgs
    else pkgs.pkgsCross.aarch64-multiplatform-musl;
  inherit (host) lib;
  atf-sources = host.fetchFromGitHub {
      owner = "ARM-software";
      repo = "arm-trusted-firmware";
      rev = "refs/tags/v2.2";
      sha256 = "03fjl5hy1bqlya6fg553bqz7jrvilzrzpbs87cv6jd04v8qrvry8";
    };
in
{
  levinboot = aarch64.stdenv.mkDerivation {
    pname = "levinboot";
    version = "0.7";
    nativeBuildInputs = [host.ninja host.python3 host.lz4];
    configurePhase = ''
      mkdir build
      cd build
      python3 ../configure.py --with-tf-a-headers ${atf-sources}/include/export --elfloader-{gzip,initcpio,sd}
    '';
    installPhase = "mkdir -p $out; cp sramstage.bin memtest.bin usbstage.bin levinboot-usb.bin levinboot-sd.img levinboot-spi.img teststage.bin $out";
    depsBuildBuild = [host.buildPackages.stdenv.cc];
    src = builtins.filterSource
      (path: type:
        type == "directory"
        || lib.strings.hasSuffix ".h" path
        || lib.strings.hasSuffix ".c" path
        || lib.strings.hasSuffix ".S" path
        || lib.strings.hasSuffix ".txt" path
        || lib.strings.hasSuffix ".py" path
        || lib.strings.hasSuffix ".sh" path
      )
      ./.;
  };
  tools = host.stdenv.mkDerivation {
    pname = "levinboot-tools";
    version = "0.0.1";
    buildInputs = [host.libusb1];
    nativeBuildInputs = [host.pkg-config host.ninja];
    preConfigure = "cd tools";
    installPhase = "mkdir -p $out/bin; cp usbtool idbtool regtool unpacktool $out/bin";
    src = builtins.filterSource (path: type: type != "directory" || {compression=null;tools=null;include=null;} ? ${builtins.baseNameOf path}) ./.;
  };
}
