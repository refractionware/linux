# refractionware Linux repo

Welcome to my humble Linux fork. You're probably here because you're curious about my work on mainlining various devices. In any case, hello, and I hope you can find what you're interested in (spoiler alert: I'm probably slacking off on updating any of it...)

This README contains a general explaination of each of the branches within.

**Beware!** I prune old branches every once in a while to keep the repo uncluttered. Usually these don't contain anything interesting, I try to carry my patches across versions, but if you plan to link directly to something in these repos, consider mirroring it elsewhere. Thanks!

## b4/*

Patches as they are submitted to the mailing list; contain the most up-to-date and working versions of patches.

* `b4/midas-wm1811-gpio-jack`: upstreaming for `tab3/headset-detect` branch (contains the latest version of those patches!); currently on v4, https://lists.sr.ht/~postmarketos/upstreaming/patches/52939
* `b4/max77693-charger-extcon`: upstreaming for the charger parts of `tab3/otg-take-2` (contains the latest version of those patches!); currently on v1 (v2 WIP is being pushed here), https://lists.sr.ht/~postmarketos/upstreaming/patches/53020
* `b4/bcm21664-common`: "ARM: dts: bcm-mobile: Split out nodes used by both BCM21664 and BCM23550"; currently on v1, https://lists.sr.ht/~postmarketos/upstreaming/%3C20240605-bcm21664-common-v1-0-6386e9141eb6@gmail.com%3E (did not get detected as a patch for some reason?)
* `b4/exynos-phy-extcon`: patch for extcon-based USB mode switch toggling, not sent yet
* `b4/bcm59054`: BCM59054 support + regulator driver fixes. Yet to be sent.
* `b4/max17042-soc-threshold-fix`: Fix SOC threshold calculation on MAX17042. Yet to be sent.

## tab3/*

Branches containing work-in-progress code for the Samsung Galaxy Tab 3 8.0. For an up-to-date mainline status, see the [wiki page on the postmarketOS wiki](https://wiki.postmarketos.org/wiki/Samsung_Galaxy_Tab_3_8.0_(SM-T310)_(samsung-lt01wifi)) (pretty much any change I make, or bug I find, I instantly write down there, even if it's not in my repos yet!)

Some of these branches are pushed to https://gitlab.com/knuxify/linux, which is a fork of the [exynos4-mainline/linux](https://gitlab.com/exynos4-mainline/linux) repo - usually for making MRs against said fork.

- `tab3/display-tweaks`: Random work on display tweaks, in an attempt to fix display issues on my second Tab 3. Turns out the driver doesn't write a few values, so that's fixed, and the timing calculations are off for horizontal porch. These are fixed (or worked around) in that branch, but display still breaks sometimes, pending more investigation.

### Older branches

- `tab3/otg-take-2`: Second attempt at charging+OTG fixes. Charger patches being prepared in `b4/max77693-charger-extcon`, extcon patches will have to be redone. No longer developed - all relevant patches have been merged into the exynos4-mainline tree, further work will be done in separate branches. Might be deleted soon.
- `tab3/headset-detect`: WIP branch for headset detection, outdated. Final patches being prepared in `b4/midas-wm1811-gpio-jack`. Might be deleted soon.
- `tab3/6.7.6`: current "in-dev" tree for the Tab 3, containing a mish-mash of changes from prepared patchsets and other tweaks. Based off exynos4-mainline Linux 6.7.6 kernel. Deprecated, might be deleted soon.
- `tab3/6.5.1`: old stable, based off exynos4-mainline Linux 6.5.1 kernel. Deprecated, might be deleted soon.
- `tab3/wip-otg`: Very old patches attempting to fix charging/OTG. Replaced by `tab3/otg-take-2`. Might be deleted once OTG work is over.

## baffinlite/*

Branches containing work-in-progress code for Broadcom Kona support. Stable branches reside on the [bcm-kona-mainline/linux](https://github.com/bcm-kona-mainline/linux) repo.

- `baffinlite/6.10`: current "in-dev" tree for the Grand Neo.
