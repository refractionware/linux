#!/usr/bin/bash

# mainline-tools v2 - postmarketOS envkernel.sh convenience wrapper

##
## SETTINGS
##

# $MT_LINUX_DIR: Directory for Linux source code.
MT_LINUX_DIR=${MT_LINUX_DIR:-"$HOME"/code/linux}
# $MT_PMBOOTSTRAP_DIR: Directory for pmbootstrap source code (needed for envkernel.sh).
MT_PMBOOTSTRAP_DIR=${MT_PMBOOTSTRAP_DIR:-"$HOME"/code/pmbootstrap}
# $MT_BOOTIMG_TARGET_DIR: Directory where generated boot.img will be placed. Defaults to kernel source tree.
MT_BOOTIMG_TARGET_DIR=${MT_PMBOOTSTRAP_DIR:-"$HOME"/code/linux}
# $MT_ALIASES: 1 to add shorthand aliases (mb, mp, mbp, mf).
MT_ALIASES=${MT_ALIASES:-1}

##
## INTERNAL FUNCTIONS
##

_mt_find_device_pkg_dir() {
	device="$(pmbootstrap config device)"
	pmaports_dir="$(pmbootstrap config aports)"
	device_pkg_dir=`find "$pmaports_dir"/device -maxdepth 2 -name "device-$device" -type d | head -n1`
	[ "$device_pkg_dir" ] || return 1

	echo "$device_pkg_dir"
}

_mt_kernel_pkg() {
	device_pkg_dir=$(_mt_find_device_pkg_dir)
	[ "$device_pkg_dir" ] || return 1
	apkbuild_path="$device_pkg_dir"/APKBUILD

	source "$apkbuild_path"
	for dep in $depends; do
		case "$dep" in
			"linux-"*) echo $dep; return;;
		esac
	done
}

##
## COMMANDS
##

# mainline-build - build the kernel.
alias mainline-build="make -j$(nproc)"
[ "$MT_ALIASES" = "1" ] && alias mb=mainline-build

# mainline-package - package the boot.img.
mainline-package() {
	local device

	# Find deviceinfo file
	device_pkg_dir=$(_mt_find_device_pkg_dir)
	[ "$device_pkg_dir" ] || return 1
	deviceinfo_path="$device_pkg_dir"/deviceinfo

	# Get pmbootstrap workdir for chroot
	workdir="$(pmbootstrap config work)"
	tempdir="$workdir/chroot_native/tmp/mainline/"

	# Extract parameters from deviceinfo
	source "$deviceinfo_path"
	_dtb=$(find "$MT_LINUX_DIR"/.output -name "$(basename $deviceinfo_dtb)".dtb)

	if [ "$deviceinfo_arch" = "aarch64" ]; then
		_image=arm64/boot/Image
	else
		_image=arm/boot/zImage
	fi

	# Generate boot.img
	pmbootstrap chroot -- apk add android-tools mkbootimg dtbtool || return $?
	if ! [ -d "$tempdir" ]; then mkdir -p "$tempdir" || return $?; fi
	cat "$MT_LINUX_DIR"/.output/arch/$_image $_dtb > "$MT_LINUX_DIR"/.zImage-dtb || return $?
	sudo cp "$MT_LINUX_DIR"/.zImage-dtb  "$tempdir"/zImage || return $?
	sudo cp "/tmp/postmarketOS-export/boot.img" "$tempdir/boot.img" || return $?
	sudo cp "/tmp/postmarketOS-export/initramfs" "$tempdir/initramfs" || return $?
	pmbootstrap chroot -- mkbootimg-osm0sis \
		--kernel "/tmp/mainline/zImage" \
		--ramdisk "/tmp/mainline/initramfs" \
		--base $deviceinfo_flash_offset_base \
		--second_offset $deviceinfo_flash_offset_second \
		--kernel_offset $deviceinfo_flash_offset_kernel \
		--ramdisk_offset $deviceinfo_flash_offset_ramdisk \
		--tags_offset $deviceinfo_flash_offset_tags \
		--pagesize $deviceinfo_flash_pagesize \
		-o "/tmp/mainline/boot.img" || return $?

	# Copy boot.img to target directory
	[ -e "$MT_BOOTIMG_TARGET_DIR"/boot.img ] && \
		mv "$MT_BOOTIMG_TARGET_DIR"/boot.img \
		   "$MT_BOOTIMG_TARGET_DIR"/boot.img.1
	cp "$tempdir"/boot.img "$MT_BOOTIMG_TARGET_DIR"/boot.img

	rm -rf "$tempdir" || return $?
}
[ "$MT_ALIASES" = "1" ] && alias mp=mainline-package

# mainline-flash - flash the boot.img generated with mainline-package
mainline-flash() {
	if [ ! -e "$MT_BOOTIMG_TARGET_DIR"/boot.img ]; then
		echo "ERROR: No boot.img generated; run mainline-package first"
		return 1
	fi

	# Find deviceinfo file
	device_pkg_dir=$(_mt_find_device_pkg_dir)
	[ "$device_pkg_dir" ] || return 1
	deviceinfo_path="$device_pkg_dir"/deviceinfo
	source "$deviceinfo_path"

	case $deviceinfo_flash_method in
		"fastboot") sudo fastboot flash boot "$MT_BOOTIMG_TARGET_DIR"/boot.img;;
		"heimdall"*) sudo heimdall flash --${deviceinfo_flash_heimdall_partition_kernel:-KERNEL} "$MT_BOOTIMG_TARGET_DIR"/boot.img;;
		*) echo "Unsupported flashing format"; return 2;;
	esac
}
[ "$MT_ALIASES" = "1" ] && alias mf=mainline-flash

# mainline-build-pkg - build kernel package for the current device with `pmbootstrap build --envkernel`.
mainline-build-pkg() {
	pmbootstrap build --envkernel $(_mt_kernel_pkg) || return $?
	# envkernel.sh needs to be reloaded to re-set the environment afterwards
	source "$MT_PMBOOTSTRAP_DIR"/helpers/envkernel.sh
}
[ "$MT_ALIASES" = "1" ] && alias mbp=mainline-build-pkg

# mainline-sideload-pkg - sideload kernel package for the current device with `pmbootstrap build --envkernel`.
mainline-sideload-pkg() {
	pmbootstrap sideload $(_mt_kernel_pkg) || return $?
}
[ "$MT_ALIASES" = "1" ] && alias msp=mainline-sideload-pkg

##
## INIT
##

read -rep "[mainline-tools] Run initial setup? [Y/n] " tmp
if [[ "$tmp" != "n" ]] && [[ "$tmp" != "N" ]]; then
	cd "$MT_LINUX_DIR"
	if [ ! -e /tmp/postmarketOS-export ]; then pmbootstrap export; fi
	source "$MT_PMBOOTSTRAP_DIR"/helpers/envkernel.sh
	export _MT_ACTIVE=1
fi
