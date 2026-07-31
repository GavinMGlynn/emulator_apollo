# Project status

The single source of truth for **what works**. Updated in the same commit as the
code it describes. If this file and the code disagree, the file is the bug.

**Accuracy claim: none yet.** Nothing boots, but a program now *runs*: `ap_m68030_step` fetches through the pipe and instruction cache, decodes, executes a named subset (`NOP`, `MOVEQ`, 8-bit `BRA`/`Bcc`) and advances the PC, with clocks accounted. An instruction outside that subset reports `UNIMPLEMENTED` rather than silently succeeding, so "how far a program got" is a real measure. The 68030's bus cycle state machine
exists, but there is no instruction execution, no memory system and no device, so
no machine can be constructed and no accuracy claim is available to make. The golden regression harness now exists, but it pins reports about the
model table, not emulated behaviour. This section will state exactly what backs
the claim when there is one.

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
| 68030 instruction pipe + cache holding register | working | `pipe_suite`, 14 tests, `MC68030 User's Manual 3ed` §11.2.2 |
| 68030 bus cycle state machine | working, including burst line fills | `bus_suite`, 23 tests, each citing `MC68030 User's Manual 3ed` ch. 7 (read, write and burst cycles) |
| 68030 on-chip instruction and data caches | working, including the bus-timing join: a hit costs 0 clocks, a burst line fill 5 | `cache_suite`, 29 tests and `bus_suite`, 23 tests, `MC68030 User's Manual 3ed` §6, §7.3.7 |
| 68030 operand access (read/write through an effective address) | working | `operand_suite`, 8 tests, `M68000 Family Programmer's Reference Manual 1992` |
| 68030 instruction step (fetch → decode → execute → advance) | working for `NOP`, `MOVEQ`, 8-bit `BRA`/`Bcc`; everything else reports unimplemented | `step_suite`, 10 tests |
| 68030 instruction prefetch (pipe driven from memory) | working | `fetch_suite`, 5 tests, `MC68030 User's Manual 3ed` §11.2.2 and §6.1 |
| 68030 logical memory access path (cache → MMU → bus) | working, reads and writes | `access_suite`, 12 tests, `MC68030 User's Manual 3ed` §6.1 |
| 68030 effective address calculation (with register side effects) | working; memory-indirect modes report the pending indirection | `addr_suite`, 13 tests, `M68000 Family Programmer's Reference Manual 1992` §2.2 |
| 68030 instruction decode dispatcher (+ MOVEQ, total length) | working — 89.9% of the 16-bit space classified, and every claimed instruction sized | `decode_suite`, 17 tests including two full 65536-word sweeps |
| 68030 family 1111 (coprocessor interface, MMU instruction dispatch) | decode working — the opcode map is now complete | `coproc_suite`, 6 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 and `MC68030 User's Manual 3ed` §9.7.6 |
| 68030 family 1110 (shift/rotate/bit field) | decode working | `shift_suite`, 10 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 |
| 68030 arithmetic/logic families 1000, 1001, 1011, 1100, 1101 | decode working | `arith_suite`, 9 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 |
| 68030 family 0000 (immediate, bit manipulation, MOVEP) | decode working; CMP2/CHK2/CAS/CAS2 not yet covered | `immediate_suite`, 10 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 |
| 68030 MOVE / MOVEA (families 0001, 0010, 0011) | decode working | `move_suite`, 8 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 |
| 68030 family 0100 single-operand group (NEGX/CLR/NEG/NOT/TST/TAS, MOVE to-from SR-CCR, ILLEGAL) | working — family 0100 now complete | `single_suite`, 7 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 |
| 68030 family 0100 LEA/CHK/`$48`/`$4C` subtree (LEA, CHK, PEA, SWAP, BKPT, EXT, EXTB, NBCD, MOVEM) | working | `misc_suite`, 11 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 |
| 68030 family 0100 `$4E` control group (TRAP/LINK/UNLK/MOVE USP/RESET/NOP/STOP/RTE/RTD/RTS/TRAPV/RTR/JSR/JMP) | working; the rest of family 0100 not yet decoded | `control_suite`, 10 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 |
| 68030 family 0101 (ADDQ/SUBQ/Scc/DBcc/TRAPcc) decode | working | `quick_suite`, 10 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 and each instruction page |
| 68030 branch family (Bcc/BSR/BRA) decode | working | `branch_suite`, 8 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 and the Bcc/BRA/BSR pages |
| 68030 operation code map (top-level instruction family) | working | `opcode_suite`, 6 tests, `M68000 Family Programmer's Reference Manual 1992` Table 8-2 |
| 68030 conditional tests (the 16 Bcc/Scc/DBcc/TRAPcc conditions) | working | `cond_suite`, 9 tests, `M68000 Family Programmer's Reference Manual 1992` Table 3-19 |
| 68030 effective address decode (modes, extension words, lengths) | decode and extension-word counts working; address *calculation* needs the instruction unit | `ea_suite`, 17 tests, `M68000 Family Programmer's Reference Manual 1992` §2, Tables 2-1, 2-2, 2-4 |
| 68030 programming model (registers, SR, three stack pointers) | working | `regs_suite`, 10 tests, `MC68030 User's Manual 3ed` §1.3 and `M68000 Family Programmer's Reference Manual 1992` §1.3.2 |
| 68030 exception vectors, priority and stack frames | working; taking an exception needs the instruction unit | `exception_suite`, 14 tests, `MC68030 User's Manual 3ed` §8, Tables 8-1, 8-5, 8-6 |
| 68020 / 68030 / 68040 CPU | not started beyond the bus | — |
| 68030 ATC (22-entry, fully associative) | working, replacement `PROVISIONAL` | `atc_suite`, 17 tests, `MC68030 User's Manual 3ed` §9.4 |
| 68030 descriptors + search protection state | working | `desc_suite`, 23 tests, `MC68030 User's Manual 3ed` §9.5.1.1 |
| 68030 translation control (TC) + address split | working | `tc_suite`, 15 tests, `MC68030 User's Manual 3ed` §9.7.2 |
| 68030 transparent translation (TT0/TT1) | working, bit layout now transcribed | `tt_suite`, 21 tests, `MC68030 User's Manual 3ed` §9.3, §9.7.3; layout from `M68000 Family Programmer's Reference Manual 1992` Figure 1-9 |
| 68030 MMU status register (`MMUSR`) | working, both PTEST forms, bit layout transcribed | `mmusr_suite`, 16 tests, `MC68030 User's Manual 3ed` Table 9-3; layout from `M68000 Family Programmer's Reference Manual 1992` PTEST p. 6-64 |
| 68030 translation table search (the walk) | working: search, U/M writeback, and ATC fill | `walk_suite`, 40 tests, `MC68030 User's Manual 3ed` §9.2, §9.4, §9.5, §11; writeback cost cross-checked against `MC68851 PMMU User's Manual 3ed` §5.1.5.3.11 |
| 68851 PMMU, 68030/68040 MMU tables + ATC | not started | — |
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
| MAME oracle harness | driver and dumper working; oracle binary now built (MAME v0.289), no probe campaign run against it yet | `oracle_driver` (19 checks, stub MAME) and `oracle_dump_format` (19 checks, mock machine); `./apollo -listfull` lists all eleven apollo machines |
| Golden regression harness | working | `golden_model_table`, run under every build preset; drift, `-O3` identity and regeneration all verified |
| Probe suite | not started | — |
| Shared frontend layer (`frontend/common/`) | working | `frontend_common_suite`, 10 tests |
| Headless frontend | `--model`, `--list-models`, `--help` | `golden_model_table`, which supersedes the old smoke test |
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
- **Input syntax: closed, and from the manual rather than the oracle.** This was
  recorded as open on the grounds that the syntax "is not in the handbook's
  command list" — true, but the handbook continues *past* the list into a
  per-command reference (`002398-04` pp. 5-7 on) and then states the grammar
  formally at pp. 5-13/5-14. Now transcribed in `docs/references/MD.md`: the
  full BNF, hexadecimal-by-default input, `<size_spec>`/`<base_spec>` placement,
  `*` as current location, and the `AR` control-register names
  (`TC`/`RP`/`DFC`/`SFC`/`CACR`/`CAAR`) that are the Phase 2 MMU and cache probe
  surface. The scan's OCR mangles `|` and `::=`; that is called out in the file,
  and the reconstruction rests on the handbook expanding every token in prose
  directly below the grammar, not on inference.
