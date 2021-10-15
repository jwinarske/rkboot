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
		system = let
				payload = pkgs.runCommandCC "levinboot-payload" {} ''
					cp ${pkgs.armTrustedFirmwareRK3399}/bl31.elf .
					chmod +w bl31.elf
					strip bl31.elf
					mkdir $out
					${pkgs.zstd}/bin/zstd -c bl31.elf >$out/bl31.zst
					${pkgs.zstd}/bin/zstd -c ${config.boot.kernelPackages.kernel}/${config.system.boot.loader.kernelFile} >$out/Image.zst
				'';
		in {
			boot.loader.id = "levinboot";
			build.installBootLoader = pkgs.writeScript "install-levinboot.sh" ''
				#!${pkgs.runtimeShell}
				set -ex
				if ! test "$1"; then
					echo "Usage: $0 path/to/default-derivation"
					exit 1
				fi
				if test "$NIXOS_INSTALL_BOOTLOADER" = 1; then
					dd if=${(import ./. {inherit pkgs;}).levinboot}/levinboot-sd.img of="${cfg.bootloader-device}"
				fi
				dd if=$1/levinboot-payload of=${cfg.payload-device} status=progress oflag=direct conv=fsync bs=1M
				sync
			'';
			extraSystemBuilderCmds = ''
				${pkgs.dtc}/bin/fdtput -pt s - <$out/dtbs/${cfg.dtb} /chosen bootargs "systemConfig=$out init=$out/init `cat $out/kernel-params`" | ${pkgs.zstd}/bin/zstd | cat ${payload}/bl31.zst - ${payload}/Image.zst $out/initrd >$out/levinboot-payload
			'';
		};
		assertions = [
			{assertion= ! builtins.isNull cfg.bootloader-device; message="cannot install levinboot without a bootloader-device";}
			{assertion= ! builtins.isNull cfg.payload-device; message="cannot install levinboot without a payload-device";}
			{assertion= ! builtins.isNull cfg.dtb; message="cannot install levinboot without a DTB";}
		];
	};
}
