# The Domain/OS disk image, pinned rather than committed

The image itself is **not** in this repository and will not be. We
formatted the volume; its contents are Apollo's operating system,
restored verbatim from HP/Apollo installation cartridges, and arranging
vendor binaries onto a disk we made does not change who owns them.
`CLAUDE.md` says the same as a standing rule, and this repository is
public.

This file is what can be published: enough to identify the image
exactly, so that a rebuild can be checked against it, and so that anyone
holding the same media can confirm their cartridges match ours before
wondering why their result does not.

## The image

Stage: **Domain/OS SR10.4, 'large' template, installed by MINST from all four cartridges and cleanly shut down**

| Field | Value |
| --- | --- |
| File | `media/dn3500-sr10.4-installed.awd` (gitignored) |
| Size | 364904448 bytes |
| SHA-256 | `35cb5185853a5295fcbee42bf7195f6b361d0f643fcd7b0fc8f34141a7a20b9c` |

## What produced it

| Field | Value |
| --- | --- |
| Oracle | MAME pinned at `309427678b6939b354ad3bdb5c40b4aa648a18f4`, built `SUBTARGET=apollo`, with `APOLLO_XXL` enabled |
| Machine | `dn3500` |
| Driver | `tools/mame-oracle/mdsession.py` at commit `5de6767` |
| Procedure | `FINDINGS.md` C47-C54, and the plan's first-boot item |

**A rebuild is not expected to be bit-identical.** The install is a
conversation paced by the host, the volume carries timestamps, and the
RTC is not under our control (C53). The hash identifies *this* image; it
is not a reproducibility claim, and nothing timed may be measured through
an image built this way.

## The source media

Also gitignored. Hashed so that a mismatch is found here rather than
three hours into an install.

| Cartridge | Size | SHA-256 |
| --- | --- | --- |
| `019593-001.CRTG_STD_SFW_BOOT_1-REV.A.ct` | 53678592 | `9513c5c3654f132d54cb45e02abf76d0a1eda0160ef215ffe39a82cb77dd3ddf` |
| `019594-001.CRTG_STD_SFW_1.ct` | 58403328 | `caf3c4a56a6858a2dbf306e322e1d5de64736db84074c714bb6ebe0bb3036a0b` |
| `019594-002.CRTG_STD_SFW_2.ct` | 61564928 | `3459607bbf1844c2016e4703ee50ba958b97899a78f29e4bf7891184019da4b2` |
| `019594-003.CRTG_STD_SFW_3.ct` | 56568320 | `b8cce599c9c5173252eeddabe2478ff2a7d5f770d92402d352986ee5c3a571e3` |
| `019594-004.CRTG_STD_SFW_4.ct` | 57844224 | `04a2847057162057de61e1ea75d12f6e48820dafdc82692a304268024bfb3435` |

