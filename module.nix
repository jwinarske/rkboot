{config, lib, pkgs, ...}: {
	options.boot.loader.levinboot = {
		enable = lib.mkOption {
			default = false;
			example = true;
			type = lib.types.bool;
			description = ''
				Whether to enable the levinboot bootloader.
			'';
		};
		bootloader-device = lib.mkOption {
			default = null;
			example = "/dev/disk/by-partuuid/77414336-a555-48c7-a1e4-c2d714abfc6f";
			type = lib.types.str;
			description = ''
				Partition into which the levinboot image will be written. This should be in a place where the boot ROM can find them (sector 64 on SD cards or eMMC)
			'';
		};
		payload-device = lib.mkOption {
			default = null;
			example = "/dev/disk/by-partuuid/feb93b6a-7fd4-43c8-a1f8-a95823ae4730";
			type = lib.types.str;
			description = ''
				Partition into which the payload will be written. These should be where levinboot loads them (4 MiB into SD).
			'';
		};
		dtb = lib.mkOption {
			default = null;
			example = "rockchip/rk3399-rockpro64.dtb";
			type = lib.types.str;
			description = ''
				Relative path to the DTB to be used for the levinboot boot process.
			'';
		};
	};
	config = let cfg = config.boot.loader.levinboot; in lib.mkIf cfg.enable {
		system = {
			boot.loader.id = "levinboot";
			build.installBootLoader = let
				payload = pkgs.runCommandCC "levinboot-payload" {} ''
					cp ${pkgs.armTrustedFirmwareRK3399}/bl31.elf .
					chmod +w bl31.elf
					strip bl31.elf
					mkdir $out
					gzip -c bl31.elf >$out/bl31.gz
					gzip -ck ${config.boot.kernelPackages.kernel}/${config.system.boot.loader.kernelFile} >$out/Image.gz
				'';
			in pkgs.writeScript "install-levinboot.sh" ''
				#!${pkgs.runtimeShell}
				set -ex
				if ! test "$1"; then
					echo "Usage: $0 path/to/default-derivation"
					exit 1
				fi
				if test "$NIXOS_INSTALL_BOOTLOADER" = 1; then
					dd if=${(import ./. {inherit pkgs;}).levinboot}/levinboot.img of="${cfg.bootloader-device}"
				fi
				${pkgs.dtc}/bin/fdtput -pt s - <$1/dtbs/${cfg.dtb} /chosen bootargs "systemConfig=$1 init=$1/init `cat $1/kernel-params`" | gzip | cat ${payload}/bl31.gz - ${payload}/Image.gz $1/initrd | dd of=${cfg.payload-device}
				sync
			'';
		};
		assertions = [
			{assertion= ! builtins.isNull cfg.bootloader-device; message="cannot install levinboot without a bootloader-device";}
			{assertion= ! builtins.isNull cfg.payload-device; message="cannot install levinboot without a payload-device";}
			{assertion= ! builtins.isNull cfg.dtb; message="cannot install levinboot without a DTB";}
		];
	};
}
