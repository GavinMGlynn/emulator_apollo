# What you need to build and run this

Everything a clone does **not** bring with it, and where each part comes from.
The repository holds source, tests and its own research records; it holds no
firmware, no operating system media and no vendor manuals, because none of those
are ours to redistribute.

Three tiers, and only the first is needed to build and pass the test suite:

| Tier | Needed for | Redistributable |
| --- | --- | --- |
| 1. Toolchain and `ext/unity` | building, `ctest` 153/153 | yes — fetched by the commands below |
| 2. Firmware and Domain/OS media | booting a machine | **no** — you supply your own dumps |
| 3. Vendor manuals | reading the citations in the source | **no** — free to download, not to mirror |

A clone with tier 1 alone is a working, fully tested build. Tier 2 is what makes
it a *computer*.

---

## 1. Toolchain

| Software | Version | Why |
| --- | --- | --- |
| CMake | ≥ 3.21 | presets are used throughout |
| Ninja | any | the generator every preset names |
| Clang | C23 support (16+) | the one supported compiler — see below |
| Python | ≥ 3.9 | `tools/` — probes, the doc checkers, the oracle driver, `awd_read.py` |
| Git | any | submodules |

**Clang is the only supported compiler on all three platforms** (Linux x86-64,
Windows x86-64 under an MSVC environment, macOS arm64), and that is deliberate:
this project's central claim is that a workload produces byte-identical results
on every platform and build type, and holding the compiler fixed means the claim
is about the emulator rather than about three compilers happening to agree.
Another compiler will build with a warning and is off the supported path.

64-bit is enforced at configure time, not assumed — `cmake/Platform.cmake` fails
outright on a 32-bit target, because time is a `uint64_t` counter in
`AP_TIME_BASE_HZ` units and a 32-bit host is a silent-wrong-answer risk.

```sh
git clone https://github.com/GavinMGlynn/emulator_apollo.git
cd emulator_apollo
git submodule update --init --depth 1 ext/unity   # the only one needed to build
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug                        # 153/153, no firmware required
```

Substitute `macos-*` or `windows-*`; every configure preset has a matching build
and test preset. **Do not clone `--recursive`** unless you want the oracle:
`ext/mame` is a very large checkout and nothing in the build needs it.

### Submodules, and which you actually want

| Submodule | Needed | What for |
| --- | --- | --- |
| `ext/unity` | **yes** | the test framework; `ctest` needs nothing else |
| `ext/mame` | only to verify against the oracle | MAME, built and instrumented as a runnable reference. **GPL-2.0-or-later — never linked into `src/core`, never copied from.** See `tools/mame-oracle/` |
| `ext/musashi` | no | read as a reference 68000 core; not built |
| `ext/zlib`, `ext/libpng` | no | reserved for frontends that write images |
| `ext/sdl` | no | the interactive frontend is deliberately not started |

---

## 2. Firmware and media — you supply these

`roms/` and `media/` are gitignored and stay that way. Put your own dumps at the
paths below.

### Boot and option ROMs — `roms/firmware/`

Apollo firmware is on **bitsavers**, under `bits/Apollo/firmware/`. Sizes and the
first 16 hex digits of the SHA-256 are given so you can tell a good dump from a
bad one; they are facts about a file, not a copy of it.

| File | Bytes | SHA-256 (first 16) | What it is |
| --- | --- | --- | --- |
| `3500_BOOT_12191_7.bin` | 65536 | `7f1028f990027eea` | **DN3500 boot PROM — the reference machine.** Everything in `docs/PROJECT_STATUS.md` that says "identity boot" uses this |
| `3000_BOOT_8475_7.bin` | 32768 | `44d302d19a18d5f4` | DN3000 boot PROM, rev 7 |
| `3000_BOOT_8475_4.bin` | 32768 | `c14a15b4934be313` | DN3000 boot PROM, rev 4 |
| `2500_BOOT_16182_8.bin` | 131072 | `ff742cc387eba003` | DN2500 boot PROM — the only source for that model's memory map |
| `4500_BOOT_13167_02_MD7R.0.32.bin` | 65536 | `3bfb475f8dde864a` | DN4500 boot PROM |
| `5500_BOOT_A1631-80046_1-30-92.bin` | 65536 | `1c8a35482d91703a` | DN5500 (68040) boot PROM |
| `3500_RING_10666_6.bin` | 8192 | `a1abef0f17bba11a` | **Apollo Token Ring controller firmware**, AT generation. Its self-test is the controller's own test suite |
| `4500_RING_10666_8.bin` | 8192 | `d0029e2cdcf613b7` | Ring controller, later revision |
| `3000_RING_1818-4882_9-4-90.bin` | 8192 | `54b8cd2cc9d1b604` | Ring controller, DN3000 generation |
| `5500_RING_1818-4882_R9_12-10-90.bin` | 8192 | `54b8cd2cc9d1b604` | Same image as the DN3000's — identical dump, kept under both names |
| `3000_OMTI_8621_102640-B.bin` | 16384 | `6ceed91198ee7113` | OMTI 8621 disk controller |
| `3500_TAPE_80234-003.bin` | 8192 | `b1a8b8011cba09ef` | SC-499 cartridge tape controller |
| `3000_3C505_010728-00.bin` | 8192 | `9983cc0274a4b197` | 3Com 3C505 EtherLink Plus |
| `4500_WD7000_SCSI_62-00211-037_1990.bin` | 16384 | `3a96b3230a278bac` | WD7000-ASC SCSI adapter |
| `4500_WD7000_ESDI_62-00210-032_1990.bin` | 32768 | `b1d714e306dad8fe` | WD7000 ESDI adapter |
| `4500_Matrox_013748_04.bin` | 65536 | `bc9689870fbbd998` | Matrox graphics option |
| `3500_NI_1C874.bin` | 32 | `36b262d0757be4dd` | Node-ID PROM contents — 32 bytes, and the machine's identity |

