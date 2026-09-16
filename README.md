# apollo

A cycle-accurate emulator for the Apollo Computer Domain workstation family —
and, as far as we can establish, the first emulation of the **Apollo Token
Ring**, so that several emulated nodes can talk to each other over an emulated
12 Mbit/s ring.

[![CI](https://github.com/GavinMGlynn/emulator_apollo/actions/workflows/ci.yml/badge.svg)](https://github.com/GavinMGlynn/emulator_apollo/actions/workflows/ci.yml)

> **Status: 1.0.0.** Domain/OS SR10.2, SR10.3 and SR10.4 boot to a login prompt
> on the 68030 and the 68040; two emulated nodes exchange frames over the token
> ring and see each other's files; SCSI, cartridge tape and floppy round-trip
> real media. `docs/PROJECT_STATUS.md` is the single source of truth and records
> the verification behind every claim here.
>
> **It is not 100% of the hardware, and says where it is not.** Six of the twelve
> models have no runnable oracle and are verified against documents and firmware
> alone; 33 figures and 29 readings are `PROVISIONAL`, each with its reason and
> what would close it, in the register at the end of `PROJECT_STATUS.md`. Nothing
> here claims accuracy it has not earned.

## Why the token ring is the interesting part

MAME's `apollo` driver (Hans Ostermeyer, 2010) already boots Domain/OS, and it
is this project's runnable oracle. But its Domain networking runs over an
emulated **3c505 802.3 Ethernet** card — the proprietary Apollo Token Ring that
Apollo shipped in 1981 and built Domain's distributed single-level store on has
never been emulated by anyone.

It turns out to be tractable, because the primary sources survive:

| Source | What it gives |
| --- | --- |
| `010005-00 Apollo Token Ring MAC and Physical Layer Protocols` (Oct 87) | Bit-level wire spec: free/claimed tokens, frame start and separator characters, packet header, FCS, end-of-frame; PLL, elastic-store buffer and passive bypass at the PHY |
| US patent 4,716,575, *Adaptively synchronized ring network* | The ring's synchronisation design |
| `008778-03 Domain Series 3000/4000 Technical Reference` (Aug 87) | The controller's host interface: AT-bus I/O `0x220`–`0x23F` (Apollo physical `0x051000`; second card at `0x320`/`0x059000`), described as modem + serial/parallel conversion + dual-ported RAM buffer + bypass relays |
| Ring controller firmware, dumped | `3000/3500/4500/5500_RING_*.bin`, 8 KB of 68000 code each, identifying itself as `Apollo Token Ring Network Controller-AT`. Disassembly recovers the register map the manuals stop short of |

So the ring is a §8.9-style **two-oracle** problem: architecture and host
interface come from firmware and manuals, and every timing figure comes from the
paper oracle with a cited page — because no runnable reference for the ring
exists to measure against.

## How several nodes share one ring

All nodes advance under a **single cycle-locked scheduler in one process**, so
the ring is one shared arbitrated medium and token latency and contention are
*emergent* rather than special-cased. That is the only arrangement in which
"cycle-correct token ring" is a checkable claim: whole-ring state hashes are
reproducible, and goldens can cover inter-node timing, not just one node in
isolation.

Nodes run at different clocks — 12, 20, 25 and 33 MHz — while the ring is a
fixed 12 Mbit/s domain. One ring bit cell is 83.33 ns, which is 2.0833… cycles
of a 25 MHz 68030; and because the ring PHY is bi-phase encoded, the ring itself
contributes two clocks, a 12 MHz data clock and a 24 MHz line clock. So no CPU's
cycle is a legal unit of account, and time is counted in units of
**6.6 GHz = LCM(12, 20, 24, 25, 33 MHz)**, in which every period is an exact
integer and nothing drifts. A clock the base cannot represent exactly is
rejected at construction rather than rounded away.

The medium sits behind a narrow interface so a process-separated transport can
be added later as an explicitly non-deterministic mode, without touching node
cores.

## Models

Built DN3500-first as the reference superset, then subset across the range —
every model expressed as a row in one table rather than scattered conditionals.

| Model | CPU | MMU | Display | Oracle |
| --- | --- | --- | --- | --- |
| DN2500 | 68030 @ 20 MHz | on-chip | mono 1024×800 | paper only |
| DN3000 | 68020 @ 12 MHz | external 68851 | mono 1024×800 | MAME |
| **DN3500** | 68030 @ 25 MHz | on-chip | mono 1024×800 | MAME (reference superset) |
| DN3550 | 68030 @ 25 MHz | on-chip | mono 1280×1024 | paper only |
| DN4000 | 68020 @ 25 MHz | external 68851 | colour 1280×1024 | paper only |
| DN4500 | 68030 @ 33 MHz | on-chip | mono 1280×1024 | paper only |
| DN5500 | 68040 @ 25 MHz | on-chip 68040 | mono 1024×800 | MAME |
| DSP3000 / DSP3500 / DSP5500 | as the DN sibling | as the DN sibling | headless | MAME |
| DSP3550 / DSP4500 | as the DN sibling | on-chip | headless | paper only |

Twelve models, all from one table in `src/core/model/`. **Six have a runnable
oracle and six do not** — the paper-only rows are checked against manuals and
their own firmware, which is a weaker claim and is made as one.

The DSP servers are the same boards without display or keyboard, which makes
them the cheap node type to run many of on an emulated ring.

`apollo-headless --list-models` prints this table from the code, including which
figures are still `PROVISIONAL`. The Apollo PRISM-based DN10000 is explicitly
out of scope: a different architecture, with no ROM dumps and no oracle.

## Verification methodology

This is the part that makes "cycle-accurate" checkable rather than hoped-for,
and it is built before the subsystems it checks.

1. **Reference-first.** Behaviour is resolved from the manuals or the oracle,
   never by trial-and-error on our own code.
2. **MAME as a runnable oracle**, built and instrumented — never linked. It is
   GPL-2.0-or-later; this core is MIT and stays clean of it.
3. **Bare-metal probes**: hand-assembled 68000 programs that make the machine
   measure itself, run identically under this emulator and the oracle, with the
   result block diffed.
4. **Paper oracle for the ring**, every figure citing its manual page, and
   anything not yet cited marked `PROVISIONAL` in code *and* status doc.
5. **Goldens in CTest** on every CI platform. Emulated cycle counts are not
   wall-clock measurements, so goldens are bit-identical across hosts and build
   types — CI asserts that `-O0` and `-O3` agree.
6. **Findings recorded** with their evidence; nothing is "fixed" on reasoning
   alone.

## Build

Requires CMake ≥ 3.21, Ninja and Clang (C23).

### Supported platforms

Three, all **64-bit**, with **Clang** the default and only supported compiler on
each:

| Platform | Architecture | Notes |
| --- | --- | --- |
| Linux | x86-64 | Both Red Hat and Debian derived distributions; CI runs `ubuntu-latest` and a `rockylinux:9` container |
| Windows | x86-64 | Clang inside an MSVC environment |
| macOS | arm64 | Apple silicon |

One toolchain everywhere is deliberate. The project's central claim is that a
given workload produces byte-identical results on every platform and build type;
holding the compiler fixed means that claim is about the emulator rather than
about three compilers happening to agree. Another compiler is a useful
portability check and will build with a warning, but it is off the supported
path.

64-bit is enforced at configure time rather than assumed. Time is a `uint64_t`
counter in `AP_TIME_BASE_HZ` units, and a 32-bit target is a silent-wrong-answer
risk rather than merely an untested one, so `cmake/Platform.cmake` fails the
configure outright.

```sh
git clone https://github.com/GavinMGlynn/emulator_apollo.git
cd emulator_apollo
git submodule update --init --depth 1 ext/unity   # the only one the build needs
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
./build/linux-debug/src/frontend/headless/apollo-headless --list-models
```

Substitute `macos-*` or `windows-*` for the platform presets; every configure
preset has a matching build and test preset. **Do not clone `--recursive`**
unless you want the oracle: `ext/mame` is a very large checkout and nothing in
the build needs it. [`docs/REQUIREMENTS.md`](docs/REQUIREMENTS.md) says what each
submodule is for.

Timing and performance are measured on release builds only; the CI and debug
builds are `-O0`.

## What a clone does not bring with it

**[`docs/REQUIREMENTS.md`](docs/REQUIREMENTS.md) is the complete manifest** —
every piece of software, firmware, media and documentation needed, what each is
for, and where to get it.

The short version: a clone plus `ext/unity` builds and passes all 153 tests with
**no firmware at all**. To boot a machine you supply your own dumps, because
Apollo firmware and Domain/OS media are not ours to redistribute and are
gitignored:

- `roms/firmware/` — boot and option ROMs. `3500_BOOT_12191_7.bin` is the one
  every reference figure here was measured with. Bitsavers, `bits/Apollo/firmware/`.
- `media/` — Domain/OS install tapes and disk images. SR10.2, SR10.3 and SR10.4
  are the set that exists.
- `docs/references/` — ~130 vendor manuals the source cites by page. The
  `*_WALK.md` records of what each one yielded **are** committed, so the findings
  are readable without the manuals.

`REQUIREMENTS.md` lists every ROM with its size and SHA-256 prefix, so you can
tell a good dump from a bad one.

## Licence

MIT, with `ext/` submodules and the vendored hardware manuals under
`docs/references/` keeping their own terms. See [LICENSE](LICENSE) — in
particular, MAME is used as a reference oracle only and is never linked into
this core.

## Documents

- [`docs/REQUIREMENTS.md`](docs/REQUIREMENTS.md) — **everything needed to build and run**, with sources
- [`docs/PROJECT_STATUS.md`](docs/PROJECT_STATUS.md) — what works, with its verification, and the `PROVISIONAL` register
- [`docs/COMPLETION_PLAN.md`](docs/COMPLETION_PLAN.md) — the route taken, item by item
- [`docs/references/RING.md`](docs/references/RING.md) — Apollo Token Ring findings, each cited
- [`tools/mame-oracle/FINDINGS.md`](tools/mame-oracle/FINDINGS.md) — every divergence against the oracle and how it was settled
- [`CLAUDE.md`](CLAUDE.md) — the working conventions the project is built under