- **Still open: the output format.** The handbook never shows a literal output
  line, so column layout, separators, prompt and terminator are unknown — and
  the harness has to parse exactly those bytes. That still wants a captured
  session under the oracle. The no-guessing rule governs the parser; it no
  longer blocks the encoder's input side.

### 68030 instruction timing, and why the tables are a check and not a recipe

Recorded in full in `docs/references/M68030_TIMING.md`, from `MC68030 User's
Manual` 3ed ch. 11. The load-bearing fact:

**No published average-no-cache-case number is a value any single execution of
that instruction ever takes.** Motorola computed the odd-word-aligned and
even-word-aligned cases and published *the mean, rounded up* (§11.3.3, p. 11-8),
and the same for prefetch bus-cycle counts. The cache-case figures separately
assume no overlap, no data-cache hits, and two-clock bus cycles throughout.

So an emulator that looks up an instruction's published cycle count and adds it
is not cycle-accurate and cannot be made so by refining the table — it is
reproducing an average the hardware never exhibits on any particular run. This
is the concrete justification for the strictly cycle-stepped reference core:
alignment, cache state, wait states and contention all fall out of the machine
rather than being tabulated, and ch. 11 then serves as an independent check on
numbers we produce.

Nothing from ch. 11 is in code. This is reference only.

### Golden result blocks