**The minimum to boot anything is one boot PROM.** `3500_BOOT_12191_7.bin` is
the one every reference figure in this repository was measured with.

### Domain/OS media — `media/`

Install tapes are on bitsavers under `bits/Apollo/`. **SR10.2, SR10.3 and SR10.4
are the set that exists**; SR9.7 and SR10.1 install media are not online
anywhere, which `docs/PROJECT_STATUS.md` records as searched rather than merely
unfound.

| Path | What | How to get it |
| --- | --- | --- |
| `media/dn3500-sr10.4-installed.awd` | An **installed, bootable** SR10.4 disk — what `tools/identity-boot.sh` runs | Built by installing, not downloaded. `docs/PROJECT_STATUS.md` has the route end to end |
| `media/domainos/019593-*.ct`, `019594-*.ct` | SR10.4 install cartridges | bitsavers |
| `media/dn3500.awd` etc. | Working disk images at various stages | Produced by the install; `truncate -s 348M` makes a blank one |

A `.awd` is a raw Apollo Winchester image and a `.ct` a cartridge-tape image;
`tools/awd_read.py` reads a volume offline without booting anything.

---

## 3. Vendor manuals — `docs/references/`

Every timing figure, register layout and bit definition in `src/core` cites one
of these by document number and page. The `*_WALK.md` files in
`docs/references/` **are** committed: they record page by page what each manual
yielded, so you can read the findings without the manual. The PDFs themselves
are gitignored — vendor copyright, free to download, not ours to mirror.

Put them in the directories below and the citations line up.

| Directory | Documents | Source |
| --- | --- | --- |
| `docs/references/bitsavers/` | ~62 Apollo manuals — the Engineering Handbooks (`002398-01/03/04`), the Series 3000/4000 Technical Reference (`008778-03`), the DN5500 addendum (`019411-A00`), GPIO driver guides, the SR10 documentation set | bitsavers `pdf/apollo/` |
| `docs/references/bitsavers/patents/` | 10 Apollo patents, incl. **4,716,575 "Adaptively synchronized ring network"** — a primary source for the token ring | Google Patents / USPTO |
| `docs/references/bitsavers/release_notes/` | SR9.7 – SR10.4 release notes and the 10.4 addendum | bitsavers |
| `docs/references/motorola/` | MC68020/68030/68040 user's manuals, the 68030 electrical spec (`MC68030EC`), the 68040 designer's handbook, `M68000 Family Programmer's Reference 1992`, MC146818A | bitsavers `pdf/motorola/` |
| `docs/references/intel/` | 8237A DMA, 8259A interrupt controller, the 1983 peripherals handbook (for the 8254) | bitsavers `pdf/intel/` |
| `docs/references/omti/` | OMTI 8000-series and 8640 controller references | bitsavers |
| `docs/references/archive/` | SC-499 tape controller guide, **QIC-02 Rev D**, Apollo's QIC-36 spec (`08845`) | bitsavers / QIC archive |
| `docs/references/exabyte/` | EXB-8200 product spec, user, theory and maintenance manuals | bitsavers |
| `docs/references/westernDigital/` | WD7000-ASC engineering spec | bitsavers |
| `docs/references/3com/` | 3C505 EtherLink Plus developer's guide and technical reference | bitsavers |
| `docs/references/nec/` | µPD765 floppy controller datasheets | bitsavers |
| `docs/references/signetics/` | SCN2681 DUART | bitsavers |
| `docs/references/brooktree/` | Bt458 RAMDAC databook | bitsavers |

**The one document that does not exist**: `007861-A01`, the parent handbook the
DN5500 addendum refers to. It is on no archive, and its absence is why the I/O
protection map's entry layout and the board cache's condition-code format are
`PROVISIONAL`. `docs/PROJECT_STATUS.md` records the search rather than leaving
the gap unexplained.

**The token ring's own sources**, because they are the ones people ask for:
`010005-00 Apollo Token Ring MAC and Physical Layer Protocols` (Oct 87) on
bitsavers, patent 4,716,575, `008778-03` for the host interface, and the ring
controller firmware above. `docs/references/RING.md` is the findings record.

---

## Where to look next

| Document | What it answers |
| --- | --- |
| [`docs/PROJECT_STATUS.md`](PROJECT_STATUS.md) | What works, with the verification for each claim, and the `PROVISIONAL` register of every figure that was chosen rather than transcribed |
| [`docs/COMPLETION_PLAN.md`](COMPLETION_PLAN.md) | The route that was taken, item by item |
| [`docs/references/RING.md`](references/RING.md) | Apollo Token Ring findings, each with its citation |
| [`tools/mame-oracle/FINDINGS.md`](../tools/mame-oracle/FINDINGS.md) | Every divergence found against the oracle and how it was settled |
| [`CLAUDE.md`](../CLAUDE.md) | The working conventions the whole project is built under |
