# mainline-tools

This directory houses the envkernel.sh wrapper script I use for postmarketOS kernel development, `mainline-tools`.

## Usage

Load with `source path/to/mt.sh`. You can adjust the following variables before sourcing:

* `MT_LINUX_DIR` - path to your Linux source code (default: `~/code/linux`)
* `MT_PMBOOTSTRAP_DIR` - path to your pmbootstrap source code for helpers/envkernel.sh (default: `~/code/pmbootstrap`)
* `MT_ALIAS` - 1 (default) to enable shorthand aliases.

Commands:

* `mainline-build` (`mb`) - alias for `make -j$(nproc)`.
* `mainline-package` (`mp`) - generate a boot.img from the built kernel, bypassing pmbootstrap's image building capabilities.
* `mainline-flash` (`mf`) - flash the boot.img generated with `mp` to the device.
* `mainline-build-pkg` (`mbp`) - build the kernel package with `pmbootstrap build --envkernel`.
* `mainline-sideload` (`ms`) - sideload the kernel package built with `mbp`.