- A *result block* is any deterministic report the emulator prints. Each is
  pinned by a committed file under `tests/goldens/` and checked by
  `tools/regress.py`; `cmake/Goldens.cmake` registers each as an ordinary CTest
  entry labelled `golden`, so it runs under every build preset on every
  platform.
- **Why a committed golden and not a diff of two builds.** Comparing `-O0`
  output with `-O3` output proves only that the two agree, which is also true
  when both have drifted together. Checking each against a reviewed file catches
  that, and catches one platform quietly differing from the other three — which
  is the property this project actually claims. The CI identity job's hand-rolled
  `diff` was removed in favour of this.
- Goldens are regenerated by the `goldens-update` build target, never edited by
  hand, so a golden cannot be quietly adjusted into agreement with a change. A
  mismatch prints a unified diff and says to commit the regenerated golden
  *alongside* the change that caused it, because a golden updated in its own
  commit hides what moved.
- Absent Python 3 is a **fatal configure**, not a skipped test; `-DAPOLLO_GOLDENS=OFF`
  is the explicit opt-out. A green `ctest` that pinned nothing is worse than a
  failed configure.
- *Verification: verified four ways — a perturbed golden fails with a named
  diff, restoring it passes, the release build matches the same golden as the
  `-O0` build, and `goldens-update` regenerates byte-identically.*
- Only one block exists today (`model_table.txt`, from `--list-models`), because
  only the model table produces one. Probe results, register dumps and boot
  state hashes join it as those subsystems land.

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

### Media in hand — installation tapes, and no bootable disk image

This is the constraint that shapes Phase 1's first milestone, so it is recorded
before the work rather than discovered inside it.

| Item | What it is |
| --- | --- |
| `Apollo_DOMAINOS_SR10.3.5.tgz` | 5 tapes, 176 numbered `.img` files |
| `019593-001.CRTG_STD_SFW_BOOT_1-REV.A.ct.gz` | bootable QIC install cartridge |
| `019594-001..004.CRTG_STD_SFW_{1..4}.ct.gz` | QIC install cartridges 1–4 |
| `cptape.hlp` | Apollo `cptape` help text, rev 9.0, 1986-12-17 |

The `.img` files are **tape files, not disk images** — one file per tape mark,
the layout `cptape` writes. `tape1/00.img` is 8192 bytes beginning
`00 13 d8 00 … "SYSBOOT REV" … " M68K    "` followed by 68000 code: the Apollo
tape boot record. `tape1/02.img` at 50 MB is the install payload.

**So we hold no pre-installed, bootable Domain/OS disk image — only the media to
create one.** Phase 1's first item is written as "MAME boots Domain/OS from an
SR10.x image to a login prompt", which assumes an image that does not exist.
Reaching a login prompt actually requires installing Domain/OS from tape onto a
blank disk image under the oracle first, which is a substantially larger task
than booting one. Recorded as a tail in `docs/COMPLETION_PLAN.md` rather than
absorbed silently into the item.

It also pulls `.ct` cartridge support forward in importance: Phase 4 lists it as
a storage item, but the install path makes it the format the first boot depends
on.

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

### A manual figure that did not survive its scan

`MC68030 User's Manual` 3ed Figure 9-37 gives the bit layout of the transparent
translation registers. Its lower half — the positions of E, CI, R/W, RWM,
FC BASE and FC MASK — OCRs to nothing but a stray "FC MASK", identically under
`pdftotext -layout` and plain extraction, so the loss is in the scan rather than
the extraction. The upper half is legible (31-24 logical address base, 23-16
logical address mask), and every field's *meaning* is given in prose.

`src/core/cpu/m68030/ap_m68030_tt.c` therefore models the register as **decoded
fields** and implements the documented semantics, leaving the packing undone
rather than inferred from a plausible-looking layout. Nothing needs the packing
until software writes the register with `PMOVE`. Recorded as a named item in
`docs/COMPLETION_PLAN.md` with two ways to close it.

This is the same discipline as the MD grammar, where the OCR damage *was*
recoverable because the manual expanded every token in prose below the figure.
Here it is not, so it stays open.

## Deliberate approximations

None yet. Each one added here carries its reason and cost to close.

## PROVISIONAL figures

Every entry is also a named item in `docs/COMPLETION_PLAN.md`.

| Figure | Current value | Why provisional | Cost to close |
| --- | --- | --- | --- |
| 68030 ATC replacement algorithm | first-invalid, then first entry with a clear history bit, sweeping when all are set | The `MC68030 Data Sheet 1991` was checked and is *less* specific still, calling it only "a variation of the least recently used algorithm" without naming the ingredients — so that document is a dead end for this, not a lead. `[030]` §9.4 names the policy and its ingredients — "a pseudo least recently used algorithm ... a validity bit and an internal history bit" — but never states the rule. Inventing a precise one would be a fabricated point number. What *is* documented is implemented exactly: invalid entries are reused first | Measure eviction order against the oracle, or find a Motorola application note that states the algorithm; medium. Affects hit rates and therefore timing, never the translation a hit produces |
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
