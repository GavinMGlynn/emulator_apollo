# Project status

The single source of truth for **what works**. Updated in the same commit as the
code it describes. If this file and the code disagree, the file is the bug.

**Accuracy claim: none yet.** Nothing boots. There is no probe suite and no
golden regression, so no accuracy claim is available to make. This section will
state exactly what backs the claim when there is one.

Last updated: 2026-07-31.

## Subsystems

| Subsystem | Status | Verification |
| --- | --- | --- |
| Build system, presets, CI | working | 4-platform matrix green on first run, plus the `-O0` vs `-O3` output-identity job |
| Model table (`model/`) | working, 9 models | `model_suite`, 13 tests |
| Time base (`time/`) | working | `time_suite`, 13 tests |
| State hash (`state/`) | primitive working | `hash_suite`, 11 tests, incl. published FNV-1a 64 vectors |
| Ring medium interface | not started | — |
| Ring controller | not started | — |
| 68020 / 68030 / 68040 CPU | not started | — |
| 68851 PMMU, 68030/68040 on-chip MMU | not started | — |
| 68881 / 68882 / 68040 FPU | not started | — |
| Memory bus, cache, address translation map | not started | — |
| Two 8259 interrupt controllers | not started | — |
| Two AT DMA controllers | not started | — |
| Interval timer, calendar | not started | — |
| SIO (serial lines, keyboard, mouse) | not started | — |
| Winchester (OMTI, WD7000 ESDI/SCSI) | not started | — |
| Floppy, QIC cartridge tape | not started | — |
| Mono and colour graphics controllers | not started | — |
| 3c505 802.3 Ethernet | not started | — |
| MAME oracle harness | not started | — |
| Probe suite and goldens | not started | — |
| Shared frontend layer (`frontend/common/`) | working | `frontend_common_suite`, 10 tests |
| Headless frontend | `--model`, `--list-models`, `--help` | `headless_lists_models` CTest |
| SDL frontend | not started, deliberately not stubbed | — |

## What is established, and from where

Findings that already have a cited source, so they are not re-derived later.

### Oracle

- MAME's `apollo` driver (`ext/mame/src/mame/apollo/`) covers `dn3000`,
  `dn3500`, `dn5500` and the `dsp*` server variants, and boots Domain/OS. It is
  the runnable oracle: **built and instrumented, never linked.** GPL-2.0-or-later
  against this core's MIT.
- MAME's Domain networking is **802.3 over an emulated 3c505**, not the Apollo
  Token Ring. There is therefore no runnable reference for the ring at all.
- MAME does **not** model DN2500 or DN4500. Those two are paper-only.
- Known MAME limitations, per its own driver notes: some FPU operations and
  operands, Winchester bad-track formatting, and certain video synchronisation.
  Expect to out-accurate the oracle in those areas and record it as a class.

### The probe injection path

- The DN3500 boot PROM holds the **Mnemonic Debugger** (`008778-03` §1.5.1), an
  interactive monitor with `A` (access/deposit), `G` (jump), `D` (dump memory),
  `DR` (dump registers), `SS` (single step) and `B`/`CB` (breakpoints). Full
  command set in `docs/references/MD.md`, from `002398-04` ch. 5.
- This is how probes will be injected, and it removes three obstacles at once:
  no cross toolchain, no need to recover Apollo's on-disk executable format
  first, and no Domain/OS boot — MD runs from PROM before any OS. That is what
  keeps the probe suite a Phase 1 deliverable rather than one gated on Phase 4
  storage work.
- Two commands are worth noting early: `TE` runs the boot PROM's own
  diagnostics — the hardware's test suite for free, the same free-test argument
  as the ring firmware's self-test — and `IC` toggles the instruction cache,
  which is what makes a cache-effect timing probe possible at all.
- **Open:** the command list is confirmed, the per-command *syntax and output
  format* are not. They will be transcribed from a captured MD session under the
  oracle, not guessed.

### Supported platforms and toolchain

- Three platforms, all 64-bit: **Linux x86-64** (Red Hat and Debian derived),
  **Windows x86-64** (clang in an MSVC environment), **macOS arm64**. Clang is
  the default and only supported compiler; all nine presets set it.
- 64-bit is **enforced**, not assumed: `cmake/Platform.cmake` fails the configure
  on a 32-bit target. Time is a `uint64_t` in `AP_TIME_BASE_HZ` units, so a
  32-bit build is a silent-wrong-answer risk rather than merely untested. A
  non-Clang compiler or an unlisted platform warns rather than fails — useful as
  a portability check, but off the supported path.
