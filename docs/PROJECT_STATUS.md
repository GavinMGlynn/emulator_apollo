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
| Build system, presets, CI | working | 4-platform matrix green; `-O0` vs `-O3` output diffed in CI |
| Model table (`model/`) | working, 8 models | `model_suite`, 13 tests |
| Time base (`time/`) | working | `time_suite`, 13 tests |
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
| Headless frontend | `--list-models` only | `headless_lists_models` CTest |
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

### Time base

`AP_TIME_BASE_HZ = 3.3 GHz = LCM(12, 20, 25, 33 MHz)`, giving exact integer
periods: 275 units at 12 MHz, 165 at 20 MHz, 132 at 25 MHz, 100 at 33 MHz, and
275 units for a 12 Mbit/s ring bit. This is a *derived* constant — adding a
clock it does not divide (a video dot clock, most likely) means recomputing the
LCM, which changes the unit and no emulated behaviour.

## Deliberate approximations

None yet. Each one added here carries its reason and cost to close.

## PROVISIONAL figures

Every entry is also a named item in `docs/COMPLETION_PLAN.md`.

| Figure | Current value | Why provisional | Cost to close |
| --- | --- | --- | --- |
| DN4500 CPU clock | 33 MHz | Not confirmed from a cited page; DN4500 is not in MAME | Read `HP-Apollo Products Configuration Guide` / `Quick-Reference Configuration Guide`; low |
| DN2500 clock, RAM base and size, display | 20 MHz, 16 MB @ `0x1000000`, mono | DN2500 is in neither MAME nor any manual we have yet; only its boot ROM | Find a DN2500 hardware reference, or derive the map from its boot PROM; medium |

## Known gaps

- No CPU, so no machine can be constructed yet — the model table describes
  machines that cannot be built.
- The ring controller's register-level interface is not yet recovered; the
  manuals give its address window and block diagram but not its registers.
- No SDL frontend. Deliberately absent rather than stubbed.
