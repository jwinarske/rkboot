# SPDX-License-Identifier: CC0-1.0
{pkgs ? import <nixos> {}}:
let
  host = pkgs;
  aarch64 =  if builtins.currentSystem == "aarch64-linux"
    then pkgs
    else pkgs.pkgsCross.aarch64-multiplatform-musl;
  inherit (host) lib;
in
{
  levinboot = aarch64.stdenv.mkDerivation {
    pname = "levinboot";
    version = "0.0.1";
    nativeBuildInputs = [host.ninja host.python3];
    configurePhase = ''
      mkdir build
      cd build
      python3 ../configure.py >build.ninja
    '';
    installPhase = "mkdir -p $out; cp levinboot-usb.bin levinboot.img $out";
    depsBuildBuild = [host.buildPackages.stdenv.cc];
    src = builtins.filterSource
      (path: type:
        type == "directory"
        || lib.strings.hasSuffix ".h" path
        || lib.strings.hasSuffix ".c" path
        || lib.strings.hasSuffix ".S" path
        || lib.strings.hasSuffix ".txt" path
        || lib.strings.hasSuffix ".ld" path
        || lib.strings.hasSuffix ".py" path
      )
      ./.;
  };
  tools = host.stdenv.mkDerivation {
    pname = "levinboot-tools";
    version = "0.0.1";
    buildInputs = [host.libusb1];
    nativeBuildInputs = [host.pkg-config host.ninja];
    installPhase = "mkdir -p $out/bin; cp usbtool idbtool regtool $out/bin";
    src = ./tools;
  };
}