- *Verification: the configure prints the resolved triple and compiler
  (`apollo: Linux/x86_64, Clang 21.1.8, 64-bit`); the CI matrix builds all three
  platforms plus the `-O0` vs `-O3` identity job.*

### Third-party dependencies

- Six pinned submodules, documented per-submodule in `ext/README.md`. Four are
  linked — `unity` `v2.7.0`, `zlib` `v1.3.2`, `libpng` `v1.6.58`, `sdl`
  `release-3.4.12` — and two, `mame` and `musashi`, are reference-only and never
  enter a build.
- All four linked submodules now sit on **release tags**. They had been added at
  whatever branch tip was current — zlib on `develop`, libpng on the `libpng18`
  development branch, SDL on `main` — which records a stable SHA but names an
  arbitrary mid-development commit upstream never released or tested as a unit.
  A project whose premise is bit-identical output across platforms and build
  types cannot rest on those.
- **Only `ext/unity` is needed to build.** zlib, libpng and SDL are declared so
  the versions are recorded, but no target references them until the media and
  display phases. *Verification: a fresh clone with only `ext/unity` initialised
  configures, builds and passes `ctest`; CI's `CI_SUBMODULES` does the same on
  all four platforms.*
- The MIT/GPL boundary is **asserted at configure time**, not left to review:
  `cmake/GplBoundary.cmake` fails the configure if any first-party source
  includes a `mame` or `musashi` header, or if any target acquires one in its
  link libraries or include directories. *Verification: both halves were probed
  with a deliberate violation and both fail with a named message.*

### Address map (Series 3000/4000, `008778-03` Table 2-8, Table 2-9)

| Range | Device |
| --- | --- |
| `0x000000`–`0x00FFFF` | boot PROM |
| `0x010000` / `0x010100` | CPU status / control register |
| `0x010200` | cache control register |
| `0x010300` | task alias register |
| `0x010400` / `0x010500` | SIO1 / SIO2 |
| `0x010800` / `0x010900` | interval timer / calendar |
| `0x010C00` / `0x010D00` | DMA controller 1 / 2 |
| `0x011000` / `0x011100` | interrupt controller 1 / 2 |
| `0x011200` | network ID PROM |
| `0x011300` | latch-page-on-parity-error register |
| `0x011600` | master request register |
| `0x012000` / `0x014000` | cache RAM / cache condition-code RAM |
| `0x016400` | selective clear locations |
| `0x017000` | address translation map |
| `0x040000`–`0x05FFFF` | AT-compatible bus I/O space |
| `0x051000` (AT `0x220`–`0x23F`) | **Apollo Token Ring controller** |
| `0x058000` (AT `0x300`–`0x310`) | 802.3 Network Controller-AT |
| `0x059000` (AT `0x320`–`0x33F`) | second ring controller |
| `0x04D000` (AT `0x1A0`) | Winchester |
| `0x050000` (AT `0x218`) | tape drive |
| `0x05F800` (AT `0x3F0`) | floppy interface |
| `0x080000`–`0x09FFFF` | AT-compatible bus memory space |
| `0x1000000`+ | main memory |

### Firmware in hand

All from bitsavers `bits/Apollo/firmware/`, gitignored locally under
`roms/firmware/`.

| Image | Size | Notes |
| --- | --- | --- |
| `2500_BOOT_16182_8` | 128 K | only DN2500 artefact we have |
| `3000_BOOT_8475_4`, `_7` | 32 K | two revisions |
| `3500_BOOT_12191_7` | 64 K | reference superset boot PROM |
| `4500_BOOT_13167_02_MD7R.0.32` | 64 K | |
| `5500_BOOT_A1631-80046` | 64 K | |
| `3000_RING_1818-4882` | 8 K | ring firmware, rev 4.0 |
| `3500_RING_10666_6` | 8 K | ring firmware, rev 3.6 |
| `4500_RING_10666_8` | 8 K | |
| `5500_RING_1818-4882_R9` | 8 K | |
| `3000_3C505_010728-00`, `3c505.zip` | 8 K | 802.3 controller |
| `3000_OMTI_8621`, `4500_WD7000_ESDI`, `_SCSI` | 16–32 K | disk controllers |
| `3500_TAPE_80234-003` | 8 K | QIC tape |
| `4500_Matrox_013748_04` | 64 K | DN4500 graphics |
| `3500_NI_1C874` | 32 B | node ID PROM |

