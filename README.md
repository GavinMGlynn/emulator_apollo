# apollo

A cycle-accurate emulator for the Apollo Computer Domain workstation family —
and, as far as we can establish, the first emulation of the **Apollo Token
Ring**, so that several emulated nodes can talk to each other over an emulated
12 Mbit/s ring.

[![CI](https://github.com/GavinMGlynn/emulator_apollo/actions/workflows/ci.yml/badge.svg)](https://github.com/GavinMGlynn/emulator_apollo/actions/workflows/ci.yml)

> **Status: early.** The build system, model table and time base exist and are
> tested. No machine boots yet. The table below is the honest account, and
> `docs/PROJECT_STATUS.md` is the single source of truth for it. Nothing here
> claims accuracy it has not earned.

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
| DN4500 | 68030 @ 33 MHz | on-chip | mono 1280×1024 | paper only |
| DN5500 | 68040 @ 25 MHz | on-chip 68040 | mono 1024×800 | MAME |
| DSP3000 / DSP3500 / DSP5500 | as the DN sibling | as the DN sibling | headless | MAME |
| DSP4500 | as DN4500 | on-chip | headless | paper only |

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
git clone --recursive https://github.com/GavinMGlynn/emulator_apollo.git
cd emulator_apollo
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
./build/linux-debug/src/frontend/headless/apollo-headless --list-models
```

Substitute `macos-*` or `windows-*` for the platform presets; every configure
preset has a matching build and test preset. Only `ext/unity` is needed to build
and test; `ext/mame` is the oracle and is not required.

```sh
git submodule update --init --depth 1 ext/unity   # enough for build + tests
```

Timing and performance are measured on release builds only; the CI and debug
builds are `-O0`.

## ROMs and media

Apollo boot and ring firmware and Domain/OS media are **not** distributed here
and are gitignored. Put your own dumps in `roms/firmware/` and `media/`.
Firmware images are on bitsavers under `bits/Apollo/firmware/`, and Domain/OS
SR10.3/SR10.4 install tapes under `bits/Apollo/`.

## Licence

MIT, with `ext/` submodules and the vendored hardware manuals under
`docs/references/` keeping their own terms. See [LICENSE](LICENSE) — in
particular, MAME is used as a reference oracle only and is never linked into
this core.

## Documents

- [`docs/PROJECT_STATUS.md`](docs/PROJECT_STATUS.md) — what works, with its verification
- [`docs/COMPLETION_PLAN.md`](docs/COMPLETION_PLAN.md) — phased road to done
- [`docs/references/RING.md`](docs/references/RING.md) — Apollo Token Ring findings
- [`CLAUDE.md`](CLAUDE.md) — working conventions
