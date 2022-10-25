# SPDX-License-Identifier: CC0-1.0
{pkgs ? import <nixpkgs> {}}:
let
  version = "0.9.0";
  host = pkgs;
  aarch64 =  if builtins.currentSystem == "aarch64-linux"
    then pkgs
    else pkgs.pkgsCross.aarch64-multiplatform-musl;
  inherit (host) lib;
  tools = host.stdenv.mkDerivation {
    pname = "levinboot-tools";
    inherit version;
    buildInputs = [host.libusb1];
    nativeBuildInputs = [host.pkg-config host.ninja];
    preConfigure = "cd tools";
    installPhase = "mkdir -p $out/bin; cp lbusb $out/bin";
    src = ./.;
  };
in aarch64.stdenv.mkDerivation {
  pname = "levinboot";
  inherit version;
  passthru = {inherit tools;};
}
