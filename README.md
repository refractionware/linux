# refractionware Linux repo

Welcome to my humble Linux fork. You're probably here because you're curious about my work on mainlining various devices. In any case, hello, and I hope you can find what you're interested in (spoiler alert: I'm probably slacking off on updating any of it...)

This README contains a general explaination of each of the branches within.

**Beware!** I prune old branches every once in a while to keep the repo uncluttered. Usually these don't contain anything interesting, I try to carry my patches across versions, but if you plan to link directly to something in these repos, maybe reconsider. Thanks!

## tab3/*

Branches containing code for the Samsung Galaxy Tab 3 8.0. For an up-to-date mainline status, see the [wiki page on the postmarketOS wiki](https://wiki.postmarketos.org/wiki/Samsung_Galaxy_Tab_3_8.0_(SM-T310)_(samsung-lt01wifi)) (pretty much any change I make, or bug I find, I instantly write down there, even if it's not in my repos yet!)

In general, if you're looking for my Tab3 work, much of it is pushed to my GitLab repo at https://gitlab.com/knuxify/linux, which is a fork of the [exynos4-mainline/linux](https://gitlab.com/exynos4-mainline/linux) repo. This is also where most code for my MRs to the exynos4-mainline linux repo reside.

Sometimes though, I'll push some updates here. (This will probably also become my go-to repo for in-dev work if I ever do anything more involved, but in general, I use the GitLab repo since I make MRs to the exynos4-mainline repo from it.)

- `tab3/6.5.1`: old stable, based off exynos4-mainline Linux 6.5.1 kernel. Deprecated.
- `tab3/6.7.6`: new stable, based off exynos4-mainline Linux 6.7.6 kernel.
- `tab3/wip-otg`: (this should be moved to `wip/`...) Patch for MAX77689 MFD to 1. fix cable detection and 2. enable OTG. Does not handle OTG power yet. Also doesn't have working peripheral/OTG switching. Also also is missing at least one commit, be warned. This one should be re-done from scratch...

## baffinlite/*

Branches containing work-in-progress code for Broadcom Kona support. It's rare that I push these here; most of them reside on the [bcm-kona-mainline/linux](https://github.com/bcm-kona-mainline/linux) repo.

*No such branches at the moment.*

## wip/*

Work-in-progress branches, as the name may suggest. Usually a few hacky commits, though sometimes I put in the effort to keep things upstreamable from the start. I usually just push these so that I can snapshot my progress, or quickly share some concepts, so readability isn't exactly a priority.

- `wip/bcm59054-redo`: I submitted a patch to add BCM59054 support to the regulator, and somebody suggested that the whole thing could be done better so I rewrote most of the driver. Theoretically the rewrite part is done, now to extend it to add bcm59054 support...

## patchset/*

Branches where I prepare patchsets for submission, and try to figure out the order. Very rare, since I tend to jump directly to b4 to prepare patches.

*No such branches at the moment.*

## b4/*

Patches as they are submitted to the mailing list. See the list below for the branches accompanied by relevant lore links:

*No such branches at the moment.*
