# SPDX-License-Identifier: CC0-1.0
{pkgs ? import <nixos> {
  #crossSystem={config="aarch64-unknown-linux-musl";};
}}: {
  levinboot = pkgs.clangStdenv.mkDerivation {
    pname = "levinboot";
    version = "0.0.1";
    nativeBuildInputs = [pkgs.ninja pkgs.python3];
    configurePhase = ''
      mkdir build
      cd build
      python3 ../configure.py >build.ninja
    '';
    installPhase = "mkdir -p $out; cp levinboot.bin levinboot.img $out";
    depsBuildBuild = [pkgs.buildPackages.stdenv.cc];
    src = ./.;
  };
  tools = pkgs.stdenv.mkDerivation {
    pname = "levinboot-tools";
    version = "0.0.1";
    buildInputs = [pkgs.libusb1];
    nativeBuildInputs = [pkgs.pkg-config pkgs.ninja];
    installPhase = "mkdir -p $out/bin; cp usbtool idbtool regtool $out/bin";
    src = ./tools;
  };
}
