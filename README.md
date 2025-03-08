# refractionware Linux repo

Welcome to my humble Linux fork. You're probably here because you're curious about my work on mainlining various devices. In any case, hello, and I hope you can find what you're interested in (spoiler alert: I'm probably slacking off on updating any of it...)

This README contains a general explaination of each of the branches within.

**Beware!** I prune old branches every once in a while to keep the repo uncluttered. Usually these don't contain anything interesting, I try to carry my patches across versions, but if you plan to link directly to something in these repos, consider mirroring it elsewhere. Thanks!

## b4/*

Patches as they are submitted to the mailing list; contain the most up-to-date and WIP versions of patches.

### Broadcom Kona

* `b4/bcm59054`: BCM59054 support + regulator driver fixes.
* `b4/kona-bus-clock`: Broadcom Kona bus clock support patches.
* `b4/bcm21664-pinctrl`: BCM21664 pinctrl support (merged!)
* `b4/kona-gpio-fixes`: Fixes for GPIO bugs on Broadcom Kona (merged!)

### Samsung Exynos 4

* `b4/max77693-charger-extcon`: MAX77693 charger extcon + OTG support patches. https://lists.sr.ht/~postmarketos/upstreaming/patches/53020
* `b4/exynos-phy-extcon`: patch for extcon-based USB mode switch toggling, not sent yet and likely won't be pending a better way to do this
* `b4/max17042-soc-threshold-fix`: Fix SOC threshold calculation on MAX17042. Yet to be sent.

## tab3/*

Branches containing work-in-progress code for the Samsung Galaxy Tab 3 8.0. For an up-to-date mainline status, see the [wiki page on the postmarketOS wiki](https://wiki.postmarketos.org/wiki/Samsung_Galaxy_Tab_3_8.0_(SM-T310)_(samsung-lt01wifi)) (pretty much any change I make, or bug I find, I instantly write down there, even if it's not in my repos yet!)

Some of these branches are pushed to https://gitlab.com/knuxify/linux, which is a fork of the [exynos4-mainline/linux](https://gitlab.com/exynos4-mainline/linux) repo - usually for making MRs against said fork.

- `tab3/display-tweaks`: Random work on display tweaks, in an attempt to fix display issues on my second Tab 3. Turns out the driver doesn't write a few values, so that's fixed, and the timing calculations are off for horizontal porch. These are fixed (or worked around) in that branch, but display still breaks sometimes, pending more investigation.
- `tab3/ir`: Driver for ABOV MC96FR332AUB chip used in the Tab 3 8.0/Note 10.1 for IR transmission. Seems to work (IR light lights up), but my testing has been inconclusive so far (could not get my TV to power on with lirc).

### Older branches

- `tab3/otg-take-2`: Second attempt at charging+OTG fixes. Charger patches being prepared in `b4/max77693-charger-extcon`, extcon patches will have to be redone. No longer developed - all relevant patches have been merged into the exynos4-mainline tree, further work will be done in separate branches. Might be deleted soon.
- `tab3/headset-detect`: WIP branch for headset detection, outdated. Final patches being prepared in `b4/midas-wm1811-gpio-jack`. Might be deleted soon.
- `tab3/6.7.6`: current "in-dev" tree for the Tab 3, containing a mish-mash of changes from prepared patchsets and other tweaks. Based off exynos4-mainline Linux 6.7.6 kernel. Deprecated, might be deleted soon.
- `tab3/6.5.1`: old stable, based off exynos4-mainline Linux 6.5.1 kernel. Deprecated, might be deleted soon.
- `tab3/wip-otg`: Very old patches attempting to fix charging/OTG. Replaced by `tab3/otg-take-2`. Might be deleted once OTG work is over.

## baffinlite/*

Branches containing work-in-progress code for Broadcom Kona support. Stable branches reside on the [bcm-kona-mainline/linux](https://github.com/bcm-kona-mainline/linux) repo.

- `baffinlite/usb`: USB peripheral mode patches. Works, but needs cleanups. (Might do host mode too while we're at it?)
- `baffinlite/timer-experiments`: More complete timer driver (clockevents still broken though...)
- `baffinlite/bcm59054-ponkey`: Named PONKEY after the power-on key driver, but actually has a whole bunch of BCM59054 device drivers.
- `baffinlite/cs02-mmc-experiments`: Random patches from when I was trying to figure out MMC issues on the BCM21664.
- `baffinlite/6.10`: Old in-dev tree for Grand Neo, might be removed soon

