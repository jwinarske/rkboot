{ pkgs ? import <nixpkgs> {} }:
let
	lb = import ./default.nix {inherit pkgs;};
in pkgs.mkShell {
	packages = [pkgs.openocd lb.tools pkgs.busybox];
	inputsFrom =  [lb.levinboot];
}
