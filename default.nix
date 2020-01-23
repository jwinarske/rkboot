{pkgs ? import <nixos> {
  #crossSystem={config="aarch64-unknown-linux-musl";};
}}: pkgs.clangStdenv.mkDerivation{
  name="ddrinit";
  depsBuildBuild=[pkgs.buildPackages.stdenv.cc];
  buildInputs = [pkgs.libusb1];
  nativeBuildInputs = [pkgs.pkg-config];
  src=./.;
}