Two ring board generations are visible: part `10666` on the 3500/4500 and HP
part `1818-4882` on the 3000/5500. Both identify as
`Apollo Token Ring Network Controller-AT`.

### Model figures confirmed from `[CFG]`

Cited as `[CFG]` = HP-Apollo Products Configuration Guide, Dec 1989.

| Model | Confirmed |
| --- | --- |
| DN2500 | MC68030 @ 20 MHz + MC68882 @ 20 MHz, on-board monochrome graphics, SCSI bus (7 devices), 3 async RS232 ports, 4–16 MB RAM, 15" mono 1024×800 or 19" mono 1280×1024 |
| DN3500 / DN3550 | MC68030 @ 25 MHz + MC68882, 4–32 MB RAM |
| DN4500 / DSP4500 | MC68030 @ 33 MHz + MC68882 @ 33 MHz, 4–32 MB RAM, 7-slot AT/XT bus (6 AT, 1 XT) |
| DN10000 | PRISM @ 18.2 MHz, up to 4 CPUs, dual 64-bit FPUs per CPU, 8–128 MB RAM — recorded only to confirm it is a different machine and out of scope |

### Time base

`AP_TIME_BASE_HZ = 6.6 GHz = LCM(12, 20, 24, 25, 33 MHz)`, giving exact integer
periods: 550 units at 12 MHz, 330 at 20 MHz, 275 at 24 MHz, 264 at 25 MHz, 200
at 33 MHz, and a 12 Mbit/s ring bit cell of 550 units built from two 275-unit
bi-phase windows. This is a *derived* constant — adding a clock it does not
divide means recomputing the LCM, which changes the unit and no emulated
behaviour.

The base was 3.3 GHz until the ring's second clock domain was confirmed. The
Apollo ring PHY is bi-phase encoded, so 12 Mbit/s is the data rate while the
line clock is 24 MHz (`010005-00` §3.2 p.3-3, recorded as findings 10/10a in
`docs/references/RING.md`) — and 3.3 GHz divides 24 MHz only as 137.5. Doubling
the base restored exactness. This is the discipline working as designed rather
than a correction: the constant is derived, `ap_clock_init()` rejects a
frequency the base cannot represent instead of rounding it, and every period is
computed from the base rather than written down, so no emulated behaviour moved.
A video dot clock is the next candidate to force a recomputation.

## Deliberate approximations

None yet. Each one added here carries its reason and cost to close.

## PROVISIONAL figures

Every entry is also a named item in `docs/COMPLETION_PLAN.md`.

| Figure | Current value | Why provisional | Cost to close |
| --- | --- | --- | --- |
| DN2500 RAM base | `0x1000000` | Assumed to match the other 68030 models. The DN2500 is a single integrated board with PC-standard DRAM modules and its own memory design, and no address-space allocation table for Series 2500 has been found | Derive from the 2500 boot PROM, or find a Series 2500 hardware reference; medium |

### Resolved discrepancies

Kept rather than discarded, so a future contradiction has a documented history.

- **DN4500 CPU clock: 33 MHz, not 30 MHz.** `[CFG]`'s Series 4500 Product
  Summary (p. D-108) states "32-bit MC68030 33 MHz CPU with MC68882 33 MHz",
  and its narrative section says "the 33MHz MC68030" — but its own overview
  table at p. A-11 says `MC68030@30MHZ`. 33 MHz taken as correct: two
  independent statements against one, the ordering-level summary outranks the
  marketing summary, and Motorola never binned a 30 MHz 68030 (16/20/25/33/40/50
  only). Both figures divide the time base, so nothing rests on it structurally.
  If a probe ever contradicts 33 MHz, that overview table is the reason to
  revisit.
- **DSP4500 is headless despite its heading.** `[CFG]` heads the section
  "DSP4500 Monochrome Workstation", copied from the DN4500 page. Its country kit
  (`DSPCK-*`) contains only a power cord, where the DN4500's (`DN3CK-*`) includes
  keyboard, keyboard cable and mouse. Modelled headless, like every other DSP.

## Known gaps

- No CPU, so no machine can be constructed yet — the model table describes
  machines that cannot be built.
- The ring controller's register-level interface is not yet recovered; the
  manuals give its address window and block diagram but not its registers.
- No SDL frontend. Deliberately absent rather than stubbed.
