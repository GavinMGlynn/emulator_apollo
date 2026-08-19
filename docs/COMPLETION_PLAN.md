# Completion plan

Phased road to done. Each item names **its verification** — an item without one
cannot be ticked. Tails discovered while implementing something go into this
file the moment they are found, not when someone remembers.

**`[x]` means 100% of the item, and nothing less.** Not "the parts the boot
exercises", not "the parts the firmware's self-test reaches", not "enough to
move on". An item whose module has any documented behaviour unimplemented stays
`[ ]`, with the missing part named in its text — see `CLAUDE.md`, *FINISH THE
MODULE*. Splitting an item to tick the easy half is the same violation wearing
a different shape: split only where the halves are genuinely separate
deliverables, never to bank partial credit.

`[x]` done · `[ ]` not started, or **In progress** where the text says so

(A third checkbox state was tried, `[~]`. Markdown task lists recognise only
`[ ]` and `[x]`, so those items rendered as literal text beside real controls
everywhere the file is read.)

## Phase 0 — Foundations

- [x] C23 + CMake/Ninja/Clang build, presets per platform, matching test presets
      for every build preset. *Verification: `cmake --preset` and `ctest
      --preset` succeed for each; CI runs all four platforms.*
  - [x] Supported set pinned to three 64-bit platforms, Clang only, 64-bit
        enforced at configure time. *Verification: `cmake/Platform.cmake`; the
        configure line names the resolved platform, compiler and width.*
- [x] Warning set applied to first-party targets only, `-Werror` **in every
      build type**, not debug and CI alone. Vendored code is unaffected.
      Reasoning in `PROJECT_STATUS.md`. *Verification: `build.ninja` carries
      `-Werror` for the release preset, and both `linux-debug` and
      `linux-release` build clean from a wiped build tree.*
- [x] GitHub Actions matrix: ubuntu, rockylinux:9 container, windows
      clang-in-MSVC, macos arm64, plus an `-O0` vs `-O3` output-identity job.
      *Verification: matrix green.*
- [x] MIT licence with `ext/` and manual carve-outs; ROM/media gitignored so a
      public repo cannot leak them. *Verification: `git status` shows no
      `roms/` or `media/` path; LICENSE states the MAME boundary.*
- [x] Model table for the eight in-scope models, one row per machine.
      *Verification: `model_suite`, including that no MAME-covered model is
      still `PROVISIONAL` and each DSP matches its DN sibling exactly.*
- [x] Exact common time base for mixed clock domains. *Verification:
      `time_suite`, including the 25 MHz-vs-ring realignment period and
      rejection of a clock the base cannot represent.*
  - [x] Recomputed to 6.6 GHz once the ring PHY was confirmed bi-phase, adding a
        24 MHz line clock the 3.3 GHz base divided only as 137.5. This closed
        `RING.md` open question F. *Verification: `time_suite` asserts both ring
        clocks divide the base and that a bit cell is exactly two line windows.*
- [x] `ext/` submodules pinned and documented: unity, sdl, zlib, libpng (linked);
      mame, musashi (reference only). *Verification: CI builds with only
      `ext/unity` initialised, and a fresh clone with just that submodule
      configures, builds and passes `ctest`.*
  - [x] All four linked submodules moved from branch tips to release tags —
        unity `v2.7.0`, zlib `v1.3.2`, libpng `v1.6.58`, sdl `release-3.4.12`.
        *Verification: `ext/README.md` records each tag.*
  - [x] MIT/GPL boundary asserted at configure time rather than by review:
        `cmake/GplBoundary.cmake` rejects a `mame`/`musashi` include in any
        first-party source, and any target that acquires one in its link
        libraries or include directories. *Verification: both halves probed with
        a deliberate violation; both fail with a named message.*

- [x] Shared frontend layer in `src/frontend/common/`: part naming, the
      `--list-models` report, and the options every frontend accepts, parsed one
      argument at a time so each frontend keeps its own flags. *Verification:
      `frontend_common_suite` — every model selectable by name, a typo rejected
      rather than silently emulating the wrong machine, a frontend-specific flag
      passed through untouched, and the report byte-identical across calls.*

## Phase 1 — Verification infrastructure, before the subsystems it checks

- [x] Build MAME with only the apollo driver and assemble the `dn3500` ROM set
      from `roms/firmware/`. *Verification: MAME boots Domain/OS from an SR10.x
      image to a login prompt — done, on an SR10.4 system this project installed
      itself; `ex domain_os` from the disk alone reaches `login:` and `user`
      logs in.*
  - [x] ROM sets assembled by `tools/mame-oracle/romset.py`, which parses the
        table out of `apollo.cpp` rather than transcribing it, and matches our
        files to MAME's by SHA-1 rather than by name — so it cannot drift when
        the `ext/mame` pin moves. *Verification: all 11 apollo machines
        assemble, plus the `3c505` device set, and every one of our bitsavers
        images has exactly the SHA-1 the driver declares; output lands in the
        gitignored `tools/mame-oracle/out/roms/`.*
  - [x] **"Assembles" was not "runs", and this item said the wrong thing until a
        real run proved it.** Two defects hid behind eleven green machine sets:
        MAME loads a card's ROMs from a *sibling* set named after the device, so
        `dn3500` would not start without a `3c505` set beside it; and the ROM
        parser knew only `ROM_LOAD`/`ROMX_LOAD`, so a `ROM_LOAD16_BYTE` entry was
        skipped and a *partial* set reported as success. The device list is now
        derived from the driver's own `#include` lines and the whole `ROM*_LOAD`
        family is matched. The lesson is the item's, not just the code's: a
        verification that stops at "we produced files" cannot see that the files
        are unusable.
  - [x] The narrow build itself (`SUBTARGET=apollo`, `REGENIE=1`, `NOWERROR=1`).
        Detail in `PROJECT_STATUS.md`.
        *Verification: `./apollo -listfull` lists all eleven apollo machines,
        `dn3000` through `dn5500`, and `test_oracle.py` passes against it.*
        Two tails recorded in `FINDINGS.md` rather than left to bite the next
        person: the binary is named `apollo`, not `mameapollo` — MAME's own
        naming moved, so `oracle.py` now accepts either, the same
        pin-independence rule `romset.py` already follows for ROMs — and the
        build is bounded by *memory* rather than cores, at a measured ~2.5 Gbyte
        for the peak translation unit, so `-j"$(nproc)"` is the wrong instinct
        on a small machine.
  - [x] **The first-boot gate: closed.** All the Domain/OS media we hold is
        *installation* media, so reaching a login prompt required performing the
        install under the oracle first. Done, and the result boots.
        *Verification: `ex domain_os` from the disk alone — no `di c`, no
        cartridge — brings up the kernel and the Phase II environment, `user`
        logs in over the serial console, and `bldt` reports
        `**** Node 12345 **** "//node_12345"`.*
        - Artifact: `media/dn3500-sr10.4-installed.awd`, gitignored, pinned by
          SHA-256 in `docs/references/DOMAINOS_IMAGE.md` together with all five
          source cartridges.
        - Procedure: `tools/mame-oracle/install-domainos.cmds`, replayable, plus
          INVOL first on a blank image.
        - Told in full in `FINDINGS.md` C47-C58 and summarised in
          `PROJECT_STATUS.md`; needs the `ext/mame` SC-499 edit of C56.
  - This also pulls `.ct` cartridge support (Phase 4) forward in importance: it
    is the format the first boot depends on, not merely a storage item.
  - [x] **Answered: the 68040 path does have an oracle.**
        Detail in `PROJECT_STATUS.md`.
        to run it. *Verification: an attempted `dn5500` run under the built
        oracle, recorded in `FINDINGS.md` either way.*
- [x] Oracle harness: drive MAME headless, run N frames/cycles, dump RAM and
      device state in our hex format. *Verification: two runs of the same
      workload produce identical dumps — done, `dn3500` at 1.0, 3.5, 5.0 and 8.0
      emulated seconds and `dn5500` besides.*
  - [x] `tools/mame-oracle/dump.lua`, the state dumper MAME loads as
        `-autoboot_script`: sorted register dump widthed from each entry's own
        `datasize`, `read_u8`-based hex ranges, and a stop-notifier so a machine
        that halts early still dumps rather than silently producing nothing.
        *Verification: `oracle_dump_format`, 19 checks against a mock machine —
        no MAME needed, which is what lets the format be pinned in CI where the
        oracle is never built.*
  - [x] `tools/mame-oracle/oracle.py`, the driver, and its logic tested against
        a **stub MAME**. The item's two halves are separable: whether the
        oracle's *numbers* are right needs a real emulator and is what
        `FINDINGS.md` campaigns settle, but pulling the dump out of a noisy
        stdout, noticing that two runs disagree, and failing loudly on a run
        that produced nothing are ordinary program logic that needs no MAME at
        all. *Verification: `oracle_driver`, 19 checks over stub oracles chosen
        for the failure shapes that matter — a noisy one (real MAME prints its
        own chatter around the dump, and that must not make two identical runs
        look different), a nondeterministic one (catching that is the entire
        reason `verify` exists), a silent one, one that exits non-zero, and a
        check that a stale run directory is wiped rather than reused.*
  - [x] The remaining half: `verify` against the **real** oracle, showing two
        runs of a real workload byte-identical. Done — `dn3500` at 1.0, 3.5, 5.0
        and 8.0 emulated seconds, and `dn5500` besides. Getting there needed
        three fixes recorded in `FINDINGS.md`, of which two were invisible to the
        19 stub-oracle checks because they live in the seam between the driver
        and a real MAME: the driver built an environment and never passed it, so
        every reading was taken at the default 1.0s whatever `--at` said and no
        memory range was ever dumped; and the dumper triggered on frame
        notifications, which stop arriving at 3.246948 emulated seconds, so every
        later sampling point silently produced nothing while MAME still exited 0.
        Detail in `PROJECT_STATUS.md`.
- [x] `tools/mame-oracle/FINDINGS.md`: one entry per probe campaign, with its
      four discrepancy classes (`ours-wrong`, `oracle-wrong`, `sub-poll-slack`,
      `open`), the ban on closing a row by tuning our timing to match, and the
      rule that an oracle number alone never closes a row. *Verification: every
      closed row cites its evidence.*
- [x] Python probe encoder emitting hand-assembled 68000 probes — no cross
      toolchain. *Verification: a trivial probe that stores a sentinel runs
      identically under both — `probe_compare.py` encodes it once, runs it on
      this core through `--probe-file` and on the oracle through `probe.lua`,
      and the instruction count, D0 and the sentinel read back from memory all
      agree.*
  - [x] `tools/mame-oracle/encoder.py`: every opcode is a bit pattern cited to
        the PRM, so a wrong encoding is a wrong citation rather than a build
        problem. *Verification: `probe_encoder`, 47 checks, each asserting the
        manual's layout assembled field by field rather than the constant the
        encoder produces — a test comparing the encoder with itself passes on
        any consistent mistake.*
  - [x] `--probe-file` on the headless frontend and `tools/mame-oracle/probe.lua`
        for the oracle: the same words, written into RAM on both sides, needing
        no firmware, no boot and no Mnemonic Debugger.
  - Addresses are **not** compared: this core's probe RAM starts at zero and a
    DN3500's main memory at `01000000`, so the program is assembled twice at two
    bases and a PC differing by the base is not a disagreement. Clocks are not
    compared either, since instruction execution time is not yet modelled here.
  - Two harness traps, both of which reported a probe that had not run.
    `cpu.debug:step()` is **asynchronous** — the scheduler runs it after the Lua
    callback returns, so the result must be read on the *next* callback. And the
    first step after taking the machine over leaves the PC where it was put, so
    one unchanged PC is not a halt; two are.
- [x] Probes side-loadable into post-boot machine state, so CI needs no
      copyrighted firmware. *Verification: the probe suite runs in CI with
      `roms/` absent* — which it does: `apollo-headless --run-probes` reads no
      file, opens no ROM and needs no boot, and its output is pinned by
      `tests/goldens/probes.txt` under every build preset. Confirmed
      byte-identical between the `-O0` and `-O3` builds.
  - [x] **The machine to side-load into** (`src/core/machine/ap_machine.c`): a
        68030 on flat RAM and nothing else — construct, poke, run to a limit,
        read back, with no firmware and no boot. RAM is supplied rather than
        allocated; an out-of-range access is a counted bus error rather than a
        wrap or a zero. Rationale in `PROJECT_STATUS.md`.
        *Verification: `machine_suite`, 31 tests — the whole probe cycle, an
        out-of-range access faulting rather than wrapping including the
        straddling case, and two machines on different buffers hashing alike at
        every step.*
  - [x] **The probes themselves** (`src/core/probe/ap_probe.c`), eight small
        programs covering one thing each. A probe reports rather than judges,
        every probe ends with `STOP`, and the runner blanks RAM and plants a
        returning handler on every vector between probes. Rationale in
        `PROJECT_STATUS.md`.
        *Verification: `tests/goldens/probes.txt` under every preset and
        identical between `-O0` and `-O3`; plus `probe_suite`, 7 tests for what
        a golden cannot express — a golden will happily pin a probe that never
        terminates, faults, or differs run to run.*
  - [x] **A defect in the state hash, found by building on it.** Two machines
        constructed identically on two different buffers hashed *differently*.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `state_suite`, 2 further tests (12 total) — stale data
        behind an invalid cache entry not counting until the entry is validated,
        and an invalid ATC entry contributing its history bit and nothing else.*
  - Note: MD-driven probes need the boot PROM, so they are a
      *development-time* path, not the CI path. The CI path stays what this item
      says — inject probe state directly into a constructed machine, no firmware
      involved. The two are complements: MD is how a figure gets measured against
      real firmware, side-loading is how it gets regression-tested without it.
- [x] `regress.py` wired into CTest, checking golden result blocks on every
      platform. `cmake/Goldens.cmake` registers each golden as an ordinary CTest
      entry labelled `golden`, so every preset checks it and no bespoke CI step
      is needed. Missing Python is a **fatal configure**, not a skipped test:
      green while pinning nothing would defeat the portability claim.
      Detail in `PROJECT_STATUS.md`.
      *Verification: goldens bit-identical on all four CI targets and both build
      types; locally, a perturbed golden fails with a named diff and
      `goldens-update` regenerates byte-identically.*
- [x] Full-machine state hash over all architectural and timing state, excluding
      host pointers, with emulated cycle count and PC reported beside it.
      Detail in `PROJECT_STATUS.md`.
      *Verification: same workload twice → same hash — `machine_suite`, two
      machines on two RAM buffers with two boards, agreeing at **every step**
      rather than only at the end, with the workload asserted to have reached
      the devices and the hash asserted to have moved; a hash that never changed
      would satisfy the first check perfectly and detect nothing.*
  - [x] The hashing primitive itself (`src/core/state/`): FNV-1a 64-bit,
        little-endian by construction and width-tagged, with no `ap_hash_ptr()`
        so host pointers are excluded by construction rather than by discipline.
        *Verification: `hash_suite`, 13 tests, including the **published**
        FNV-1a 64 vectors — external constants, not our own output.*
  - [x] **The CPU's contribution** (`cpu/m68030/ap_m68030_state.c`): every
        register, the MMU and cache control registers, the pipe, both caches,
        the ATC, the pending exception state, and the accumulated clock — which
        is hashed *with* the registers, since two runs reaching the same
        registers by different numbers of bus cycles are not the same run on a
        core whose claim is emergent timing.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `state_suite`, 12 tests sweeping every field; and
        `step_suite`'s same-program-twice check.*
  - [x] **The board's contribution** (`board/ap_board_state.c`), which was the
        half this item was waiting on and could not be written until there were
        devices to write it about: the board registers, the translation map,
        both interrupt controllers, the timer, the calendar, both DMA
        controllers, both serial ports, the node ID, the disk and tape
        controllers, the graphics memories, the keyboard and the boot PROM.
        Detail in `PROJECT_STATUS.md`, including the two deliberate exclusions
        and their cost to close.
        *Verification: `board_state_suite`, 22 tests. The sweep is per **field**
        and not per device — a device fed as a whole struct passes a per-device
        test while quietly omitting half its members, which is how a hash goes
        hollow.*
  - [x] **The line between state and instrumentation**, which this item forced:
        the devices are hashed and the counters beside them are not, so adding
        an instrument cannot change a golden and two machines that behave
        identically cannot compare unequal because one was watched more closely.
        That moved `bus_errors` **out** of `ap_machine_hash`; the probe golden's
        hash column moved with it and every other column did not.
        Detail in `PROJECT_STATUS.md`.
        *Verification: one test in each of `board_state_suite` and
        `machine_suite`, perturbing every counter and demanding the hash hold
        still while `ap_machine_state` reports the change.*
  - [x] **Elapsed time folded in**, and it is not the CPU's clock count: the
        processor counts cycles, the machine keeps time, and the two part
        company as soon as a device advances on a clock of its own.
        *Verification: `machine_suite` — the same program at two clock rates,
        the processor's own hash asserted **equal** and the machine's asserted
        different, which is a stronger statement than either alone.*
  - [x] **The numbers beside the number** (`ap_machine_state`): the hash with
        the clock, the elapsed time, the PC and the bus-error count, which is
        what turns a disagreement into a bisection. The headless frontend prints
        the block, and takes the CPU's rate from the model table so `elapsed` is
        a real figure rather than zero.
  - The **bus** part is still open, and it belongs to the arbitration point
    rather than to this item: there is one bus master today, so there is no
    arbitration state to hash. It joins when a second master does — Phase 3.

## Phase 2 — The MC68030

**Re-scoped.** This phase was "CPU family", which put the 68882, the 68020, the
68851 and the 68040 behind the same checkbox as the 68030 — four processors that
do not exist yet, each the size of the MMU or the instruction step. That kept
the phase open for months while the thing it was really tracking, the 68030, was
nearly finished, and it hid how close that part is. They are now **Phase 2b**.

Phase 2 is the DN3500's own processor and closes when the 68030 does.

- [x] 68030 integer core, strictly cycle-stepped: one `tick()` per machine
      cycle, no batching, no event queues. *Verification: probes against the
      oracle; `MC68030 User's Manual 3ed` for the paper timing figures, each
      cited.*
  - [x] **Bus cycle state machine** (`src/core/cpu/m68030/ap_m68030_bus.c`),
        built first because it is the bottom of the timing stack: emergent
        timing means every clock an instruction takes is a clock some real bus
        cycle took. One `tick()` is one clock and runs that clock's two
        half-clock states in order. Detail in `PROJECT_STATUS.md`.
        *Verification: `bus_suite`, 25 tests, each citing its manual section —
        including a never-answered cycle that must not quietly complete, and the
        assertion that every CPU clock here has an even period in base units, so
        a half-clock is exactly representable.*
  - [x] **Write-cycle strobe timing, closed from `[030]` 7.3.2.** The tail was
        recorded as one difference (DS) and turned out to be three, which is
        exactly why it was transcribed rather than inferred: on a write DS moves
        from S1 to S3 — "indicating that the data is stable on the data bus" —
        DBEN moves from S2 to S1, and DBEN is *held through S5*, where a read
        negates it. The cycle's length is unchanged: the state sequence and
        termination rules are identical, so only the strobes differ. Asserting
        the read timing on a write would have told a device its data was stable
        a full clock before it had been driven.
        *Verification: `bus_suite` grew to 17 tests, four of them contrasting a
        write against a read directly, plus one that a new cycle clears the
        signals its predecessor held — so a write's DBEN cannot leak into the
        cycle that follows it.*
  - [x] **Instruction pipe and cache holding register**, `[030]` §8.1 and
        §11.3: a long-word fetch feeds two instruction words, so alignment
        decides how many bus cycles a run costs. Detail in
        `PROJECT_STATUS.md`.
        *Verification: `pipe_suite`, 14 tests. The headline one counts bus
        cycles for four sequential words — 2 when long-word aligned against 3
        when starting on an odd word, counted rather than averaged. Others cover
        the three-stage decode latency and the status bit travelling with its
        word, so a bus error faults where the word is *used*, not where it was
        fetched.*
  - [x] **Programming model** (`src/core/cpu/m68030/ap_m68030_regs.c`), `[030]`
        §1.3 with `[PRM]` §1.3.2, whose Figure 1-8 survives the scan where the
        68030 manual's does not — so the SR layout and the two tables are
        transcribed rather than derived. Detail in `PROJECT_STATUS.md`.
        *Verification: `regs_suite`, 10 tests, including `S=0 M=1` still
        selecting the USP and a CCR write being unable to reach `S` and
        escalate privilege.*
  - [x] **Effective address decode** (`src/core/cpu/m68030/ap_m68030_ea.c`),
        `M68000 Family PRM` §2, Figure 2-2 and Tables 2-1, 2-2, 2-4 — all
        intact, so nothing here is derived. The decode only; turning a decoded
        extension word into an address reads registers and, for the memory
        indirect modes, performs bus cycles, so it lands with the instruction
        unit. Same split that kept the cache's structure separate from its cost.
        *Verification: `ea_suite`, 11 tests.*
  - [x] Two decode traps, each with its own test. **Mode 111's register field is
        a sub-opcode, not a register number** — `111 000` is absolute short, and
        reading it as "register 0" is the classic 68000 decode bug; the decoder
        therefore reports a *kind* with mode 7 already folded in, and the three
        unassigned encodings (`101`-`111`) come back as invalid rather than as
        some mode. And **Table 2-2's IS and I/IS fields must be read together**:
        `I/IS = 001` is *preindexed* when IS is clear and *memory indirect* when
        it is set, so a decoder that reads I/IS alone silently produces the
        wrong addressing mode. Both IS halves of the table are checked in full,
        plus the two encodings side by side.
  - [x] `BD SIZE = 00` is **Reserved, not null**. Collapsing the two would
        accept an illegal instruction word as a legal one, so it is reported as
        reserved and tested against the legal null encoding beside it.
  - [x] **Conditional tests** (`src/core/cpu/m68030/ap_m68030_cond.c`), the
        sixteen conditions `Bcc`, `Scc`, `DBcc` and `TRAPcc` share, `[PRM]`
        Table 3-19. Detail in `PROJECT_STATUS.md`, including the lost overbars.
        *Verification: `cond_suite`, 9 tests. The headline one exhausts the
        space — sixteen conditions against all thirty-two CCR states, asserting
        every pair complementary, which turns the transcription from trusted
        into verified.*
  - [x] **Operation code map** (`src/core/cpu/m68030/ap_m68030_opcode.c`),
        `M68000 Family PRM` Table 8-2 — bits 15-12 select the instruction
        family, and everything below decodes per family.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `opcode_suite`, 6 tests — the whole published map with
        the low twelve bits varied to prove they do not influence the family,
        the move sizes asserted as sizes rather than names, and both emulator
        families resolving to the exception module's own vectors.*
  - [x] **Branch family decode** (`src/core/cpu/m68030/ap_m68030_branch.c`),
        family `0110`, the first per-family decoder and the one that consumes
        `ap_m68030_cond`. `M68000 Family PRM` §8.2 and the Bcc/BRA/BSR pages.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `branch_suite`, 8 tests — conditions 0 and 1 decoding as
        BRA and BSR (the encodings Table 3-19 marks unavailable to `Bcc`), both
        escapes, length following displacement size, the base being +2
        *regardless* of size, the return address differing from the base for the
        wider forms with the 8-bit case asserted to agree, and a branch past the
        end of the address space wrapping rather than saturating.*
  - [x] **Family `0101` decode** (`src/core/cpu/m68030/ap_m68030_quick.c`):
        ADDQ, SUBQ, Scc, DBcc and TRAPcc, five instructions in one encoding
        space separated by fields that overlap rather than nest. Bits 7-6 are
        the ADDQ/SUBQ size field whose `11` encoding is not a legal size, and
        that spare encoding selects the conditional group — at which point bit
        8, the direction bit, becomes part of the condition. Within that group
        the EA *mode* field separates the rest **by reusing encodings `Scc`
        cannot legally take**: `Scc` writes a byte, so an address register
        destination is meaningless, and that is exactly where `DBcc` lives.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `quick_suite`, 10 tests.*
  - [x] **Family `0100` ("Miscellaneous")**, the largest in the map. Taken as
        coherent subtrees rather than claimed whole in one pass, and now
        complete in both decode and execution -- `BKPT` was the last piece, and
        it needed a CPU-space bus cycle rather than more of this family.
    - [x] **The `$4E` control group**
          (`src/core/cpu/m68030/ap_m68030_control.c`): TRAP, LINK, UNLK, MOVE
          USP, RESET, NOP, STOP, RTE, RTD, RTS, TRAPV, RTR, JSR and JMP. The
          subtree narrows in stages rather than by one field — bits 7-6 take
          JSR and JMP with a six-bit effective address between them, and `01`
          opens the control group where bits 5-3 choose the rest.
          Detail in `PROJECT_STATUS.md`.
          *Verification: `control_suite`, 10 tests — the `$4E70`-`$4E77` run
          checked as a whole so a transposition inside it cannot pass, all
          sixteen TRAP numbers, LINK/UNLK and MOVE USP splitting on bit 3,
          JSR/JMP effective addresses, only LINK/RTD/STOP carrying a following
          word, and `$4E78`-`$4E7F` being unassigned rather than aliased.*
    - [x] **The LEA/CHK and `$48`/`$4C` subtree**
          (`src/core/cpu/m68030/ap_m68030_misc.c`): LEA, CHK.W, CHK.L, PEA,
          SWAP, BKPT, EXT.W, EXT.L, EXTB.L, NBCD and MOVEM.
          Detail in `PROJECT_STATUS.md`.
          *Verification: `misc_suite`, 11 tests, including each hole against the
          instruction whose space it sits in, and the `MOVEM` direction that has
          no `EXT`.*
    - [x] **The single-operand group**
          (`src/core/cpu/m68030/ap_m68030_single.c`): NEGX, CLR, NEG, NOT, TST,
          TAS, `MOVE to/from SR`, `MOVE to/from CCR` and ILLEGAL. **Family
          `0100` is now complete.**
          Detail in `PROJECT_STATUS.md`.
          *Verification: `single_suite`, 7 tests.*
    - [x] **Correction: `$4E7A`/`$4E7B` are MOVEC, not unassigned.** The `$4E`
          control group first landed with the whole `$4E78`-`$4E7F` run decoded
          as invalid, and a test that asserted it. That would have made every
          `MOVEC` an illegal instruction — and with it `VBR`, `CACR` and the MMU
          root pointers unreachable, since `MOVEC` is the only way to load them.
          Detail in `PROJECT_STATUS.md`.
  - [x] **MOVE and MOVEA decode** (`src/core/cpu/m68030/ap_m68030_move.c`),
        families `0001`, `0010` and `0011` — one per operand size.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `move_suite`, 8 tests.*
  - [x] **Family `0000` decode** (`src/core/cpu/m68030/ap_m68030_immediate.c`):
        ORI, ANDI, SUBI, ADDI, EORI, CMPI, MOVES, the static and dynamic bit
        operations, MOVEP, and the `to CCR`/`to SR` forms. Bit 8 splits the
        family — clear selects an immediate row, set makes the bit number
        dynamic.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `immediate_suite`, 10 tests.*
  - [x] **The arithmetic and logic families**
        (`src/core/cpu/m68030/ap_m68030_arith.c`): `1000` OR/DIV/SBCD, `1001`
        SUB/SUBX, `1011` CMP/EOR, `1100` AND/MUL/ABCD/EXG, `1101` ADD/ADDX.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `arith_suite`, 9 tests.*
  - [x] **Family `1110` decode** (`src/core/cpu/m68030/ap_m68030_shift.c`):
        shifts, rotates and the 68020's bit field instructions. **This completes
        the integer opcode map** — every family from `0000` to `1110` now
        decodes, with `1010` being the Line A trap the map itself handles.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `shift_suite`, 10 tests.*
  - [x] **Family `1111` decode** (`src/core/cpu/m68030/ap_m68030_coproc.c`):
        the coprocessor interface, and with it **the operation code map is
        complete** — every family from `0000` to `1111` now decodes.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `coproc_suite`, 6 tests, including the two vectors from
        one word and the rule holding for cpID 0 against all seven others.*
  - [x] **Decode dispatcher** (`src/core/cpu/m68030/ap_m68030_decode.c`) and
        `MOVEQ`, the one family that had no decoder: which family claims a word,
        and what that family made of it. Family `0100` needs three subtrees
        tried in the order their encodings nest. Detail in `PROJECT_STATUS.md`.
        *Verification: `decode_suite`, 17 tests, including the property no
        family suite can check — a sweep of the **entire** 16-bit space
        asserting every word classifies, and that the wholesale families
        (`1010`, `1111`, `0110`) claim all 4096 of their words. 89.9% of
        encodings claimed, 10.1% illegal; neither should move without a reason.*
  - [x] **Effective address extension word counts** (`ap_m68030_ea_words`),
        the piece an instruction's total length is built from and therefore what
        advances the PC. A wrong count here does not fault — it desynchronises
        every following instruction, which is why it is pinned per mode rather
        than derived at each call site.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `ea_suite`, 6 further tests (17 total).*
  - [x] **Total instruction length** (`ap_m68030_instruction_length`), joining
        `ap_m68030_ea_words` to each family's own extension words. **The PC can
        now advance.**
        Detail in `PROJECT_STATUS.md`.
        *Verification: `decode_suite`, 10 further tests (17 total), including a
        second full 65536-word sweep asserting every sizeable instruction has an
        even, non-zero length — instructions are whole words, and that is the
        check no individual case can make.*
  - [x] **Effective address calculation**
        (`src/core/cpu/m68030/ap_m68030_addr.c`): decoded fields into an
        address, with the increment modes' register side effects applied.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `addr_suite`, 13 tests.*
  - [x] **Memory indirect address calculation**, the full-format modes with an
        indirect action. They need a bus read partway through the calculation,
        which belongs with the instruction unit that owns the bus — so
        `ap_m68030_address_calculate` reports `indirection_pending` and the
        address it reached rather than returning a half-computed address as
        though it were final — and now also `post_indirection`, what the mode
        still owes once the read has happened, which differs between the two
        forms because the index sits inside the brackets for one and outside for
        the other. The read itself landed with the step, which is what owns the
        bus. *Verification: `step_suite`'s preindexed and postindexed pair, run
        on the same registers and displacements so only the index's placement
        can account for the difference.*
  - [x] **The logical memory access path**
        (`src/core/cpu/m68030/ap_m68030_access.c`), joining the caches, TTx, the
        ATC, the table walk and the bus. Its whole content is the *order*, and
        the order is the reverse of the intuitive one.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `access_suite`, 7 tests — a cache hit asserted to cost
        zero clocks **and** to leave the table-fetch and fill counters
        untouched, which are separate claims; one burst fill serving a whole
        line with none of the four consulting the MMU; a disabled cache and the
        CDIS signal each forcing the MMU every time; a transparent access
        skipping the tables entirely; and the table search being paid once per
        *page* rather than once per line.*
  - [x] **Writes through the same path**, and the asymmetry with reads *is* the
        content: the data cache is writethrough, so a write always runs an
        external cycle and the MMU is always consulted. That is also what makes
        write protection work — a write answered from the cache would make a
        write-protected resident page writable. Detail in `PROJECT_STATUS.md`.
        *Verification: `access_suite`, 13 tests, including a write to a fully
        warm address still consulting the MMU where the read before it did not.*
  - [x] **Instruction prefetch from real memory**
        (`src/core/cpu/m68030/ap_m68030_fetch.c`), joining `ap_m68030_pipe` to
        `ap_m68030_access`. `pipe_suite` pins the pipe against words a test
        hands it; this pins the same behaviour when the words come from the
        memory path, which is where the cost actually is.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `fetch_suite`, 5 tests — the second word of a long word
        costing nothing, the two-against-three count, the words reaching stage D
        in order so the saving is real rather than a dropped fetch, a re-fetch
        hitting the instruction cache, and a pipe reset discarding the holding
        register so a branch target's neighbour is not wrongly free.*
  - [x] **The instruction step** (`ap_m68030_step.c`): fetch through the pipe
        and instruction cache, decode, execute, advance the PC, account the
        clocks. **A program runs.**
        *Verification: `step_suite`. Detail in `PROJECT_STATUS.md`.*
  - [x] **Operand access** (`src/core/cpu/m68030/ap_m68030_operand.c`): reading
        and writing an operand through a decoded effective address, with the
        data-register and address-register write rules, which look alike and are
        opposites. Detail in `PROJECT_STATUS.md`.
        *Verification: `operand_suite`, 13 tests, including the two rules applied
        to the **same** operand value side by side, plus an unfinished address
        and an immediate each reported as a fault rather than read as a zero
        that would look like a real operand.*
  - [x] **MOVE and MOVEA semantics**, in the addressing modes reachable without
        an extension word. The extension-word modes were excluded for a concrete
        reason: `MOVE`'s destination extension words sit after its source's, and
        guessing a displacement of zero would run and be wrong.
        *Verification: `step_suite`, 7 further tests — a word `MOVE` leaving the
        destination's upper half intact against `MOVEA.W` sign-extending all 32
        bits, which is the operand layer's two rules seen through running code.*
  - [x] **Defect found by that round trip: the write path never wrote
        through.** `ap_m68030_access_write` translated the address and updated
        the data cache but never issued the external write cycle — so it
        documented writethrough and behaved like writeback. Every existing test
        passed, because none of them observed memory. Fixed by adding the store
        callback the cache fill already had, with a regression test that writes
        twice to a *cached* line and asserts memory saw both, at the physical
        address rather than the logical one.
  - [x] **Extension word fetching**, which unblocks every addressing mode at
        once. They come from the *same prefetch path* as the instruction word,
        so they cost what the manual says and share the holding register's
        savings. Detail in `PROJECT_STATUS.md`.
        *Verification: `step_suite`, 6 further tests — including both operands
        taking their words **in order**, checked by giving them different
        displacements so a swapped read produces the wrong address.*
  - [x] **The MMU instructions**, which are how every MMU register and the ATC
        actually get driven: `PMOVE`, `PFLUSH`, `PFLUSHA`, `PLOAD` and `PTEST`.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `step_suite`, 15 further tests — including a table-search
        `PTEST` leaving the ATC empty and returning the descriptor's address,
        and the ATC surviving `PMOVEFD` where `PMOVE` flushes it.*
    - [x] `PTEST`'s level is a **search depth**, not merely a choice between the
          ATC and the tables — "the search ends at the specified level" — and the
          address register takes the last descriptor *successfully* fetched. Both
          were missing here while the 68851's own search had the ceiling from the
          day it was written. Detail in `PROJECT_STATUS.md`.
          *Verification: `walk_suite` 47 tests (7 further) and `mmusr_suite` 17
          (1 further) — one table accessed per level asked for, a truncation
          reported apart from a fault, and a bus-errored fetch leaving the last
          readable descriptor named.*
    - [x] The fault profile is keyed by the faulting **instruction**, with the
          address span it reached and a count, and what the cap refuses is
          counted rather than dropped in silence. Keyed by address — which is
          how it was first built — a boot came back `64 distinct, 335 more not
          recorded` with 62 slots taken by one PROM scan, so no one-off fault
          could appear at all. Detail in `PROJECT_STATUS.md`.
          *Verification: `machine_suite` 52 tests (4 further) — one instruction
          faulting three times as one row, a scan's span recorded from both
          ends, two instructions on the same address kept apart, and the
          overflow counted.*
    - [x] The MMU's refusals are recorded too, keyed the same way and over
          *logical* addresses. A bus error from the board and one from
          translation are the same vector 2 to the program, and only the
          board's were counted: a boot taking 939 vector 2 exceptions while the
          board refused 652 accesses had 287 faults recorded nowhere. This is
          what named the fatal one. Detail in `PROJECT_STATUS.md`.
          *Verification: `machine_suite` 55 tests (3 further) — an MMU refusal
          counted apart from a board refusal, the logical address kept
          unrounded, and a machine that translates cleanly reporting zero.*
    - [x] `--boot-stop-pc-skip N` ignores the first N times the stop address is
          reached. One address executed on a path that recovers and again on
          one that does not is two events, and the second is usually the
          question — the fatal fault's PC faults twice and only the second
          matters.
          *Verification: `check_frontend_flags.py` exercises it; the stop fires
          at the second occurrence of `3C47A25A` where the unskipped run fires
          at the first.*
    - [x] `--boot-progress-from ADDR` counts `--boot-progress` from the first
          execution of ADDR rather than from reset, so two machines that reach
          the same code at different absolute counts sample the **same**
          instants and their PCs compare. It is what located the divergence to
          one million instructions. Detail in `PROJECT_STATUS.md`.
          *Verification: 646 samples against the oracle's 966 at matched
          deltas, identical PCs at Δ 20 M and 30 M and a clean split at 53 M.*
    - [x] `--boot-stop-pc-then N` runs N more instructions after a stop fires
          and *then* ends, so a trace ring holds the window **after** an event
          rather than before it — which is what comparing two machines from the
          same point needs. Applies to the MMU-fault stop as well, where the
          question is entirely what the handler does next.
          *Verification: `check_frontend_flags.py`; the two 4000-instruction
          windows it captured are diffed in `PROJECT_STATUS.md`.*
    - [x] `--boot-stop-on-mmu-fault-at ADDR` ends a run when translation
          *refuses* a logical address — the event itself, where
          `--boot-stop-pc-skip` catches only a proxy for it and caught a visit
          that succeeded. That mis-measurement produced a published conclusion
          that had to be withdrawn; detail in `PROJECT_STATUS.md`.
          *Verification: the stop fires at 385,198,347 on `3BFF0001` where the
          skip-based stop fired 11,480 instructions earlier on a visit that did
          not fault.*
    - [x] A root pointer whose DT field is `page descriptor` is **direct
          mapping with a constant offset**, not an early termination page:
          `physical = logical + table address`, no descriptor fetched, and a
          limit check performed all the same. `ap_m68030_root_t` collapsed DT to
          `long_format`, so DT `$1` was walked as a short-format table and read
          whatever sat at the offset. Detail in `PROJECT_STATUS.md`.
          *Verification: `walk_suite` 52 tests (5 further) — the addition, zero
          fetches with a zero descriptor address (which is `PTEST`'s `$0` for
          this case), a zero offset as the identity, and the limit check both
          ways.*
  - [x] **Full-format indexed addressing and the memory indirect modes**. The
        extension word declares its own base and outer displacement sizes, so
        the number of words to read is not known until that word has been read —
        and neither, therefore, is the instruction's length.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `step_suite`, 5 further tests (124 total) — a full-format
        base and index reaching an operand with the following instruction still
        running, which is the length check; the preindexed and postindexed forms
        on the **same** registers and displacements so only the placement can
        account for the difference; a suppressed base contributing zero rather
        than its register; the scale applied before the addition; and a reserved
        displacement size declined with the PC unmoved.*
  - [x] **Integer ALU** (`src/core/cpu/m68030/ap_m68030_alu.c`): ADD, SUB, CMP,
        AND, OR and EOR, with their condition codes.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `alu_suite`, 8 tests — the two exhaustive sweeps; word
        and long cases that would catch a hardcoded byte sign bit; all four
        combinations of carry and overflow shown reachable, since a single
        "did it fit" flag would lose the distinction; `CMP` differing from `SUB`
        **only** in X; and X left alone by the logical operations against being
        replaced by the carry in the arithmetic ones.*
  - [x] **The ALU wired into the step**: `ADD`, `SUB`, `CMP`, `AND`, `OR` and
        `EOR` execute in both directions, over every addressing mode extension
        words reach.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `step_suite`, 8 further tests (30 total) — accumulation
        into a register; subtraction in the documented order and its borrow;
        `CMP` leaving both operands alone while setting Z; **a compare followed
        by a conditional branch**, the pattern every loop is built from and the
        first time three instructions have had to agree; the memory direction
        writing back to the effective address rather than the register; a byte
        operation leaving a register's upper bytes intact through the arithmetic
        path; and the logical operations clearing V and C while X survives.*
  - [x] **The immediate family and the single-operand group execute**: `ORI`,
        `ANDI`, `SUBI`, `ADDI`, `EORI`, `CMPI`, and `CLR`, `NEG`, `NOT`, `TST`.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `step_suite`, 11 further tests (41 total) — including
        `SUBI` subtracting *from* the destination rather than the reverse,
        `CMPI` and `TST` writing nothing, byte forms leaving a register's upper
        bytes intact through a third and fourth code path, `NEG` borrowing for a
        non-zero operand and **not** for zero (the boundary the rule turns on),
        and **a countdown loop that terminates** — five instruction kinds
        cooperating, and the first program here whose control flow repeats.*
  - [x] **`ADDQ`, `SUBQ`, `Scc` and `DBcc` execute.**
        Detail in `PROJECT_STATUS.md`.
        *Verification: `step_suite`, 10 further tests (51 total), including a
        `DBcc` loop running the documented number of times — a count of three
        runs the body four times, three decrements that branch and a fourth that
        reaches −1 and falls through.*
  - [x] **The address-register forms and the bit operations execute.**
        Detail in `PROJECT_STATUS.md`.
        *Verification: `step_suite`, 10 further tests (61 total), including bit
        31 reachable on a register, the bit number wrapping modulo the width,
        and only Z affected with X, N, V and C all asserted to survive.*
  - [x] **Shifts and rotates execute**, both the register form and the
        one-bit-in-memory form. Three rules carry the weight, each a place a
        plausible implementation goes wrong silently:
        **A count of zero is not a no-op.** It leaves X alone and clears V and
        C — *except* for `ROXL`/`ROXR`, where Table 3-18 gives "C ?  X=C", so C
        takes X's value. A model returning early on a zero count is right four
        times out of six.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `alu_suite`, 9 further tests (17 total); `step_suite`, 3
        more (64), including a register count taken modulo 64 so a count of 64
        shifts by nothing.*
  - [x] The last of the instruction semantics: divides and multiplies, and the
        register-to-register extended forms (`ADDX`/`SUBX`/`ABCD`/`SBCD`/
        `CMPM`/`EXG`). Detail in `PROJECT_STATUS.md`, including the three rules
        a plausible implementation drops: `ADDX`/`SUBX` clear `Z` rather than
        setting it, `ABCD`/`SBCD`'s `N` and `V` follow real silicon rather than
        the manual's "undefined", and a divide's overflow leaves the operands
        alone.
        *Verification: `step_suite`, 16 further tests (80 total), with `CHK`'s
        `Z` taken from the register and not the bound — the plausible wrong
        reading.*
  - [x] **Misaligned operands are performed, not declined**, and this is not an
        edge case: every exception frame puts its long-word PC at `SP + 2`, so
        `RTE` and `RTR` read a straddling long *every time*. A model that
        declined them would decline returning from every exception. The operand
        layer now splits a transfer into one bus cycle per long word, in address
        order, with the operand's most significant bytes at the lower address so
        a split write and a split read agree. `ap_m68030_access_write` takes the
        operand size rather than an "is it a long" flag, and passes it to the
        memory system — telling it every write is four bytes wide would have a
        byte store clobber its three neighbours — and derives the cache's write
        allocation rule from the size and address together rather than trusting
        the caller to assert it.
        *Verification: `operand_suite`, 2 further tests — a straddling long
        splitting into two cycles at the right addresses, plus the `RTE` round
        trip in `step_suite`, which is the real output.*
  - [x] **Sub-long-word operands are selected by position, not by mask.** Found
        while testing the memory `ABCD`: the access path answers in long words,
        and `ap_m68030_operand_read` masked the low bits of one instead of
        shifting the operand down from where its address puts it. Every byte
        read returned the long word's last byte and every word read its low
        half — right exactly when `A & 3` is 3, silently wrong the other three
        times in four, and never faulting. An operand straddling two long words
        now declines rather than returning half of one; the 68030 does perform
        the two bus cycles that case needs, so that is a named gap.
        *Verification: `operand_suite`, 3 further tests (11 total), one sweeping
        all four byte offsets so no single lucky alignment can carry it.*
  - [x] **CMP2/CHK2, CAS and CAS2 decode**
        (`src/core/cpu/m68030/ap_m68030_bounds.c`), which occupy size field `11`
        in family `0000`'s immediate rows. **The opcode map now has no holes
        left in it.** Their *semantics* are still open: `CAS` and `CAS2` need an
        indivisible read-modify-write, which is the bus module's item.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `bounds_suite`, 9 tests — including the two size
        encodings side by side on the same bit pattern, each half's unassigned
        value being the other's valid one, and each taking the addressing mode
        category its operand needs.*
  - [x] **The immediate source, swept.** "An immediate is fetched, not
        addressed" had been fixed four separate times — in the arithmetic
        forms' address path, in the multiplies and divides, in `CHK`'s bound,
        and in `MOVE to SR`/`MOVE to CCR`, that last being how every
        68000-family boot ROM sets itself up. Four is enough: rather than wait
        for a fifth failing test, every `gather_address_input` call site was
        checked against its instruction's own page.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `step_suite`, 3 further tests (158 total) — an immediate
        source accepted in two different families with the following
        instruction still running, which is the length check; the memory
        direction refusing an immediate destination; and `TST` in both the zero
        and negative cases.*
  - [x] **Addressing mode categories** (`src/core/cpu/m68030/ap_m68030_category.c`),
        which decide whether a decoded mode is *legal* for an instruction.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `category_suite`, 8 tests — each of the four definitions
        swept over every mode, plus the two independent cross-checks against
        `MOVE`'s and the MMU instructions' own pages, `LEA` admitting the
        PC-relative modes (which is what makes position-independent code
        possible) while excluding the increments, and an invalid mode belonging
        to no category. `step_suite`, 3 further tests (142 total) — `LEA (A0)+`
        refused **with A0 unmoved**, `MOVE.W D0,(d16,PC)` refused with nothing
        stored, and `PMOVE (A0)+,TC` refused.*
  - [x] **Instruction execution time — the microcode clocks between the bus
        cycles.** **Closed.** Every transcribed row is priced as
        `microcode + measured operand bus + prefetch cost`, so a published
        figure and a measured one are never added twice.
        *Verification: `timing_suite`, `timing_table_suite` and the probe
        goldens. Detail in `PROJECT_STATUS.md` and
        `docs/references/M68030_TIMING.md`.*
  - [x] **Effective address times, §11.6.1–§11.6.5**, composed through Equation
        (11-2), which overlaps the effective address's tail against the
        operation's head — the reason `head` and `tail` were transcribed from
        the start. A footnoted row is left at bus time alone rather than
        reported as a total, so it reads as the lower bound it is. Every row is
        classified, and two readings landed `PROVISIONAL` with their closing
        measurements.
        *Verification: `timing_table_suite` asserts **zero** rows classified
        unknown, so a row added without a class decision fails rather than being
        priced by whichever rule sits first. Detail in `PROJECT_STATUS.md` and
        `docs/references/M68030_TIMING.md`.*
  - [x] **The termination *kind* now comes from a device.** `machine_fill` and
        `machine_store` ask the board and answer `BERR` when nothing decodes the
        address, `STERM` when something does — so a bus error is a device
        declining to answer rather than a test asserting one. That is what made
        the boot PROM's 129 self-test faults, the AT bus empty-slot reads and
        the display probe all behave as the hardware does.
  - The termination's **arrival clock** is **moved to Phase 3**, where its own
    text always said it belonged: `STERM` is answered at a fixed two-clock
    minimum regardless of which device replied, so a slow device cannot lengthen
    a cycle. That is the arbitration point's item and not the 68030's, and
    leaving it here made Phase 2 look incomplete for work that is not Phase 2's.

- [x] Exceptions, traps, interrupt priority, bus/address error stack frames.
      *Verification: probes that deliberately fault, diffed against oracle.*
  - [x] **That verification line is now met.** The behaviour was implemented
        and heavily tested from the inside; what the line asked for was the
        *outside* half, and no probe deliberately faulted. Four probes now do —
        illegal instruction, bus fault, and the two frame formats — diffed
        against the oracle. Detail in `PROJECT_STATUS.md` and `FINDINGS.md`
        C72-C74.
        *Verification: `probe_compare.py --program fault` and `--program
        bus-fault`, both running identically on the two implementations.*
  - [x] **Vectors, priority and frame formats**
        (`src/core/cpu/m68030/ap_m68030_exception.c`), `[030]` §8 and Tables
        8-1, 8-5, 8-6. The part that is pure fact, and that everything else will
        be checked against. Table 8-1 is used as a *check* rather than
        transcribed: offsets are computed as vector × 4 and the table's own
        published hex is asserted against that, so a wrong rule fails instead of
        being copied in.
        *Verification: `exception_suite`, 14 tests — the published offsets
        across the table's whole range, autovectors 25-31 and traps 32-47, the
        documented priority order end to end, address error outranking bus error
        *within* group 1, each frame format's documented size, and only the six
        formats the 68030 defines being valid.*
  - [x] Two details that would produce a frame `RTE` accepts and mishandles.
        Detail in `PROJECT_STATUS.md`.
  - [x] Level 7 interrupts, which cannot be expressed as a comparison against
        the mask. "Level 7 interrupts cannot be masked by the interrupt priority
        mask, and they are transition sensitive ... recognizes an interrupt
        request each time the external interrupt request level changes from some
        lower level to level 7, regardless of the value in the mask." So
        recognition needs the *previous* level, which the interface takes: held
        at 7 is not a new interrupt, dropped and re-raised is. The manual's own
        level 6 contrast is tested alongside it.
        Detail in `PROJECT_STATUS.md`.
  - [x] **Family `0100` executes**, completing the family the decoder finished
        earlier: `LEA`, `PEA`, `SWAP`, the three `EXT` forms, `NBCD`, `CHK`,
        `MOVEM` both directions, `NEGX`, `TAS`, `MOVE` to and from `SR` and
        `CCR`, and `ILLEGAL`. `BKPT` is declined rather than called illegal — it
        runs a breakpoint acknowledge cycle this step does not issue. Detail in
        `PROJECT_STATUS.md`.
        *Verification: `step_suite`, 14 further tests, including `MOVEM`'s
        register list order in both directions and `EXT`'s three widths.*
  - [x] **The `$4E` control group executes in full**: `JSR`, `JMP`, `BSR`,
        `RTS`, `RTR`, `RTD`, `RTE`, `LINK`, `UNLK`, `TRAP`, `TRAPV`, both
        directions of `MOVE USP` and `MOVEC`, `STOP` and `RESET`, with the
        privileged ones raising a privilege violation in user state. `MOVEC`'s
        sparse register codes, `STOP`'s ordering and `RESET`'s
        nothing-happens-inside semantics are in `PROJECT_STATUS.md`.
  - [x] **The wider branch displacements**: `BRA`/`Bcc`/`BSR` at 16 and 32 bits,
        all three sizes sharing one base, "the instruction address plus two"; and
        with them `RTE`/`RTR`, `LINK`/`UNLK`, `JMP`/`JSR`, `TRAP`/`TRAPV` and
        `MOVE An,USP`. Detail in `PROJECT_STATUS.md`.
        *Verification: `step_suite`, 16 further tests across the two landings —
        a `BSR`/`RTS` round trip, a word branch landing where a byte one would,
        a long `BSR` pushing the address after **both** displacement words,
        `LINK`/`UNLK` as exact inverses, `RTR` declining to restore a stacked S
        bit, an undefined frame format becoming a format error, and `TRAPV` in
        both directions since a model that always trapped would pass a test that
        only set V.*
  - [x] **Taking an exception**: stacking the frame, fetching the vector
        through the VBR, and loading the PC (`ap_m68030_take_exception`). Every
        frame this model can build, with the two bus fault frames and the
        coprocessor mid-instruction frame each carrying their own rules.
        *Verification: `exception_suite` 16 tests and 10 in `step_suite`,
        `[030]` §8.1 and Table 8-6. Detail in `PROJECT_STATUS.md`.*
  - [x] An encoding the manual's **effective-address category tables** forbid
        takes the machine's trap instead of reporting our gap: vector 4 for
        `MOVE` and the misc group, vector 11 for the MMU's F-line words (p. 8-10,
        not vector 4 — the distinction a single flag would have lost). 791 of
        the 65536 opcodes were reclassified. `step_suite` 4 tests updated, each
        of which had been asserting the wrong verdict. Detail in
        `PROJECT_STATUS.md`.
  - [x] The same category tables enforced in the **single-operand, immediate
        and shift** groups, each rule taken from its own instruction page.
        **578 words were executing instructions the hardware refuses** — the
        expensive direction — and now trap. Three rules no single category
        expresses: `TST` reaches every mode but bars a *byte* address register,
        `CMPI` is data rather than data alterable, and `BTST`'s two forms
        disagree about the immediate. `step_suite` +4. Detail in
        `PROJECT_STATUS.md`.
  - [x] Categories in the **quick** group (family 5) and the **ALU** group
        (families 8, 9, B, C, D), the last two that enforced none. `ADDQ`/`SUBQ`
        are *alterable* rather than data alterable — so they reach an address
        register, at word and long only — and the six ALU instructions state
        their category **per direction**, the source half differing between
        `ADD`/`SUB` ("all modes") and `AND`/`OR` ("data"). A further **1040
        words** stopped executing instructions the hardware refuses.
        `step_suite` +1, one test corrected. Detail in `PROJECT_STATUS.md`.
  - [x] **`EOR Dn,Dn` was decoded as illegal**, found while verifying the above.
        Four of the five memory-direction families need a *memory* alterable
        destination, which leaves the register-destination hole for `SBCD`,
        `SUBX`, `ADDX` and `ABCD`; `EOR`'s is *data* alterable, so its mode-000
        encoding is an ordinary instruction. `arith_suite`'s own test asserted
        the wrong verdict on the wrong reasoning.
  - [x] **`MOVEP` and `BTST Dn,#<data>`**, the two instructions the sweep named
        as genuinely absent. `MOVEP` moves alternate bytes at increments of two,
        high-order first; its word form replaces sixteen bits and leaves the
        register's upper half, and it touches no condition code. `BTST`'s
        dynamic form is the one bit operation whose operand can be an immediate,
        so it is handled before the address path — there is no address to
        gather. `step_suite` +3, 264 words. Detail in `PROJECT_STATUS.md`.
  - [x] The **last three category holes**, found by naming all 716 remaining
        `UNIMPLEMENTED` words by decoded kind rather than guessing: `CHK` (whose
        check existed but returned no verdict), `JMP`/`JSR` (which had none and
        resolved the address *first*, moving `A0` on a refused `JMP (A0)+`), and
        `MOVEM` (which checked the increment pairing but never the category).
        296 words. `step_suite` +2. Detail in `PROJECT_STATUS.md`.
  - [x] **`MOVES`** (180 words), the one instruction that reaches an *arbitrary*
        address space: it carries whatever `SFC`/`DFC` hold rather than a
        function code fixed by what the access is. An address-register
        destination is sign-extended and a data register keeps its upper bits;
        condition codes are untouched. Writing its test found that
        `ap_m68030_immediate_privileged` had **no caller** — the three `to SR`
        forms were checked by a condition written out again inline, and `MOVES`
        was not checked at all, so a user program could have read supervisor
        memory with it. `step_suite` +3. Detail in `PROJECT_STATUS.md`.
  - [x] **`TRAPcc`** (48 words — all three forms, not just the operand ones).
        The operand is consumed whether or not the trap is taken: it is part of
        the instruction and only *available* to the handler, so dropping it runs
        it as an instruction after every trap of one polarity. `step_suite` +1.
  - [x] **Reserved coprocessor instruction types** (128 words). §10.2 names four
        categories — general, conditional, context save, context restore — and
        types 110/111 are none of them, so they take the line 1111 emulator
        exception. Marked a *reading*: the manual defines the four and is silent
        on a fifth. Without it they fell through to the general path, which
        fetches no command word for them, so the FPU was asked to execute
        command zero.
  - [x] **The MMU's remaining 64 words classified**, and with them the last
        `UNIMPLEMENTED` opcode in the instruction set: **2621 → 0**. The rule is
        transcribed, not inferred — p. 9-51 lists the 68851 forms a 68030 lacks,
        including "`PMOVE` for unsupported registers", and says they "must be
        avoided or emulated in the exception routine for **F-line unimplemented
        instructions**". Five sites across `execute_pmove`,
        `execute_pflush_or_pload` and `execute_ptest`, two of which already had
        the verdict in a comment and returned the other one. `machine_suite` +1,
        which now sweeps all 65536 opcodes and asserts none reports our gap;
        three `step_suite` tests corrected or superseded by it. Also settled:
        `CAS`/`CAS2` **do** execute — a stale comment said otherwise and this
        document had repeated it. Detail in `PROJECT_STATUS.md`.
  - [x] **The eight bit field instructions** — `BFTST`, `BFEXTU`, `BFCHG`,
        `BFEXTS`, `BFCLR`, `BFFFO`, `BFSET`, `BFINS`, 488 words. The field is a
        span in a big-endian bit stream, not a mask on a word: a 32-bit field at
        a non-zero offset crosses **five** bytes, a register-supplied offset is
        signed and reaches back before the base address, and in a data register
        the field **wraps** where in memory it does not. `step_suite` +4, each
        case computed by hand from the bit stream. Detail in
        `PROJECT_STATUS.md`.
  - [x] The **machine** resets through §8.1.1's sequence rather than a shorter
        one of its own, which had dropped the VBR, the CACR and the translation
        enables and added an ATC flush reset never performs. Invisible on a cold
        start — a zeroed struct already holds what reset writes — and wrong on
        every later one. `machine_suite` +1, checked against the old code as
        well as the new. Detail in `PROJECT_STATUS.md`.
- [x] **The MC68030's paged memory management unit.** Transparent
      translation, the translation control register and logical address
      decomposition, descriptor semantics, the address translation
      cache, `MMUSR` and the table walk.

  - [x] **Transparent translation (TT0/TT1)**
        (`src/core/cpu/m68030/ap_m68030_tt.c`), `[030]` §9.3 p. 9-16 and §9.7.3
        p. 9-57. On this machine's critical path rather than an optional extra:
        the boot PROM runs before any translation tree exists, and the TTx
        registers are how it reaches memory and I/O at all.
        *Verification: `tt_suite`, 15 tests — masked function-code and
        A31-A24 comparison, the 16 Mbyte minimum block, the manual's own
        `$00000000-$0FFFFFFF` worked example, read-only blocks not matching
        writes (which is what lets the tables still write-protect a range whose
        reads are transparent), either register matching being sufficient, CI
        ORed when both match, and a non-matching register not contributing its
        CI.*
  - [x] The read-modify-write rule, which is easy to get subtly wrong: with RWM
        clear, *neither* the read nor the write portion of an RMW cycle is
        transparently translated, and `[030]` stresses this holds "regardless of
        the function code and address bits". It overrides an otherwise perfect
        match rather than refining it, so it is checked before the address
        comparison and tested in both directions.
  - [x] **Closed: the TTx register bit layout, deferred and then transcribed.**
        Detail in `PROJECT_STATUS.md`.
        *Verification: `tt_suite`, 6 further tests — every field packing to its
        own bit checked individually, the unassigned bits 14-11/7/3 never set
        and ignored on unpack, the easily-inverted R/W and R/WM senses pinned in
        all three combinations ("1 = Only read accesses permitted", "1 = R/W
        field ignored"), a lossless round trip, and the manual's own
        `$00000000-$0FFFFFFF` worked example driven through a register *image*
        rather than a struct — the form `PMOVE` will deliver.*
  - [x] Fixed while doing it: `ap_m68030_tt.h` and `ap_m68030_walk.h` both
        defined `ap_m68030_access_t`, with different members. Any module using
        the tables *and* transparent translation — which is every real MMU —
        would not have compiled, and nothing caught it because no file included
        both. The walk's is now `ap_m68030_search_access_t`, and `walk_suite`
        includes both headers for no reason other than to make the build catch a
        recurrence.
  - [x] **Translation control register (TC) and logical address decomposition**
        (`src/core/cpu/m68030/ap_m68030_tc.c`), `[030]` §9.7.2 pp. 9-54 ff. TC
        is what turns a logical address into a path through the tree — how many
        high-order bits to ignore, how many index each of four levels, how many
        remain as page offset — so it is built before the walk it drives.
        *Verification: `tc_suite`, 15 tests — all eight documented page-size
        encodings and the reserved ones rejected, field decode, address split,
        and the consistency rule.*
  - [x] The consistency rule, which is the part worth getting exactly right:
        "The TIx fields are added together **until a zero field is reached**,
        and this sum is added to PS and IS. The total must be 32." A non-zero
        TIx *after* a zero one must not contribute — summing all four
        unconditionally would accept configurations the hardware rejects with an
        MMU configuration exception. Tested directly.
        Detail in `PROJECT_STATUS.md`.
  - [x] **Descriptor semantics and accumulated search state**
        (`src/core/cpu/m68030/ap_m68030_desc.c`), `[030]` §9.5.1.1 pp. 9-20 ff.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `desc_suite`, 23 tests — role resolution at both levels,
        the 4/8-byte next-table stride, upper and lower LIMIT with only its 15
        bits used, PAGE ADDRESS masking by `PS - 8`, WP/S/CI accumulation, WP
        being absolute against supervisor, and the full M-bit rule.*
  - [x] The M-bit rule, transcribed rather than simplified: set before a write
        to a page whose M is zero, **except** after a WP-set descriptor or a
        supervisor violation — and "an access is considered to be a write for
        updating purposes if either the R/W or RMC signal is low", so the read
        half of a read-modify-write already counts. Each clause has its own
        test, including that an already-modified page is not written again,
        which matters because that update costs a bus cycle.
  - [x] **Address translation cache** (`src/core/cpu/m68030/ap_m68030_atc.c`),
        `[030]` §9.4 pp. 9-17 ff. 22 entries, fully associative, with the tag
        (V, FC, 24-bit logical) and data (B, CI, WP, M, 24-bit physical)
        portions the manual specifies.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `atc_suite`, 17 tests — offset merged into the frame
        without translation, page size deciding how much of the tag is compared,
        FC as part of the tag, B faulting reads as well as writes, WP faulting
        only writes and read-modify-writes, PFLUSH of one entry leaving the
        rest, no duplicate tags, and all 22 entries used before any eviction.*
  - [x] The timing consequence that is easy to miss: **a write to a page that
        was previously only read is a hit that still costs a full table
        search**, because the cached entry has M clear. `[030]` §9.4 spells out
        why — it "assures that the first write operation to a page sets the M
        bit in both the ATC and the page descriptor ... even when a previous
        read operation to the page had created an entry for that page in the ATC
        with the M bit clear". It is its own lookup status rather than folded
        into a plain hit, so the cost cannot be silently lost.
  - [x] **`PROVISIONAL`: the ATC replacement algorithm**, narrowed from "no
        published rule" to a tie-break. `[030]` §9.4 names the ingredients and
        never states the rule; `MC68851 PMMU User's Manual` §5.2.1.3, describing
        the compatible ATC, supplies the half the 68030's own text omits — the
        history bit means "recently **used**", so a translating hit marks it
        where this core had only marked an insert. What remains provisional is
        the choice among valid entries once all are marked.
        *Verification: `atc_suite`; the row in `PROJECT_STATUS.md`'s PROVISIONAL
        table carries the reasoning, the sibling-manual quotation and the cost
        to close.*
  - [x] **MMU status register (`MMUSR`)**
        (`src/core/cpu/m68030/ap_m68030_mmusr.c`), `[030]` §9.7.4 pp. 9-59 f.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `mmusr_suite`, 17 tests — every field packing to its own
        bit checked individually so a transposition cannot hide, the unassigned
        bits never set, round-trip through pack/unpack, and each column of
        Table 9-3 separately: an absent ATC entry reporting I, a transparent
        match reporting T *alone*, L/S/N always clear after a level 0 probe, I
        set by a limit violation as well as by an invalid descriptor, and S
        depending on the probed function code rather than on the tree.*
  - [x] Separating B from I in the walk, which `MMUSR` forced and the ATC had
        hidden. §9.4's single B bit folds a bus error together with an invalid
        descriptor, but Table 9-3 reports them as different bits, so the walk
        now records the *cause* as well as the effect.
        Detail in `PROJECT_STATUS.md`.
  - [x] The table walk itself: fetch descriptors through the bus, apply
        `ap_m68030_desc`'s rules, and fill the ATC — the piece that joins the
        four MMU modules together, and the first whose *timing* is a table
        search rather than an instruction.
        *Verification: `walk_suite`, 40 tests. Detail in `PROJECT_STATUS.md`.*
- [x] **The MC68030's caches, and their half of the bus timing join.**
      Structure and policy, the data cache's write rules, the `CBREQ`
      decision, burst cycles, and what a miss costs end to end.

  - [x] **Cache structure and policy**
        (`src/core/cpu/m68030/ap_m68030_cache.c`), `[030]` §6. Both caches are
        "256-byte direct-mapped ... organized as 16 lines. Each line consists of
        four entries", with the tag holding **a valid bit per entry**, not per
        line — "each entry is independently replaceable". That one sentence
        shapes the module: per-line validity would make a burst fill and a
        single-entry fill indistinguishable, and those cost very different
        numbers of bus cycles. It would also make `CEI`/`CED` unimplementable.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `cache_suite`, 29 tests — including a single-entry fill
        leaving its three neighbours invalid against a burst validating all
        four, and a tag change invalidating the rest of the line.*
  - [x] The data cache's write rules, which are the easiest part to get
        backwards. It is **writethrough**: a write hit updates the entry "even
        if the cache is frozen", because freeze stops *replacement*, not
        updating. On a miss `WA` selects between "write cycles that miss do not
        alter the data cache contents" and allocation — and the manual's two
        allocation cases run into one sentence in the scan, so the boundary is
        reconstructed from the summary line that follows it ("an aligned
        long-word data write may replace a previously valid entry; whereas, a
        misaligned data write or a write of data that is not long word may
        invalidate a previously valid entry or entries"). Both halves are tested
        separately, including that a sub-long-word write miss *removes*
        information rather than adding it.
        Detail in `PROJECT_STATUS.md`.
  - [x] **The cache's half of the timing join: the `CBREQ` decision**, `[030]`
        §7.3.7. Whether a miss asks the memory system for a whole *line* rather
        than one long word, which is worth 5 clocks against 8 and so misprices a
        line fill if it is wrong even when the data ends up right. The manual
        gives two conditions and it is an **or**: the tag does not match, *or*
        "all four long words corresponding to the indexed tag ... are marked
        invalid". The second is the one easily left out — a line whose tag
        matches but whose entries were all cleared still bursts, and without it
        a cleared cache would refill an entry at a time and never burst at all.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `cache_suite`, 4 further tests — each condition on its
        own, the matching-tag-all-invalid case against its complement (one valid
        entry is enough to stop the burst), and all four suppressors checked
        against an access that would otherwise burst.*
  - [x] **The bus's half: burst cycles themselves.** A burst holds one cycle
        open across up to four long words — "The processor continues to accept
        data on every clock during which STERM is asserted until the burst is
        complete or an abnormal termination occurs" — so a line costs 2 clocks
        for the first long word and 1 for each of the next three.
        *Verification: `bus_suite`, 6 further tests. The headline pair counts
        both sides rather than asserting the ratio: a full burst line fill takes
        **5** clocks, and the same four long words fetched as four separate
        synchronous cycles take **8**. Also: a burst needs CBREQ, CBACK and STERM
        together, each missing on its own leaving an ordinary cycle; `CBREQ`
        negated after the third long word; and a bus error ending the fill
        short.*
  - [x] **What a miss costs, end to end** (`ap_m68030_cache_read`): a hit costs
        no external bus cycle at all and the MMU is "completely ignored"; a miss
        pays translation, the bus, and the fill. That join is what makes the
        clock emergent rather than tabulated. Detail in `PROJECT_STATUS.md`.
        *Verification: `cache_suite` asserts 5 clocks for a burst line fill, 2
        for a single long word and 0 for a hit, and a disabled cache paying 2 on
        every access.*
  - [x] **What a miss costs, end to end** — closed, and it had been done for
        some time without the item being ticked. `ap_m68030_cache_read` runs a
        cycle through `ap_m68030_bus` and *counts the ticks*, so a miss is
        priced by the bus state machine rather than by a constant: a burst line
        fill costs 5 clocks, a single long-word fill 2, and a hit 0. The step's
        clock flows from that, which is why `--time-instructions` shows an
        untranscribed instruction alternating 0/2 with prefetch alignment.
        *Verification: `cache_suite` asserts 5, 2 and 0 respectively, and a
        disabled cache paying 2 on every access. The self-timing probe on real
        hardware remains worth doing, but that is a **measurement** of the part
        rather than the modelling this item asked for.*
- [x] Data-dependent instruction timings published only as ranges are modelled
      at the documented value and marked `PROVISIONAL`. No invented point
      numbers. Detail in `PROJECT_STATUS.md`.
      *Verification: two rows in its PROVISIONAL table — the four divides that
      **are** transcribed at §11.6.8's published maxima, and the `+` rows that
      are **not**, which stay unpriced rather than priced wrongly.*
- [x] 68882 FPU.
      the same audit and are now in: §4.4's thirty-two tests are sixteen
      equations plus one bit, and `BSUN` is bit 4 against the NAN condition
      code with no special cases. Audited against this verification line after the
      transcendentals landed, which found that only round-to-nearest had ever
      been measured -- and closing that gap found §6.1.4's mode-dependent
      overflow result wrong across the whole core, plus a precision-dependent
      overflow threshold that was missing. Detail in `PROJECT_STATUS.md`.
      *Verification: probe suite over each operation and rounding
      mode; note the oracle's admitted FPU gaps as a divergence class.*
- [x] An enabled floating-point exception becomes a **trap**, which nothing
      delivered before: §6.1.9's priority picks the exception, Table 8-1's own
      ordering picks the vector, and §6.4.2 makes it a *pre-instruction*
      exception on the next non-exempt FP instruction — so it is not taken by
      the instruction that caused it, and `FMOVEM`/`FMOVE` control/`FSAVE`/
      `FRESTORE` do not report it. Found by sweeping for public functions the
      product never calls. `step_suite` +3. Detail in `PROJECT_STATUS.md`.
- [x] An oracle probe that drives an **enabled** floating-point trap:
      `fpu-trap` enables `DZ`, divides 1.0 by 0.0, and stores the frame's format
      word from the `FADD` that must not run. `$00C8` carries three claims at
      once — that it trapped, through vector 50, in a four-word frame. Ours
      returns it; MAME returns nothing, and its own `m68kfpu.cpp` raises no
      vector in 48–54 at all, so this is a recorded oracle-wrong difference
      rather than a measurement. `test_encoder` +3. Detail in
      `PROJECT_STATUS.md`.
- [x] **MC68882 floating-point coprocessor.** The programming model, the
      three binary real formats, the coprocessor interface registers,
      rounding, the four arithmetic operations, instruction decode, the
      operand transfers, `FMOVEM`, packed decimal both ways, `FSAVE`/
      `FRESTORE`'s state frames, and the transcendentals to §4.3.2's
      published bound.

  - [x] **The verification line is now met.** The audit found why it had
        the 68882 was not reachable from a running machine at all.**
        `ap_machine_init` never attached one, so `cpu->fpu` was null on every
        machine this core builds and every F-line instruction took the line 1111
        trap. That is also why no floating-point probe existed — there was
        nothing to probe.
        - [x] **The part is attached and two probes cover it.** `fpu` runs a ROM
              constant, an add and a store conversion; `fpu-transfer` runs both
              operand directions and an `FMOVEM` of the register file. Both
              appear in `probes.txt` with a state hash, and **no existing probe
              line changed**, so attaching the coprocessor perturbed nothing.
              Release and debug agree bit for bit.
        - [x] **The oracle comparison is running and has produced its first
              divergence class.** Six campaigns, `FINDINGS.md` C59-C64: the
              coprocessor was not attached at all (C59); the rounding mode is
              honoured and the constant ROM has its first external witness
              (C60); a double-precision comparison cannot separate two
              conforming transcendentals and an extended one can (C61, C62);
              five functions swept and adjudicated against 140-digit truth
              (C63); and the difference diagnosed (C64).
        - [x] **The transcendental difference is settled, and it is not a
              defect.** Four candidate sites were each eliminated -- the
              argument reduction bounded by arithmetic, the series and the
              `1 + expm1(r)` addition each compensated and measured, the final
              rounding exact by construction. Compensating any one leaves the
              total where it was, because every site loses a fraction of a unit
              and none loses a whole one: the ordinary behaviour of arithmetic
              done at the destination's own width, which §3.4 says the part
              avoids by carrying 67 bits. One unit in the last place is inside
              the accuracy suite's 3.1 ceiling and far inside §4.3.2's 64.
              Reclassified `sub-poll-slack` in `FINDINGS.md` C70, with the
              standing `PROVISIONAL` unchanged and its benefit now priced --
              along with the finding that no cheaper subset of it buys
              anything.
        - [x] **The sweep is widened and the divergence class is drawn.**
              Seven functions split along the line §4.3.2 itself draws: the
              bounded transcendentals, where every difference the campaign found
              lies, and the exactly specified operations, where there is none —
              `FSQRT` of 10 and `FINT` of pi both agree exactly on both sides
              against 140-digit truth. The sweep flags a differing
              exact-operation row as a defect, so the distinction is enforced
              rather than remembered.
              *Verification: `FINDINGS.md` C71. The class is one unit in the
              last place, transcendentals only, three of five at argument 1.0,
              oracle closer, cause understood, inside both the accuracy suite's
              ceiling and §4.3.2's bound.*
  - [x] **The programming model** (`src/core/cpu/m68882/ap_m68882_regs.c`),
        `[68881]` §2 and Figures 2-2 to 2-7: the three control registers, the
        eight extended-precision data registers, Table 2-1's condition codes and
        the five accrued-exception equations. Started here for the same reason
        the 68030 started with its registers -- it is the part that is pure
        transcription, so it can be got right before any arithmetic exists to
        get wrong.
        *Verification: `m68882_regs_suite`, 13 tests. Table 2-1 checked in full,
        all eight rows; the IEEE conditions swept over **every** combination of
        the four condition bits, including the eight the part never generates,
        because an `FMOVE` to the status register can write any of them; and
        each accrued equation on its own.*
  - [x] Three details that would each be wrong without ever faulting.
        **`AEXC(UNFL)` is an AND** where every other equation is an OR -- an
        underflow that was *exact* does not accrue, and the obvious OR produces
        a sticky bit set far too often. **`AEXC(INEX)` is set by `OVFL`**, an
        overflowed result being by definition not the exact one. And **reset
        leaves the data registers as NANs, not zeros**: a zeroed register reads
        as `+0`, which is a perfectly good operand, so a program that forgot to
        load one would produce plausible answers rather than propagating a NAN.
  - [x] **The three binary real formats** (`ap_m68882_format.c`), §3.2 and
        Figures 3-2 to 3-4: single, double and extended, the five data types,
        and the conversions. Not a convenience -- "since all FPCP internal
        operations are performed in extended precision, single and double
        precision operands are converted to extended precision values before the
        specified operation is performed", so these are on the path of every
        operand at those precisions.
        *Verification: `m68882_format_suite`, 12 tests, including 1.0 in each
        format to pin its bias, and single precision swept across exponents and
        fractions rather than sampled -- a shift written one place out survives
        some values and not others.*
  - [x] Four traps, each documented and each silent when wrong. **An extended
        exponent of zero is not always denormalized**: the manual's own NOTE
        says such a number "may have an explicit integer bit equal to one, which
        results in a normalized number", so the rule that holds for the other
        two formats misreads exactly what extended exists to hold. **An infinity
        is told from a NAN by the fraction with the integer bit ignored**, since
        real 68881 output sets that bit on infinities. **Signalling is the top
        *fraction* bit**, one below the integer bit -- reading bit 63 would call
        every NAN signalling. And **a denormal's true exponent is `1 - bias`**,
        not `0 - bias`, an error small enough to look like rounding.
  - [x] **The coprocessor interface registers** (`ap_m68882_cir.c`), §7.2 and
        Table 7-2 -- how the 68030 reaches the FPU at all. Not memory mapped:
        the decode "uses the MPU function codes (FC0-FC2), the CPU space type
        field (A16-A19), and the Cp-ID field (A13-A15)", and then A0-A4 selects
        the register. The type field is `0010`, next door to the `0000` this
        core already runs for `BKPT`.
        *Verification: `m68882_cir_suite`, 8 tests -- every one of the 32 select
        values classified; the don't-care bits exercised on both values, since
        decoding all five exactly would leave every odd address undecoded; the
        32-bit registers spanning four addresses each; and all three selectors
        required together, each shown to match something else on its own.*
  - [x] Two more transcription traps. **A read of a write-only register returns
        all ones**, not zero -- zero is a legal value for most of these, so a
        driver could not tell it from data. And **two registers in the map do
        not exist on this part**: Table 7-2's footnote excludes the operation
        word and operand address CIRs, "since they are not used by the MC68881",
        so a map transcribed without its footnote gives them storage.
  - [x] **Rounding** (`ap_m68882_round.c`), Figure 6-3's algorithm transcribed
        and §6.1.7's intermediate format. Its own module because every
        operation ends here: the add, the multiply, the divide and the
        transcendentals all produce a result "as if to produce infinite
        precision" and round it once, so this is the single place `INEX2` is
        raised and the four modes are interpreted.
        *Verification: `m68882_round_suite`, 11 tests, asserting the pseudocode's
        **branches** rather than a handful of results -- the exact case in all
        four modes, each directed mode on both signs, the tie, above and below
        the tie, the chop, and the carry.*
  - [x] Three properties that are the difference between an IEEE machine and an
        approximation. **Round to nearest is round half to *even*** -- a model
        rounding every tie up is wrong half the time on ties and biases a long
        summation, which is why the standard specifies the case at all. **The
        directed modes follow the sign, not the magnitude**, so rounding toward
        minus infinity makes a negative number larger. And **rounding happens
        once, from the full intermediate**: the bits below a single-precision
        boundary fold into that decision rather than being rounded away first,
        which is the classic double-rounding error and gives a different answer
        near a tie.
  - [x] **The four arithmetic operations** (`ap_m68882_arith.c`): add,
        subtract, multiply, divide and compare, on extended values, ending in
        the rounding stage. The special cases are the specification -- Table 6-2
        lists the combinations that are *errors*, and every other infinity or
        zero combination has a defined value, so a model treating all of them as
        errors traps where the hardware computes and one treating none of them
        as errors computes where it traps.
        *Verification: `m68882_arith_suite`, 20 tests -- Table 6-2's four rows
        for these operations, and **properties** rather than values where a
        value would not discriminate: addition and multiplication commutative
        over a set of operands, a value divided by itself being one, and
        multiply and divide inverting each other.*
  - [x] Three defects the property tests caught, none of which a single worked
        value would have. **The multiply's exponent was one low** -- two
        mantissas in [1,2) give a product in [1,4), so the unshifted case is the
        larger, and `3 * 4` came to 6. **The divide lost a quotient bit** when
        the dividend was smaller than the divisor, halving the answer. And
        **the alignment swap flipped both signs**, which made addition
        non-commutative for mixed-sign operands -- caught by the commutativity
        sweep and invisible to any single example.
  - [x] Two distinctions that are separate exceptions with separate vectors.
        **`DZ` is not `OPERR`**: `1/0` is *defined* as an infinity of the right
        sign and only `0/0` is an operand error, so folding them would trap the
        wrong handler on every division by zero. And **a signalling NAN raises
        once and comes out quiet**, since leaving it signalling would raise the
        exception again on every later operation -- one invalid operand becoming
        an exception per instruction for the rest of the calculation.
  - [x] **The instruction decode** (`ap_m68882_decode.c`), §4.7 and Tables 4-11
        and 4-13: the operation word's type field, the command word's opclass
        and register fields, and all forty-odd extension encodings. The 68030's
        F-line decoder gets as far as "a coprocessor instruction for cpID 1";
        everything past that is here.
        *Verification: `m68882_decode_suite`, 10 tests -- every one of the 128
        extension values classified, each type and opclass at its own encoding,
        and Table 4-13's operations listed so a transposed pair fails here
        rather than in a program's results.*
  - [x] Three encoding traps. **The reserved encodings are not all illegal**:
        footnote 3 lists nineteen that are "redundant with valid instructions
        ... and do not cause an F-line exception if executed", so there are
        three classes and not two, and a decoder with two traps on code the
        hardware runs. **`FSINCOS` occupies eight encodings**, `$30-$37`, its
        low three bits naming the second destination register -- taking only
        `$30` would F-line trap on seven eighths of its uses. And **whether the
        operation word's low six bits are an effective address depends on the
        *command* word**, with move constant the exception inside its own
        opclass: it reads the FPCP's ROM and touches no memory.
  - [x] **Wired to the 68030's F-line path** (`ap_m68882.c`), so an `FADD`
        actually runs. The part is a *pointer* on the CPU rather than a member,
        because fitted-or-not is a machine property: a DN3500 has a 68882 and a
        DN3000 does not, and the only thing software can see is that an F-line
        word otherwise takes the line 1111 emulator exception. Attaching one
        must not change that for a machine without one.
        *Verification: `step_suite`, 5 further tests (201 total) -- the trap
        with none fitted; the instruction executing with one, its source
        untouched and the program counter past **both** words; and a different
        cpID still trapping, since a machine may hold several coprocessors.*
  - [x] **The source operand transfer** (opclass `010`), so `FADD.S (A0),FP1`
        runs: all six binary formats from every data addressing mode, split the
        way §10.4.9 splits it between the part and the main processor. Made
        stack frame format `$9` reachable, which this plan had recorded as
        unreachable, and exposed a decode defect where Table 4-13's extension
        check was applied to fields that are not opcodes.
        *Verification: `step_suite` +8 (209), `m68882_format_suite` +6 (18).
        Detail in `PROJECT_STATUS.md`.*
  - [x] **The destination operand transfer** (opclass `011`), so
        load-compute-store runs end to end. Narrowing brings the rounding mode,
        three special-case tables and the exception byte with it; gradual
        underflow is implemented rather than flushed to zero. Exposed a live
        defect in `FINT`/`FINTRZ`, neither of which reported `INEX2`.
        *Verification: `m68882_store_suite` 11 tests, `step_suite` +5 (214).
        Detail in `PROJECT_STATUS.md`.*
  - [x] **`FMOVEM` of the data registers** (opclasses `110`/`111`) — a register
        list, not another transfer: no conversion, no FPSR effect, and a mask
        whose bit order reverses between predecrement and every other mode.
        *Verification: `m68882_decode_suite` +2 (12), `step_suite` +5 (219).
        Detail in `PROJECT_STATUS.md`.*
  - [x] **The system control registers** (opclasses `100`/`101`), where `FMOVE`
        of one and `FMOVEM` of several are the same encoding. Unimplemented bits
        read as zeros, the order is fixed, and the address register steps once
        rather than per register. **The FPIAR now tracks**, under §2.4's two
        conditions.
        *Verification: `step_suite` +5 (224). Detail in `PROJECT_STATUS.md`.*
  - [x] **`FMOVECR` and the constant ROM**, completing the general type: every
        general-type instruction now executes. The 22 published offsets, rounded
        to the FPCR's precision. The offsets are published and the values are
        not, so they are computed and correctly rounded — bit-exactness with a
        mask set is not settled, and the closing route is recorded.
        *Verification: `step_suite` +5 (229). Detail in `PROJECT_STATUS.md`.*
  - [x] **`FBcc` and `FNOP`**, the one instruction type whose coprocessor half
        was already done. Exposed a live defect: a conditional must not clear
        the exception byte, and `ap_m68882_condition` had been clearing it on
        every branch.
        *Verification: `step_suite` +5 (234). Detail in `PROJECT_STATUS.md`.*
  - [x] **Packed decimal in**, §3.6's decimal-to-binary conversion — correctly
        rounded to extended regardless of `PREC`, which §6.1.8 specifies as a
        *rounding* rather than a bound and which therefore needed exact
        multi-word integer arithmetic: `5^999` alone is 2322 bits. With Table
        3-4's type rows, the bit-for-bit NAN copy, and Note 2's undetected
        non-decimal digits.
        *Verification: `step_suite` +4 (241) and `m68882_format_suite`, against
        expectations computed to 400 decimal digits. Detail in
        `PROJECT_STATUS.md`.*
  - [x] **Packed decimal out**, with the k-factor — whose two halves run in
        opposite directions, `-64 to 0` counting digits right of the point and
        `+1 to +17` counting significant digits — plus `EXP3` for an exponent
        past 999 and the `OPERR` that comes with it, and `+18 to +63` raising
        *and* being "treated as +17".
        *Verification: `m68882_store_suite` +3 (13) with page 4-67's seven-row
        table reproduced character for character, and `step_suite` +1 (242)
        round-tripping a stored string back through the load conversion. Detail
        in `PROJECT_STATUS.md`.*
  - [x] **`FSAVE` and `FRESTORE`**, §6.4.2's state frames, which completes the
        68882: every instruction and every data format now executes. Null and
        idle frames, whose *length* differs and so is state rather than a
        constant; a null restore is a hardware reset while an idle one leaves
        the programmer's model alone; an unrecognised format word is the format
        exception rather than a protocol violation; both privileged. A **busy**
        frame is deliberately absent — this core's part never suspends, so
        nothing can generate one. Two `PROVISIONAL` figures, both rows in
        `PROJECT_STATUS.md`'s table: the version number, and the idle frame's
        internal words.
        *Verification: `step_suite` +4 (246). Detail in `PROJECT_STATUS.md`.*
  - [x] **`FDBcc`, `FScc` and `FTRAPcc`** — one instruction type, one encoding,
        told apart by Table 4-19. `FDBcc`'s branch base is a third rule (the
        displacement word's address) and its counter is a low-word decrement.
        **Table 4-19 has a defect**: it reserves the two absolute addressing
        encodings that `FScc`'s own page and the PRM both list as legal, and the
        reading is recorded with the reserved rows tested beside it.
        *Verification: `step_suite` +4 (238). Detail in `PROJECT_STATUS.md`.*
  - [x] **The exactly-specified monadic operations**: `FSQRT`, `FGETEXP`,
        `FGETMAN`, `FINT`, `FINTRZ` and `FSCALE`. §4.3.2 puts square root under
        the IEEE bound rather than with the transcendentals -- "except square
        root" -- so these have one right answer and are checked against it.
        *Verification: `m68882_arith_suite`, 7 further tests (27 total) --
        perfect squares coming back exactly across a sweep, since an
        off-by-one in the exponent halving survives some values and not others;
        `sqrt(-0)` being `-0` while any other negative is an operand error;
        `FINT` following the mode against `FINTRZ` truncating, on the manual's
        own 137.57 example; and `FSCALE` exact.*
  - [x] Two defects the exactness tests caught. **The square root halved its
        mantissa where it should have doubled the radicand**, so `sqrt(9)` came
        to 6 -- fixed by shifting the radicand into a 128-bit value and taking
        the exponent as `floor(e/2)`, which is right for both parities. And the
        128-bit arithmetic is written out rather than using `unsigned __int128`,
        which is a compiler extension: this core is C23 on three platforms and
        the emulated result must be identical on all of them.
  - [x] **The transcendentals, computed to §4.3.2's published bound.** All
        nineteen, worst case under 3.1 units in the last place against
        expectations generated to 120 decimal digits — twenty times inside the
        typical bound and three orders of magnitude inside the worst case.
        Nothing calls `libm`, so results are identical on every platform.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `m68882_transcendental_suite`, `m68882_accuracy_suite`
        and `m68882_arith_suite`, with the per-function error bounds recorded in
        the status document rather than here.*
  - [x] **The oracle diff is met** (`FINDINGS.md` C84): a probe runs identically
        on both implementations built as a DN3000, which needed the machine to
        take a model, the probe runner to pass it, and the harness to know more
        than one memory map — three obstacles found and fixed in turn. The boot
        half remains Phase 4's. Detail in `PROJECT_STATUS.md`.
        *Verification: `probe_compare.py --machine dn3000 --program all`.*
- [x] **MC68020 for the DN3000.** The part's own differences from the
      68030, expressed as a derived feature set in the one model table
      rather than conditionals scattered through subsystems.

  - [x] The part's own differences from the 68030, as a derived feature set in
        the one model table rather than conditionals in subsystems: its cache
        (256 bytes as **64 single-long-word entries**, not the 68030's sixteen
        four-long-word lines, tagged with A8-A31 *and FC2*, and no data cache at
        all), its asynchronous-only bus, and `CALLM`/`RTM` -- the two
        instructions the PRM marks "(MC68020)", which exist on no other part.
        Formats read from the page images of Figures D-1, D-2 and D-3; the
        extracted text of the module entry word had lost a column.
        `m68020_cache_suite` 16 tests, `m68020_module_suite` 17 tests,
        `model_suite` +5.
  - [x] The family wired into decode, so the features table changes behaviour
        rather than merely describing it: `ap_cpu_decode()` asks the shared
        decoder and upgrades `$06C0`-`$06FF` to a module call only where
        `has_module_calls` says so. A sweep of all 65536 opcodes asserts the two
        families differ on exactly 44 words -- 16 `RTM` and 28 legal `CALLM`
        forms -- and that every difference is the 68020 accepting what the
        68030 refuses, never the reverse. `m68020_decode_suite`, 8 tests.

        The item's stated verification, "`dn3000` boots", has moved to Phase 4:
        a boot needs a board, boards are Phase 3's subject and a first boot is
        Phase 4's. Nothing in the part's own work waits on it.
  - [x] What the 68030 refuses, it refuses by **taking vector 4** rather than
        stopping -- `[030]` §8.1.5. Narrow by design: only a word positively
        identified as another family member's removed instruction traps, so an
        unimplemented one still stops at the gap instead of impersonating a
        correctly-refusing machine. `step_suite` +1, and `module-call` becomes
        *not applicable* to a DN3500 in the oracle suite rather than a known
        difference. Detail in `PROJECT_STATUS.md`.
- [x] 68851 external PMMU as its own subsystem. *Verification: `MC68851 PMMU
      User's Manual 3ed` cited per figure; oracle diff.*
  - [x] The translation control registers: `TC` with its consistency check
        (IS + TIA + TIB + TIC + TID + PS must be exactly 32, and page size bit 3
        must be one), and the three root pointers `CRP`/`SRP`/`DRP` with the
        limit field's two directions, both documented ways to suppress it, the
        four descriptor types and the `FCL` interaction that a `DT = $1` page
        descriptor overrides. Figures 6-1 and 6-3 read from the page images.
        `m68851_tc_suite` 13 tests, `m68851_rp_suite` 13 tests.
  - [x] The six translation descriptor formats and Figure 5-10's type
        determination table. A descriptor does not know its own type or its own
        width -- both come from the search state and the *previous* descriptor's
        `DT` -- so the same bits are a table descriptor at one level and an
        indirect descriptor at the next. Figures 5-10 and 5-12 through 5-20 read
        from the page images. `m68851_descriptor_suite`, 21 tests.
  - [x] The status and protection registers: `PCSR`, `PSR`, `AC`, and the
        `CAL`/`VAL`/`SCC` trio the 68020's `CALLM` and `RTM` drive -- so this
        closes the seam with the module-call layer above. `SCC`'s rule is a
        range test over a bitmap ("any bit of SCC between n and m inclusive"),
        not a comparison, and `MDS = $0` invalidates every module descriptor
        rather than accepting any alignment. Figures 6-2 and 6-4 through 6-7
        from the page images. `m68851_regs_suite`, 22 tests.
  - [x] The address translation cache: 64 fully-associative entries, the
        three-part match rule (logical address above the page offset, function
        code exactly, and *either* the task alias or the entry's `SG`), the
        replacement order (invalid first, then pseudo-LRU among the unlocked),
        and the 63-entry lock ceiling that keeps one entry always replaceable.
        Modelled as named fields rather than a packed word: Figures 5-21 and
        5-22 give no bit numbers, because the ATC is not programmer-visible.
        `m68851_atc_suite`, 22 tests.
  - [x] The table search, transcribed from Figure 5-23 with Figure 5-26's limit
        check and the root pointer selection truth table. Walks and decides; it
        does not touch the bus, taking descriptor fetches through a caller
        supplied function so the cycle-stepped core can drive it one bus cycle
        at a time later without this logic changing. `m68851_search_suite`,
        21 tests.
  - [x] Instruction decode for `PLOAD`, `PFLUSH`/`PFLUSHA`/`PFLUSHS`, `PVALID`,
        `PMOVE` (all three formats) and `PTEST`, plus the function code
        specification field they share. Two things only a full read of Appendix
        A shows: the field is a *prefix code* (`00000` is the SFC form,
        `01000` is data register 0), and **opclass `001` carries four different
        instructions** -- all eight of its mode values are used, so a decoder
        built from `PFLUSH`'s page alone rejects `PLOAD` and both `PVALID`
        forms as undefined. An earlier commit did exactly that; this corrects
        it. `m68851_decode_suite`, 25 tests.
  - [x] The operation word's type field (the same six-type encoding the 68882
        uses on the same interface), the sixteen branch/set conditions, and the
        three `PSAVE` state frame sizes. `PSR` defines nine bits and only eight
        are testable -- **`M` has no condition**, and the encodings are
        contiguous from zero so there is no gap where a pair could sit.
  - [x] The coprocessor interface: Table 9-2's CIR map, Table 9-3's three null
        primitives and Table 9-6's five vectors. The two coprocessors implement
        **complementary** subsets -- the FPU has `$18` and not `$1C`, the MMU
        `$1C` and not `$18` -- so one shared CIR table would be wrong in both
        directions. `m68851_cir_suite`, 18 tests, one of which checks both
        parts' tables against each other.
  - [x] Wiring: the registers, ATC, search and decode into one part, with
        `ap_m68851_translate()` as the whole read path -- match the ATC, and on
        a miss walk and install, denials included. `PMOVE`'s side effects and
        `PFLUSH`'s execution land with it. `m68851_suite`, 19 tests. One
        approximation, `PROVISIONAL` in `PROJECT_STATUS.md`: §5.3's root
        pointer table is not implemented, so a `CRP` write flushes rather than
        replacing an alias -- a hit-rate difference, not a correctness one.
  - [x] `PTEST`, `PLOAD` and `PVALID` execution. `PTEST`'s level is a ceiling on
        the search, and level *zero* is a different operation rather than a
        shallow one -- it searches only the ATC and fetches nothing. `PVALID`
        traps when the operand is *less* than `VAL`, because lower means more
        privileged: the confused-deputy guard. `m68851_suite` grows to 31 tests.
        One `PROVISIONAL`: `PLOAD`'s write-back of `U` and `M` into the tables
        is not modelled.
  - [x] The breakpoint registers `BADx`/`BACx` and the acknowledge cycle they
        answer -- the other half of the mechanism whose CPU side landed in
        Phase 2. A count of *n* fires *n* times and then bus-errors, and reset
        "clears the BPE bit; the skip count field is not", so a reset does not
        silently rearm every breakpoint. `m68851_suite` grows to 38 tests.

        As with the 68020 item, the "`dn3000` boots" verification has moved to
        Phase 4, where the board it needs is now an item of its own.
- [x] 68040 for DN5500: different pipeline, caches, and MMU descriptor format;
      integrated FPU. *Verification: `MC68040 User's Manual 1993` cited;
      `dn5500` oracle diff, expecting to exceed the oracle's FPU coverage.*
      **Tail, measured by the firmware sweep**: the *part* is modelled and
      nothing executes on it. There is no decode or step under
      `src/core/cpu/m68040/`, and `ap_machine` builds an `ap_m68030_cpu_t`
      whatever the model row says — so `5500_BOOT_A1631-80046` stops at its
      **second instruction**, `cinva #$3`, with the single line-F exception that
      names it. Detail in `PROJECT_STATUS.md`.
  - [x] The MMU descriptor formats, Figures 3-11 and 3-12 from the page images.
        A different MMU rather than a wider one: every descriptor is 32 bits so
        nothing depends on the previous one's width, the tree is fixed at three
        levels, and the page size varies the *address field width* instead of
        the format. The type fields free different bits -- `UDT` its low one,
        `PDT` its high one and only for the resident case -- so masking the same
        bit in both would turn every indirect descriptor into an invalid one.
        `m68040_descriptor_suite`, 15 tests.
  - [x] The MMU registers: `URP`/`SRP`, `TCR`, the four `TTR`s and `MMUSR`,
        Figures 3-3 through 3-6 from the page images. Three rules *contradict*
        the 68851 -- writing `TCR` does not flush the ATCs, `PFLUSH` works with
        translation disabled, and reset leaves the page size untouched while
        clearing the enable. `m68040_regs_suite`, 16 tests.
  - [x] The instruction and data caches: 64 sets of four 16-byte lines, four-way
        set associative, **physically** tagged, with a dirty bit *per long word*
        in the data cache. The "pseudo-random" replacement is a 2-bit counter
        per cache and entirely deterministic. `m68040_cache_suite`, 17 tests.
  - [x] The two ATCs: 16 sets of four ways each, tagged with `FC2` alone and no
        task alias -- `G` is the 68040's substitute, overriding a nonglobal
        flush rather than being one more criterion. The manual states the tag
        width two contradictory ways in one sentence; sixteen is derived from
        its own geometry and recorded in `PROJECT_STATUS.md`.
        `m68040_atc_suite`, 16 tests.
  - [x] The table search: three fixed levels indexed by `RI`/`PI`/`PGI`, one
        indirection at most, and protection accumulating down the tree. The
        manual states the geometry twice -- as field widths in Figure 3-8 and as
        concatenation identities in §3.2.1 -- and the tests check the two
        against each other rather than each against my reading.
        `m68040_search_suite`, 15 tests.
  - [x] The integrated FPU's *subset*: Table 9-10's unimplemented list, the
        three-way classification of an F-line word, and the shared vector whose
        only discriminator is the stack frame format. Table 9-10 omits `FLOG2`
        and Appendix E's Table E-2 proves it should be there -- a defect
        resolved without leaving the manual. `m68040_fpu_suite`, 12 tests.
  - [x] The floating-point programming model, which is the 68882's -- §9.1 says
        so outright and `m68040_fp_model_suite` is the evidence, checking the
        68040 manual's own encodings against the modules already in the core
        rather than duplicating them. 7 tests.
  - [x] The pipeline's timing *composition*, §10.1 and Table 10-2: three stages
        priced separately, a fetch stage the tables omit and floor at one clock,
        and an execute time given as lead-plus-base with an interlock that
        charges only the stall beyond the lead. A different shape from the
        68030's Equations 11-1 and 11-2, so the tables cannot be read as if it
        were. `m68040_timing_suite`, 14 tests.
  - [x] §10.5's miscellaneous integer table, 75 rows over 44 instructions, from
        the page images -- with each figure marked exact, *minimum* or
        *typical* per the section's own notes. Extraction renders `MOVEQ` as
        `MOVEa`, which would have handed `MOVE` the wrong instruction's timing.
        `m68040_misc_timing_suite`, 15 tests.
  - [x] §10.3's `CINV` and `CPUSH` timing, which are formulae rather than
        numbers: `Idle` depends on the preceding instruction stream and `Line`
        on the user's memory, and for `CPUSH` the manual refuses an equation
        outright. Each best case turns out to be its own worst-case formula at
        the cheapest line transfer, which checks both rows exactly.
        `m68040_cache_timing_suite`, 10 tests.
  - [x] §10.4's `MOVE` cross product, 15 source modes against 12 destinations,
        180 cells from the page images. Generated from the figures as read so a
        misplaced row cannot shift a column silently, and checked by structural
        properties as well as spot values -- the eight unindexed source rows are
        identical for every complex destination, which is exactly where a slip
        would show. `m68040_move_timing_suite`, 13 tests.
  - [x] §10.6's integer unit tables, pages 10-13 to 10-28: 46 column groups
        over 71 mnemonics and 17 addressing modes, with a dash modelled as
        *invalid* rather than zero so a decoder cannot price an encoding it
        should reject. The section drove five model shapes -- a second figure
        under four different selectors, conditional penalties as a tagged
        list, three confidence classes, a cost that *replaces* rather than
        adds, and figures depending on the register list -- and produced nine
        suspect entries, all transcribed as printed with a test stating each
        contradiction. Two of the nine were resolved rather than recorded: the
        `MOVES` non-monotonicity turned out to be licensed by its *typical*
        marking, and `ADDA`/`SUBA`'s disagreement was characterised by the
        68030 manual printing the two identical. *Verification:
        `m68040_iu_timing_suite`, 99 tests. Detail in `PROJECT_STATUS.md`.*
  - [x] §10.7's floating-point timings, pages 10-29 to 10-37, in two modules.
        §10.7.1 and §10.7.2 price the *integer unit's* support -- address
        formation and operand transfer with an idle FPU, so `FDIV` and `FNEG`
        share a column -- across seven tables whose mode enum had to become
        the union of every page's rows. §10.7.3 prices what the FPU then does,
        in a unit of its own: the **half cycle**, since `FDIV` executes in
        37.5 and `FMOVE` converts in 1.5. Each of its stages carries a latency
        *and* an occupancy, and a row is chosen partly by the operands'
        *values*, so a zero or NAN short-circuits both later stages. Four more
        suspect entries, all kept as printed. *Verification:
        `m68040_fpu_timing_suite` (32 tests) and `m68040_fp_pipeline_suite`
        (18 tests). Detail in `PROJECT_STATUS.md`.*

## Phase 3 — Core board


- [x] **Give the machine a model.** `ap_machine_init_model` takes one and
      `ap_machine_init` is that with the DN3500, so every existing caller kept
      the machine it had. The row is consulted rather than merely held: the step
      carries `has_module_calls`, so a DN3000 executes `CALLM` where a DN3500
      refuses it, and the processor's rate is the table's `cpu_hz` — which
      nothing in the core had read, leaving every machine keeping no time at
      all. There is now no way to set a rate that is not the model's. The
      coprocessor needed no gating and the sub-item saying it did was wrong:
      every model in the table has one. Detail in `PROJECT_STATUS.md`.
      *Verification: `step_suite` +1 (247) — `$06C0` reported `ILLEGAL` on a
      DN3500 and `UNIMPLEMENTED` on a DN3000, reached through a machine rather
      than through the decoder. `machine_suite` — every model's rate against the
      table, and a DN2500 taking exactly 25:20 of a DN3500's time over identical
      cycles. Every hash in `tests/goldens/probes.txt` moved and no other column
      did: a probe run had been keeping no time.*
- [x] Memory bus with one shared arbitration point, so contention is emergent.
      *Verification: measured, and against itself rather than the oracle —
      MAME's 68000 family models no bus arbitration at all, so no second master
      there could ever take a cycle to time. The same program costs 18 clocks on
      an idle bus and 86 with a DMA channel running, 720 ns against 3440 ns at
      25 MHz, and an idle board costs it nothing at all. Nothing computes the
      difference: the processor is `[030]` §7.7's lowest-priority claimant and
      loses the arbitration.*
  - [x] The processor's side of the protocol: `[030]` §7.7's BR/BG/BGACK state
        machine, with the processor at the lowest priority. Both documented
        deferrals are in — a grant waits for a committed bus cycle to begin, and
        a locked read-modify-write refuses one outright, which is what will make
        `CAS` indivisible against a DMA controller. `arb_suite`, 15 tests.
  - [x] The shared arbitration point itself, `board/ap_arbiter.c`: the
        external priority encoder §7.7 requires the board to supply, with
        `008778-03` §2.4.6's order — "DRQO having the highest priority and DRQ7
        having the lowest" — and the processor beneath all of them.
        Detail in `PROJECT_STATUS.md`.
  - [x] **The route an I/O adapter takes into the arbiter**, `008778-03`
        §2.4.7's first method: DRQx to a channel programmed in cascade mode,
        DACKx.L from the board once the arbitration is won, then MASTER.L, and
        ownership until *both* signals are released. `board/ap_master.c`.
        Neither route needed transfers after all — acquisition is a handshake,
        and the transfer is the DMA controllers' item. The second route, the
        Series 4000 Master Request Register, is now a **recorded gap**: the
        manual never names the bit, the register is absent from the oracle
        (C10), and all that is known is that the PROM clears it at reset.
        Detail and cost to close in `PROJECT_STATUS.md`.
        *Verification: `master_suite`, 10 tests, one per clause. The one that
        changed the model's story: two adapters on one controller are ordered
        by the **channel** encoder, not the arbiter's line order — a controller
        has one request output, so the AT's DRQ0-highest order is what the
        cascaded pair implements rather than an encoder above them.*
  - [x] **The termination's arrival clock.** A device can now lengthen its own
        cycle: the memory system declares its wait states through one callback
        on the access context, and termination is withheld from the bus rather
        than added to a total afterwards, so the state machine counts them.
        `NULL` means the minimum, which is what §11's tables assume, so no
        existing figure moved. Detail in `PROJECT_STATUS.md`.
        *Verification: `cache_suite` +1 across six cases, single and burst at 0,
        1 and 3 wait states — the burst paying them once, since its four beats
        are one cycle held open.*
    - [x] **A board that actually declares them**, and `008778-03` said
          otherwise where it could: Appendix A (Series 3000) and Appendix B
          (Series 4000) give the AT bus cycle times, so the two AT windows now
          answer in hundreds of nanoseconds where the minimum is eighty. The
          board answers a *duration* and the machine converts it, so the
          wait-state count is written down nowhere — a DN3500 pays 19 clocks
          and a DN3000 pays 9 for the same card. Regions with no published
          figure still answer at the minimum, and that is now a statement
          rather than a default. One `PROVISIONAL`, in the table: the DS3500 is
          in neither appendix, so the board keeps the Series 3000 set and the
          bracket is one bus clock on a memory read. Detail in
          `PROJECT_STATUS.md`.
          *Verification: `atbus_suite`, 8 tests. The transcription checks
          itself: the two appendices are the same bus at two clock rates, so
          every figure but the memory read reduces to the same number of bus
          clocks in both, asserted to 0.05 of a clock.*
  - [x] The synchroniser stays `PROVISIONAL` at two clocks, but the figure is
        now **bounded by the manufacturer's own measurement** rather than by the
        user manual's prose. `MC68030EC/D` p. 7, parameter 35, gives `BR`
        asserted to `BG` asserted as **1.5 to 3.5 clocks**, frequency
        independent; our three clocks sit inside it and `arb_suite` asserts so.
        The document was not on disk and the web step found it.
        **The recorded closing route was impossible**: MAME's 68000 family
        models no bus arbitration at all, so no oracle measurement could ever
        have been taken. What remains is sub-clock phase, which nothing
        clock-stepped represents. Detail in `PROJECT_STATUS.md`.
        *Verification: `arb_suite` +1 — grant latency inside parameter 35's
        envelope, which a change to the synchroniser could leave.*
- [x] Address translation map (`0x017000`), CPU status/control, cache control,
      task alias, master request, latch-page-on-parity-error registers.
      *Verification: the oracle diff is `FINDINGS.md` C10 — `regprobe.lua`
      drove every bit of all six registers in both directions, with two
      addresses from gaps in Table 2-8 as the control that established what
      unmapped looks like. Four are characterised, and the two that match the
      control exactly are declined rather than modelled. The map itself cites
      `019411-A00` §4.2.1.4 per figure.*
      **This was satisfied before it was ticked**, which is the drift the phase
      boundary re-read exists to catch and did not: C10 ran, the children were
      ticked one by one, and nobody went back to the parent.
      `tools/check_docs.py` now fails a parent whose children are all done and
      which neither ticks nor says what it is waiting for.
  - [x] The address translation map itself: the translation for both DMA
        widths, the entry format, and the register file. `atmap_suite`,
        15 tests. The source that settles it is `019411-A00` §4.2.1.4, an
        addendum that *replaces* pages 4-10 and 4-11 of the handbook and is the
        only one naming the DS3500 — the base manual describes the map as a
        Series 4000 feature and would have left our reference machine without
        one.
  - [x] **Both open questions closed, one answered and one instrumented.** The
        entry count is the manual's: §4.2.1.4 gives 64 for 8-bit DMA and 128 for
        16-bit, so our 128 is right and the oracle's `& 0x3ff` — 1024 entries
        spanning the whole 2 KB — is over-permissive, classified as
        hardware-truer on our side. What the region decodes to beyond the
        entries has **no answer available**: the addendum, the technical
        reference and the web are all silent, and the oracle cannot witness it
        because its own decode is the one already ruled wrong. The aliasing
        assumption stays, and is now counted rather than silent — the board
        records reads and writes to the undescribed seven eighths, with the
        first address of each. Detail in `PROJECT_STATUS.md`.
        *Verification: `board_suite` +2 (16) — an access inside the entries is
        not counted and one past them is, and the first address recorded stays
        the first. `atmap_suite` still pins the two figures that do not divide.*
  - [x] **Characterised** by measurement, since no manual here lays these out:
        `008778-03` Table 2-8 gives addresses only, and the handbook carrying
        the bit layouts is not in `docs/references/`. `tools/mame-oracle/regprobe.lua`
        drives every bit in both directions; `FINDINGS.md` C10 has the table.
        Detail in `PROJECT_STATUS.md`.
  - [x] The four registers measurement covers: CPU status (bit 15 stuck,
        write-clears the latched bits), CPU control and latch-page-on-parity
        (16 bits of storage each), cache control (a mirrored byte, one writable
        bit), each aliased across its 256-byte range. `boardreg_suite`,
        12 tests. Storage and width only — what the bits *mean* is still
        unknown, and **nothing may be built that depends on a meaning**.
  - [x] **Unblocked by the disassembly the item asked for, done across all five
        boot PROMs.** The master request register is referenced 29 times in the
        DS3500, DS4500 and DS5500 images and **not once** in either Series 3000
        one — §2.4.7's "In the Series 4000, an alternate method of bus
        arbitration exists" confirmed from the firmware, and the DS3500 placed
        in the same model set `019411-A00` gives the translation map to. Every
        site is a **byte write** of `$00`, `$02`, `$08` or `$40`, and **none is
        a read** — so the read-back value, the one thing that could not be
        measured, is something no firmware in hand depends on. Task alias is at
        no absolute address in any image and a 400,000-instruction boot never
        touches it: that one needs the architecture handbook and only the
        handbook. Both stay declined, and both are now counted, so a run says
        which it touched. Detail in `PROJECT_STATUS.md`.
        *Verification: `board_suite` +1 (17) — the two counted apart, aliased
        across their 256-byte ranges, and a modelled register not counted.*
- [x] Two 8259 interrupt controllers and the Apollo interrupt vector scheme.
      *Verification: the ordering is driven by two **devices** and nothing is
      asserted by hand — the interval timer reaches terminal count on its own
      and the DUART raises TxRDY, and the timer at master IR0 is serviced before
      the SIO at IR1, each on its own vector. `machine_suite`. Not diffed
      against the oracle: the second source only exists because the tick loop
      does, and MAME advances devices on a different schedule entirely, so a
      side-by-side ordering diff would be comparing two quantisations rather
      than two priority encoders. The encoders themselves are pinned against
      `[8259]` twelve ways in `intr_suite`.*
  - [x] **The route to that verification exists at last: probes can run on a
        board.** `--probe-file` takes `board 1` and builds a whole core board —
        no boot PROM, since `ap_board_init` needs none and a probe is
        side-loaded precisely so no firmware runs. The probe loads at the
        model's RAM base, which is where the oracle's loads, so both sides run
        the same addresses. Detail in `PROJECT_STATUS.md`.
        *Verification: the same probe reading a DMA register runs in 3
        instructions with no bus error on a board, and on flat RAM takes 25 bus
        errors and never terminates — the device range simply is not there.
        This unblocks the device verification lines for interrupts, DMA, the
        timers and the SIO, all of which needed a machine with devices in it.*
  - [x] **And the route is now travelled: `probe_compare.py` runs board probes
        against the oracle.** A board probe must be *self-contained* — this
        side's devices are at reset and the oracle's have been booting for three
        emulated seconds — so it re-initialises the part and reads back only
        what it wrote. Both sides load at the model's RAM base, so the **program
        counter is compared**, which no earlier probe could do.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `intr-mask` drives `ICW1`-`ICW4` into both controllers,
        writes `5A` and `A5` to the two `OCW1`s and reads both back: `00005AA5`,
        17 instructions and the same PC on both sides. `dma-register` pins the
        8237A's byte-pointer flip-flop the same way at `00003412`. Both are in
        `--program all`, now 17 programs, and `probe_encoder` +16 (47) pins the
        four new opcodes and the addresses that can be wrong while the program
        still runs cleanly.*
  - [x] The 8259A itself, with no Apollo in it: initialization sequence, all
        three operation command words, fully nested priority with both
        rotations, edge and level triggering, special mask mode, special fully
        nested mode, poll, automatic EOI, and the spurious level 7.
        Detail in `PROJECT_STATUS.md`.
  - [x] The Apollo pairing at `011000` and `011100`, cascaded on **IR3** —
        not IR2 — with vector bases `A0` and `A8` giving the sixteen levels the
        contiguous range `A0`-`AF`. `intr_suite`, 12 tests.
  - [x] **Settled, and it was our assumption that was wrong.** The 8259A's
        initialization words cannot be read back, so
        `tools/mame-oracle/writetrace.lua` watched the boot PROM write them:
        master ICW3 `08`, slave ICW3 `03`, agreeing on IR3. With the cascade
        there, Table 2-3 is plain fixed priority and carries no anomaly at all.
        Detail in `PROJECT_STATUS.md`.
  - [x] **The CPU interrupt level is 6**, measured. Neither manual states it,
        and an idle boot cannot show it because nothing requests — so the
        interval timer was started by hand to make something request, and a
        single write of the CPU's mask swept: taken at mask 5, blocked at mask
        6, so the level is 6. Reproduced twice at each bracketing mask and
        confirmed by the master's ISR. `FINDINGS.md` C12.
  - [x] **Answered, and it is both — by level.** `008778-03` §3.2 said the
        acknowledge is a CPU-space cycle, and the oracle confirms it: MAME's
        `dn3500` installs a `cpu_space_map` covering `FFFFFFF2-FFFFFFFF`, which
        is CPU space and therefore invisible to a program-space tap. That is why
        no read ever appeared on the controller's range.
        Detail in `PROJECT_STATUS.md`.
  - [x] **The ordering a program sees**, which is not the ordering
        `intr_suite` pins: two lines standing at once, resolved through the
        board's sampling, the CPU's level, the acknowledge, the EOI a handler
        owes and the second interrupt that only arrives because the first
        finished. The SIO at master IR1 is serviced before the disk's slave
        input 6 — losing on the cascade's position at IR3, not on its number —
        each on its own vector, `A1` then `AE`. Detail in `PROJECT_STATUS.md`.
        *Verification: `machine_suite` +1 (36). It found that `ICW1` clears the
        request register, so a line asserted before the controllers are
        programmed is wiped and, being edge-triggered, never returns.*
  - [x] **The subsystem is reachable at last, which it never was.** Five
        devices carried an IRQ accessor and a line constant and the board wired
        none of them; the CPU's `interrupt_level` is a caller's field and no
        caller drove it, so every controller test passed on a machine that
        could not take an interrupt. The board now samples its devices' lines
        and answers the level and the vector; the machine samples before each
        instruction. It also makes the parent's verification runnable — the
        DUART raises its line from two register writes with no time passing.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `machine_suite` +2 (33) — a program that programs both
        controllers and unmasks TxRDY is interrupted onto vector `A1`, told
        apart from the level-6 autovector by a second handler at vector 30; and
        the same program on unprogrammed controllers delivers nothing while the
        device is still asking.*
- [x] Two AT DMA controllers. *Verification: a cartridge's bytes reach main
      memory by DMA on Table 2-4's DRQ1, translated through the address map,
      with nothing counted unwired — the drive put them on the bus. And the
      request line gates a **block**: a channel armed for 4096 bytes against a
      1024-byte cartridge moves 1024 and stops, count nowhere near terminal and
      the processor given the bus back because the device let go of it rather
      than because the count ran out. The disk's two data ports move under an
      acknowledge on DRQ2 and DRQ7; neither has a request line, because
      `ap_omti.h` models the register sets and not the command sets, so there is
      no condition to derive one from. `dma_suite`, 16 tests.*
  - [x] Placement measured before writing anything: DMA 1 at `010C00` **stride
        1** (sixteen registers aliased every sixteen bytes, the all-mask
        register reading `0F` as a reset part holds), DMA 2 at `010D00`
        **stride 2**. The part is confirmed an 8237A by its byte-pointer
        flip-flop: two bytes written to an address register, the low one read
        back first. `FINDINGS.md` C13. Intel's datasheet is now in
        `docs/references/intel/`.
  - [x] The 8237A's programming model, entire: all sixteen register addresses,
        four channels with base and current registers, the single shared
        first/last flip-flop, command/mode/request/mask/status/temporary,
        master clear, autoinitialise reload and mask-on-terminal-count.
        Detail in `PROJECT_STATUS.md`.
  - [x] **The part's transfer cycle**, now that the arbitration point it was
        blocked on exists. A service cycle moves a byte either way, verifies
        without moving one, walks the address up or down, and ends on the
        borrow out of zero — "the number of transfers is one more than the
        number programmed", which a model ending at zero gets wrong every
        time. Cascade and illegal modes run nothing, and memory-to-memory is
        refused rather than half-run. Detail in `PROJECT_STATUS.md`.
        *Verification: `i8237_suite` +8 (26) — a count of 3 moving four bytes,
        both directions checked by where the byte landed, autoinitialise
        reloading and staying armed, and three modes that transfer nothing.*
  - [x] **The board driving it.** One arbitration point on the board, each
        controller's single HRQ into it, a transfer while it holds the bus, and
        the address composed through the translation map. The peripheral side
        is not wired — which device sits on which channel is unmeasured — so a
        read or write transfer is counted and reads all ones, while a *verify*
        transfer needs no device and runs normally, which is what lets the
        contention measurement be taken before that is settled.
        **It found a defect only a transfer could**: the board wrote both
        halves of a 16-bit map entry with the whole byte, truncating every page
        number above `00FF`, so a transfer aimed at `01000000` landed in the
        boot PROM at zero. Detail in `PROJECT_STATUS.md`.
        *Verification: `dma_suite` +4 (10) — the processor loses the bus and
        gets it back at terminal count, a transfer lands where the map points,
        and a verify transfer is not an unwired one. `atmap_suite` +1 (16) pins
        the byte lanes directly.*
  - [x] **The machine stalls for it, and the contention is measured.** The bus
        advances at the processor's rate and the processor does not run while
        another master holds it — no penalty is computed, it simply loses the
        arbitration. Eight `NOP`s and a `STOP` cost 18 clocks on an idle bus and
        86 with a channel running: 68 lost to 64 transfers, 720 ns against
        3440 ns at 25 MHz. Block mode holds the bus to terminal count, which is
        the "block granularity, not per word" this item asks about.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `machine_suite` +2 (35) — the same program on two boards
        differing only in whether a channel runs, asserted as a bracket rather
        than a figure since the handshake's exact cost is the synchroniser's;
        and an idle board costing the identical program exactly nothing.*
  - [x] Both controllers wired into the board at `010C00` (stride 1) and
        `010D00` (stride 2). The cascade the module used to refuse is
        `008778-03` §2.4's — "DRQ4 is used on the system board to cascade
        Channels 0 through 3" — and Table 2-4 gives every channel's device, the
        tape confirmed twice over by Table 8-1's jumper configuration. The
        board wires the cascade rather than encoding the priority, so Table
        2-4's order emerges. Detail in `PROJECT_STATUS.md`.
        *Verification: `dma_suite` +2 (12) — a controller-1 channel beating a
        lower-numbered channel on controller 2, and nothing on controller 1
        reaching the bus at all until the cascade is programmed.*
  - [x] And the shared arbitration point it was pointing at — Phase 3's first
        item, which had been waiting for a second bus master to exist. Each was
        the other's blocker and neither was blocked: the arbiter needed a master
        to arbitrate for, the transfers needed a bus to arbitrate over, and both
        landed the moment either did.
- [x] Interval timer and calendar. *Verification: both, from different places.
      The timer is measured against the **machine's own clock** — 201 pulses of
      250 kHz, which the CPU's clock count independently agrees is 20,100 of
      its own, overshooting by under one instruction and never undershooting.
      And a fortnight of one-second carries lands on the right date across a
      month end, a common and a leap February, a year end and the 400-year
      rule. `machine_suite`, `mc146818_suite`.*
      The "14-day calendar interval hazard noted in the MAME driver" this line
      used to cite **does not exist**; the substance behind it is kept and
      tested. Detail in `PROJECT_STATUS.md`.
  - [x] The MC6840 itself, 16-bit continuous mode: register model, byte
        buffering, status register, prescaler, gate, and every documented way of
        clearing an interrupt. `mc6840_suite`, 23 tests. The three Apollo input
        rates (250/125/62.5 kHz, `008778-03` §3.8) all divide `AP_TIME_BASE_HZ`
        exactly, so the device needs no change to the time base — asserted in
        the suite so a future change to either side breaks a test.
  - [x] The remaining counting modes: single shot and dual eight-bit, both
        transcribed from `[6840]` §§3.7.2-3.8.2 and implemented. This also fixed
        a real defect — the mode field is *not* three contiguous bits, and
        reading it as one had been declining `XX0000XX`, which is half of
        continuous mode.
  - [x] **Period and pulse-width measurement are declined, and that is a
        decision rather than a gap.** Both time a signal applied to a timer's
        gate pin and this board connects nothing to any of the three, so the
        modes cannot be exercised, observed or tested. They are decoded and
        reported, so a caller learns which mode it asked for. Reopens only if a
        model appears whose board wires a gate, which is a Phase 7 question.
        Was marked "In progress", which read as work outstanding above text
        saying it was not.
  - [x] The timer's placement and interrupt route, measured: the part is at
        **odd addresses, stride 2** (`RS n` at `010801 + 2n`, confirmed by the
        `FFFF` latch default showing through), and its interrupt reaches the
        master controller as **IRQ0**, confirming Table 2-3. `FINDINGS.md` C12.
  - [x] The timer wired into the board at `010800`: odd-address decode, the
        three input rates as exact clock domains, and the IRQ0 route through to
        vector `A0`. `timer_suite`, 8 tests. Advancing is by whole pulses with
        the remainder carried, so the rate does not depend on polling
        frequency.
  - [x] The MC146818A itself: clock bytes, registers, RAM, the update cycle
        with a full Gregorian carry (century rule included, since the part's
        two-digit year cannot distinguish 1900 from 2000), the alarm with
        `11XXXXXX` don't-care codes, and Register C's read-to-clear.
        Detail in `PROJECT_STATUS.md`.
  - [x] The calendar wired into the board at `010900`: stride 1 and byte
        consecutive (measured — the timer beside it is odd-address stride 2, so
        neither could be inferred from the other), sixty-four registers aliased
        through the range (also measured), and the IRQ8 route through to vector
        `A8`. `calendar_suite`, 5 tests.
  - [x] `[146818]` Table 5 transcribed, and the periodic interrupt implemented
        for the nine rates that divide the time base (512 Hz to 2 Hz). The six
        fastest are **refused, not rounded**: the rates are 32768/2^n Hz and
        `AP_TIME_BASE_HZ` carries only 2^9.
  - [x] **Decided: not now.** The cost of admitting the six fast periodic rates
        is measured rather than guessed — x64 on the time base, dropping the
        representable span from 88.6 years to 505 days. The rates are refused
        rather than rounded, so anything reaching for one fails loudly, and the
        recomputation is a mechanical change whenever something is seen doing
        so. The part's own 4.194304 MHz crystal is a separate matter and can
        never be a clock domain at all: it would leave 3.95 days.
  - [x] ~~**The square-wave output and `DSE` are declined for the same
        reason**~~ **-- superseded: both are now implemented.** The original
        reasoning was that nothing on this board is wired to the square-wave
        pin, which is a fact about the board and not about the part.
        Historical:
        nothing on this board is wired to the square-wave pin, and `DSE`'s
        daylight-savings shifts are stored but inert. Implementing either would
        add behaviour no test could distinguish from its absence.
  - [x] **A long calendar interval, carried at every boundary it can cross.**
        Fourteen days is 1,209,600 one-second carries and any of them can be
        the wrong one, so the fortnight is walked from a July date across the
        month end, from February in a common year and a leap year, across a
        year end, and across the 400-year rule that the part's two-digit year
        cannot decide for itself. And the same fortnight reached in ragged
        sub-second steps agrees with it reached in one, which is the property a
        polling driver and a fast mode both depend on.
        *Verification: `mc146818_suite` +3 (29).*
- [x] SIO serial lines, keyboard and mouse. *Verification: **met**. The machine
      emits `CR LF "MD7C REV 8.00, 1989/08/16.17:23:52" CR LF '>'` on serial 1
      channel B, character for character the oracle's captured sign-on and
      prompt — the `CR`s included, which MAME's stdio device strips and its own
      register tap confirmed. The comparison is in `docs/references/MD.md`.
      It needed seven defects in this core fixed, none of them in the serial
      code, and the pacing `MD.md` had already prescribed: a character cannot be
      delivered faster than the wire carries it. Detail in `PROJECT_STATUS.md`.*

  - [x] Placement measured: both ports at `010400` and `010500`, **stride 2**,
        sixteen registers over thirty-two bytes. `FINDINGS.md` C14. The DUART
        manual is already in `docs/references/motorola/`.
  - [x] The 2681/68681's programming model: all sixteen register addresses,
        both channels with their FIFOs and mode-register pointer, the
        counter/timer with its two address-triggered commands, the interrupt
        registers, and the input and output ports. `mc68681_suite`, 15 tests.
  - [x] **Serial framing, the whole of the item's original list**: baud rates,
        start and stop bits, parity, and the automatic echo and loopback modes.
        A character arrives with only as many bits as the link carries, a rate
        or parity mismatch sets its own status bit and the byte still enters the
        FIFO, and all four channel modes act — auto-echo delivering *and*
        retransmitting where remote loopback retransmits and does not deliver,
        which is the only thing separating them. The keyboard is written and
        wired with it: `device/ap_kbd.c` reports the key that moved, make and
        break, on serial 1 channel A. Detail in `PROJECT_STATUS.md`.
        Stop bits are decoded rather than **timed**, which needs the tick loop.
        *Verification: `mc68681_suite` 15 → 29 across framing, parity and the
        four modes; `sio_suite`; `kbd_suite`, 5; `board_suite`, 2. Framing gave
        a fact about the machine, not only a model: `MR1` resets to a five-bit
        link, so a release code cannot arrive until the firmware programs
        eight.*
  - [x] Both ports wired into the board at `010400` and `010500`, stride 2,
        sharing IRQ1. §3.9's memory-refresh period is pinned at exactly 99000
        base units — a figure whose *frequency* is not an integer, so it is the
        second case (after the interval timer's prescaled rate) that a core
        counting in hertz could not represent at all. `sio_suite`, 6 tests.
  - [x] **The memory refresh runs.** The DUART's counter is clocked at X1 and
        produces §3.9's 15 microsecond square wave on OP3 from the boot PROM's
        own preload. X1 is in no manual here: it is derived from two facts that
        agree at one rate only — §3.9's period, and the firmware's measured
        preload of 27 with `ACR E0` — giving 3.6 MHz, which forced the time base
        from 6.6 GHz to 19.8. It caught a defect in the part, the counter having
        tested for zero before decrementing. Detail in `PROJECT_STATUS.md`.
        *Verification: `sio_suite` +4 (14) — the derivation checked against both
        facts it rests on, the square wave inverting each half period and
        returning each whole one, the ready bit at half that rate, and ragged
        advances reaching the same place as one.*

- [x] **The tick loop.** `CLAUDE.md` opens with "one `tick()` per machine
      cycle, every subsystem advancing inside it", and this core had none: a
      counter reached terminal count only if a test reached in and advanced it.
      Every device on this board that keeps time now advances against the
      machine's absolute `now` — the interval timer, the calendar, and the
      DUART's counter driving the memory refresh.
      *Verification: `machine_suite` — a `BRA` loop that touches nothing brings
      the interval timer to terminal count on its own, and two machines running
      different programs to the same instant leave their timers reading the
      same status. The processor is still stepped by instruction; that is the
      literal reading of "per machine cycle" and it has moved to Phase 8, where
      the identity harness that a run-loop rewrite needs is built.*
  - [x] **The devices that keep time now advance**, to the machine's absolute
        `now`, after every instruction. Advancing per instruction reaches the
        state advancing per clock would, because each device is a function of
        the instant and carries its own remainder — pinned by two machines
        running *different* programs to the same instant and leaving their
        timers reading the same status. What is quantised is when a change is
        *noticed*, bounded by the longest instruction against a 250 kHz fastest
        rate. A boardless machine advances nothing, so no probe golden moved.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `machine_suite` +3 (39) — a `BRA` loop that touches
        nothing brings the interval timer to terminal count on its own.*
  - [x] **The DUART's counter/timer advances too**, so the last device on this
        board that kept time and could not is driven from the tick. It needed a
        clock domain the time base could not represent, which is why it landed
        with the memory refresh rather than with the first two. Detail in
        `PROJECT_STATUS.md`.

- [x] Node ID PROM (`0x011200`), including node ID taken from the logical volume
      label. *Verification: the reader takes the node from the creator UID at
      block 0 `+0x48` — twenty bits, the split confirmed three ways — and the
      PROM built from it presents that identifier across registers 0-3.
      `volume_suite`, 5 tests; `nodeid_suite` +1 (7). Run against all eleven
      `.awd` images, which agree, and against the boot PROM, which is correctly
      refused. `lcnode` under Domain/OS is Phase 9's content testing and is the
      confirmation this cannot give itself.*
  - [x] The PROM itself: layout measured, identifier big-endian in registers
        0-3, checksum in register 14 confirmed arithmetically. Stride 2 with the
        odd byte reading zero — *not* the serial ports' arrangement at the same
        stride, which reads every value twice. `nodeid_suite`, 6 tests.
  - [x] **The identifier comes from the volume label.** `image/ap_volume.c`
        reads block 0's creator UID and `--volume` gives it to the board, so a
        machine takes its identity from its disk rather than from a constant.
        Refuses a file that is not a Domain volume — the magic is in block 1 at
        `+0x18` — because a node invented from an arbitrary file makes a machine
        lie about itself and every object its file system creates carries it.
        Detail in `PROJECT_STATUS.md`.
  - [x] **Answered: only the identifier.** The oracle's `apollo_ni::call_load`
        computes `data[2] + data[4] + data[6]` and compares it against
        `data[30]`. Three bytes summed, not sixteen, and a **sum** rather than
        an exclusive-or.
        Detail in `PROJECT_STATUS.md`.
## Phase 4 — Storage, then a first boot

- [x] **The OMTI takes access time, and `00120020` is closed.** This core set
      `IREQ` in the instant a command was issued; Domain/OS requires that it
      does not, and MAME carries the same fix with the comment "Domain/OS
      doesn't expect zero access time". With zero latency one of the boot's
      `READ DATA TO BUFFER` interrupts landed inside the kernel's page-fault
      handler and the kernel crashed. Now `finish` sets a deadline built from
      the drive's published seek, half-turn and transfer figures, and
      `ap_omti_advance` retires it from the board tick.
      **`PROVISIONAL`: which drive**, not the arithmetic -- the XT-4380E is
      inferred from capacity and era. The data phase is still undelayed, named
      as an approximation with its cost. Detail in `PROJECT_STATUS.md`.
      *Verification: the boot no longer crashes in the handler -- the screen
      reaches `Domain/OS kernel(7)` and then `Unable to resolve
      "/sys/node_data" -- E0007`, where it used to print `CRASH_STATUS
      00120020  PC 3C40E114` with no message. `awd_suite` +4, each checked to
      fail against the old zero-time behaviour; 135/135 green.*

- [x] **A DN3000 core board, and `dn3000` boots.** The board holds a *map* per
      model now — Table 2-6's DS3000 space against Table 2-8's — because the
      difference is not a shift: the device block moves from `010000` to
      `008000` and the DMA, interrupt and node-ID placements move again within
      it. Device modules are untouched, the map carrying a canonical address
      beside each base. Two things the boot found: the **DMA page register** at
      `009200`, which replaces the translation map and which the firmware writes
      before anything else, and §1.3's rule that a Series 3000 **ignores address
      bits 27-31** — the PROM writes `08000000` thirty-eight thousand times.
      Detail in `PROJECT_STATUS.md`.
      *Verification: 500,000 instructions with zero bus errors and zero unmapped
      accesses, and the console emits `MD8 REV 7.0, 1988/08/16.15:14:39` — its
      own PROM's string at `0008DA`. Diffed against the oracle: `mdsession.py
      --machine dn3000` gives the same banner and prompt, byte for byte once
      MAME's stripped `CR`s are accounted for. `board_suite` +6 (23).*

- [x] **OMTI 8621 ESDI/floppy controller** — one controller for both, and the
      DN3500's. Both halves complete: §5's fixed disk over `.awd`, §6's floppy
      over `.afd`, wired to the board on IRQ14 and IRQ6.
  - [x] The register model for both halves, from `[OMTI]` Tables 4-1, 4-2 and
        4-3: the fixed disk's four ports and the floppy's five, modelled as two
        independent sets sharing nothing. Both measured dumps are reproduced as
        tests. `omti_suite`, 9 tests.
  - [x] §5's fixed-disk command set transcribed (`FINDINGS.md` C27): the 6-byte
        CDB with its cylinder split across three bytes, the twenty-four common
        commands, and the ESDI-specific three — noting that `0C INITIALIZE DRIVE
        CHARACTERISTICS` is ST506-only and must *not* be accepted here.
  - [x] The CDB decode, `device/ap_omti_cdb.c`: field extraction with the
        three-byte cylinder, and ESDI acceptance that refuses the ST506-only
        command. `omti_cdb_suite`, 7 tests.
  - [x] **The fixed disk's data commands, with a drive behind them.**
        `image/ap_awd.c` is the raw sector image -- 1056 bytes a sector, the
        oracle's constant and §5.4.14's own table -- and the geometry is the
        drive's, since nothing in the file says. The controller has §5.1.1's
        command phase now: `READ`, `WRITE`, `REQUEST SENSE`, `TEST DRIVE READY`,
        `RECALIBRATE`, `SEEK`, with anything accepted-but-unimplemented failing
        rather than reporting success. Detail in `PROJECT_STATUS.md`.
        *Verification: `awd_suite`, 11 tests. The one that earned its place:
        §5.1.2's block count of zero means **256**, and storing it back in a
        byte turned the largest transfer into none — asking 256 sectors of a
        sixteen-sector drive must read all sixteen and then fail.*
        `media/` has eleven `.awd` images; the claim that it had none was stale.
  - [x] **§6's floppy command set, and the `.afd` image under it.**
        `image/ap_afd.c` is the diskette: one geometry only, 77×2×8 at 1024
        bytes, so a file of another size is refused rather than reinterpreted.
        The controller has a second, wholly separate command phase for all ten
        of §6.3's commands plus INVALID, with their ST0–ST3 result bytes.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `afd_suite`, 26 tests. §6 is a scan, so the opcodes are
        a page-image read; the sibling 8640 manual's §5.3 has a text layer and
        independently confirms the same eleven commands.*
        **There is no `WRITE DATA`** in either manual's floppy set — only
        FORMAT A TRACK puts data on the medium — so `05` takes the INVALID
        path rather than a command invented from generic 765 knowledge.
  - [x] Both halves wired into the board at `04D000` and `05F800`, each
        aliased through 1 KB on its own period, on IRQ14 and IRQ6. The 74 KB gap
        is asserted as the window's arithmetic rather than as two constants, so
        the *rule* is what is pinned. `disk_suite`, 6 tests.
        Detail in `PROJECT_STATUS.md`.
      sets** running concurrently, not one controller with a mode bit. *Verification: DMA-completion device shape — transfer now,
      schedule completion in emulated time; oracle diff.*
      **Corrected at the Phase 4 boundary.** This item used to read "Winchester
      controllers: OMTI (DN3000), WD7000 ESDI and SCSI (DN4500)", which left the
      reference machine unaccounted for and split a single device in two. The
      oracle's slot list gives the DN3500 `wdc  OMTI 8621 ESDI/floppy controller
      (Apollo)`, with the floppy drives hanging off it at `isa1:wdc:omti_fdc:0`,
      and `roms/firmware` carries the matching `3000_OMTI_8621_102640-B.bin`.
      The WD7000 is the DN4500's and belongs with the other model variants.
      `FINDINGS.md` C15.
- [x] **Booting from the cartridge, and the machine that had to exist first.** The SC-499's register model and the boot record, then everything `--boot-tape` turned out to need: the address map, the PROM boot, the missing opcodes, the bus-error investigation.
  - [x] The controller's register model, from `[SC499]` §1.9: all four
        addresses, the derived interrupt flag, the tri-stated IRQ line, and
        RSTDMA's documented identity with power-on reset — which makes the two
        testable against each other directly. `sc499_suite`, 9 tests.
  - [x] The boot record's header **confirmed**, not merely inferred: the first
        instruction is a PC-relative `LEA` that computes word 0 exactly when
        executed at word 1, so word 0 is the load address and word 1 the entry
        point (`FINDINGS.md` C24). `ap_ct_boot_image` names the fields
        accordingly and validates the image against the cartridge's size.
  - [x] `--boot-tape <cartridge>` loads the image and runs it. The SR10.3.5
        boot cartridge executes **16,933 instructions** before faulting,
        deterministically across runs and build types. Not a boot — flat RAM, a
        chosen stack, no devices mapped — but the first real firmware to run
        here, and a number to drive upward.
  - [x] Located the fault (`FINDINGS.md` C28): the final PC is **outside the
        allocated RAM**, and 5634 bus errors precede it — all above the flat
        RAM's top, since the register addresses fall *inside* flat-from-zero
        memory and read as zeros. The firmware reaches high, repeatedly, then
        jumps somewhere unmapped.
  - [x] The address map itself, `board/ap_board.c`: every device placed by
        Table 2-8, main memory at `1000000`, unmapped reported rather than
        answered, and every region named. `board_suite`, 7 tests.
  - [x] `ap_machine` routed through it, optionally — flat RAM stays the default
        so the probes keep the harness they want. `--boot-tape` uses the map.
  - [x] **Settled: the boot image's addresses are logical.** The oracle reads
        `FF` at `0013D800` and at `00120000`, main memory at `01000000`, and
        `TC = 0` — so the address is unmapped physically, this core's map is
        right, and the zero is correct behaviour. Whatever loads the image
        enables translation and maps the range first (`FINDINGS.md` C28).
  - [x] Boot through the PROM: `--boot-prom` loads it at zero and runs from the
        reset vector. Twenty instructions, **zero bus errors, zero unmapped
        accesses** (`FINDINGS.md` C29).
  - [x] `ORI`/`ANDI`/`EORI` to `SR` and `CCR` implemented — all six were
        missing together. The PROM goes from 20 instructions to **35**.
        Detail in `PROJECT_STATUS.md`.
  - [x] Investigated the `000028D0` stop, and it was **neither** `CMPI` nor
        `(d16,An)`: the step was reporting a **bus fault as an unimplemented
        instruction**. Executors signal both with a bare `false`, and the caller
        turned both into `UNIMPLEMENTED` — blaming the CPU for the memory
        system's answer, and pointing the investigation at a decoder that was
        correct. `access_faulted` now carries the distinction from the access to
        the status. `step_suite`, 3 new tests: the fault case, the control that
        the same instruction over memory that answers executes, and the stale
        flag not leaking into the next instruction. The PROM reports `FAULT` at
        the same PC and **the instruction count does not move** — the fix
        changes the diagnosis, not the machine (`FINDINGS.md` C30).
  - [x] Confirmed against the oracle, and the derivation was **wrong** even
        though the conclusion was right (`FINDINGS.md` C31). The firmware is
        probing for a display controller, but these are Apollo's own
        natively-mapped controllers, not PC MDA/CGA through the AT window: the
        ranges are `0x408` bytes, which no `0x80`-strided AT port can be, and
        `000A0000` is Apollo's colour graphics memory rather than a PC frame
        buffer. The "three independent facts" that made the window reading feel
        safe were three consequences of one design decision.
  - [x] **Display controller identification**, `board/ap_graphics.c` — the two
        register blocks (`05D800-05DC07` monochrome, `05E800-05EC07` colour) and
        the device ID register at offset 1 of each. The four screen types are
        `C4P=8`, `19I=9`, `C8P=10`, `15I=11`, which is exactly what the boot PROM
        compares against at `000028D0` onward. `graphics_suite`, 6 tests.
        Detail in `PROJECT_STATUS.md`.
  - [x] The boot PROM reaches **425 instructions**, up from 89, with the display
        probe answered.
  - [x] `00090000` is **AT bus memory**, not unmapped. The oracle's map gives
        `ATBUS_IO 040000-05FFFF` and `ATBUS_MEMORY 080000-FFFFFF`, and both
        windows are decoded by the *board*: an address with no card behind it
        reads `FF` and terminates normally. The PROM **jumps into** AT bus
        memory at `00090000`, almost certainly scanning for an expansion ROM, so
        a board that faulted on an empty window turns "found nothing" into a
        crash. Same lesson as the display controller, found the same way — the
        map, first. `board_suite`, 2 tests, including one that the windows do
        not swallow the tape, disk and display controller sitting inside them.
  - [x] **The line 1010 and line 1111 emulator traps** (vectors 10 and 11) are
        raised. Both were defined, classified by `ap_m68030_opcode.c`, and never
        taken — the step reported them `UNIMPLEMENTED`, which said the gap was
        ours when taking the trap *is* the complete behaviour. No `A000-AFFF`
        word is an instruction on any member of the family; the range exists to
        be trapped. `step_suite`, 3 tests.
        Detail in `PROJECT_STATUS.md`.
  - [x] Main memory's *name* now stops where its address space does.
        Detail in `PROJECT_STATUS.md`.
  - [x] `--boot-limit N` on the headless frontend, to stop a boot short. Without
        it the only observable is the end state, and an end state cannot say
        which instruction produced it — a wild PC looks identical however far
        back the mistake was made.
  - [x] Localised the `FFFF060E` stop by bisecting on that flag. The PC leaves
        the PROM on instruction **2788**, at `00002502`, which is an `RTS`. The
        subroutine is five instructions:
        ```
        0024F6  2F08       MOVE.L  A0,-(A7)
        0024F8  4E7A 8801  MOVEC   VBR,A0
        0024FC  0810 0007  BTST    #7,(A0)
        002500  205F       MOVEA.L (A7)+,A0
        002502  4E75       RTS
        ```
        Each advances the PC by exactly its own length, so nothing is
        mis-decoded, and the push and pop balance. **The return address on the
        stack is therefore already wrong when this subroutine is entered** — the
        `RTS` is where the damage becomes visible, not where it happens.
  - [x] `--boot-trace`, reporting **PC and A7** per step. A7 is the observable
        it exists for: a wrong PC is where damage becomes visible, a stack
        pointer that stops matching the call depth is where it happens, and here
        the two were 2788 instructions apart.
  - [x] **Found it, and my "not another absent device" call was wrong.** The
        trace shows `CLR.B $00011600` bus erroring on every pass through the
        PROM's reset path, each fault draining a frame off a 384-byte supervisor
        stack until A7 descended past `01000000` and the `RTS` popped garbage.
        Detail in `PROJECT_STATUS.md`.
  - [x] Raised the limit. **5000000 instructions, zero bus errors, zero unmapped
        accesses, zero empty-slot accesses.** The machine is executing real
        firmware cleanly through its own address map for as long as we let it.
  - [x] Settled the `000006B4` spin by counting rather than reading: 15 loop
        entries against 7399 `DBF` executions in 20000 instructions, so ~493 per
        entry. **`DBF` terminates correctly** and the spin is above it.
  - [x] **Found it, and it retracts the "5000000 clean instructions" reading.**
        Detail in `PROJECT_STATUS.md`.
  - [x] Tested the A6/stack-overlap hypothesis by tracing A6, and **it is
        refuted**. A6 is `01000180`, set deliberately by a `LEA` at step 8, and
        the firmware indexes it with *positive* offsets while the supervisor
        stack grows *down* from the same address. They do not overlap — that is
        the design. Both earlier suspicions that the reset SSP was too small
        were also wrong: `01000180` is a data-area base as much as a stack top.
  - [x] The real shape, from the same trace: step 10 is `JMP` (not `BSR`) to
        `00000646`, so nothing is pushed. RAM starts zeroed, and the slot the
        bad `RTS` reads at `01000172` **was never written by anything**. Control
        reached an `RTS` without a matching `BSR`.
  - [x] Not a branch either. Walking the 46 steps shows every `BSR`/`RTS` pair
        balancing and the control flow following the PROM exactly. Step 30's
        `BSR.W` pushes return address `00000620` to `01000172`; step 57's `RTS`
        reads `01000172` and gets zero. Nothing between them writes there — the
        `(d16,A6)` stores all land at `010002B0` and above.
  - [x] **Refuted: misaligned long access is fine.** Step 18 is
        `MOVE.W SR,-(A7)`, a word push, so every later long push and pop sits at
        2 mod 4 and the failing read at `01000172` spans the lines at
        `01000170` and `01000174`. Tested directly rather than through the PROM:
        write a long at 2 mod 4, read it back, sweep 4 KB of other addresses to
        disturb the cache, read again. It round-trips. The test is kept as a
        regression test — misaligned data is legal on this part and the path is
        now covered either way.
  - [x] `--boot-watch ADDR`, reporting a location's contents per step, and it
        settles it. **The memory is correct.** `01000172` holds `00000620` from
        step 30 straight through step 57. The `RTS` read the right address, the
        right value was there, and it jumped to zero anyway.
  - [x] **Reproduced and half fixed.** A write hit on a *resident* line works;
        a **misaligned long write spanning two entries** did not. The hit path
        did `line->entry[entry] = value` regardless of alignment, so one entry
        got a whole long word assembled from the wrong bytes and the other was
        left stale. The manual's rule for the miss case says "the valid bit(s)
        are cleared" — **plural**, because this is the access that touches two —
        and the same applies on a hit: a cache entry is a whole long word, so
        there is no way to write half of `value` into each. Both are now
        invalidated, which costs a refill and cannot return a wrong value
        because writethrough has already reached memory.
  - [x] **Fixed, and the read path was never the problem.** `operand_write`
        splits at long-word boundaries, so a misaligned long arrives at the
        cache as two *partial* writes — and the hit path stored a partial
        `value` into a four-byte entry, replacing the bytes it had not written
        along with the ones it had. The entry held neither the old value nor the
        new one. The written bytes are now merged into their own lanes,
        big-endian, and the rest of the entry is left alone.
        Detail in `PROJECT_STATUS.md`.
  - [x] Characterised the 129 bus errors, and **they are the PROM's self-test**
        (`FINDINGS.md` C33). MAME's `apollo_unmapped_r` calls
        `apollo_bus_error()`, so an unmapped read really does fault on a DN3500
        and ours matches; and its source names `00030000` as the "Bus error test
        address in DN3500 boot prom and self_test". The firmware provokes them
        deliberately. A machine taking *no* bus errors here would be the
        suspicious one, and driving the count to zero would be chasing the
        wrong target.
  - [x] `ap_board_t::first_unmapped_read` / `first_unmapped_write`, reported by
        the headless frontend. A count cannot distinguish a self-test from a
        defect; an address can. The first unmapped read is `FFF90000`, high
        space, in the range MAME leaves commented out as `apollo_f8_r/w`.
  - [x] Characterised `FFF90000`, and **our behaviour matches the oracle**.
        Detail in `PROJECT_STATUS.md`.
  - [x] **Found out why, and it is decisive: the commented-out handler does not
        raise a bus error.** `apollo_f8_r` returns `FFFFFFFF`; the catch-all it
        falls through to returns the same value *and* faults. The firmware
        probes `F8000000`-`FFFFFFFF` to discover whether an FPA is fitted and
        **the fault is the negative answer**, so the quiet handler would make a
        machine with no accelerator report one. The oracle confirms it from the
        other side: its `fff90000` clause omits only the *logging* of the "FPA
        trial access" and keeps the bus error. Nothing is decoded there, and the
        space is deliberately not given a region name — that would be the first
        step toward answering. Detail in `PROJECT_STATUS.md`.
        *Verification: `board_suite`, 2 tests — the space unmapped on **both**
        models, and the trial access faulting on read and on write. The DN3000
        gets there differently: its top five address bits are ignored, so the
        probe folds to `07F90000`, still above anything it decodes.*

### The PROM now needs time to pass

- [x] **The console: which device, which channel, and what it draws on.** The
      display is the console and the keyboard is its input, both established
      against the oracle and then implemented — Table 12-1's codes on serial 1
      channel A. The drawing engine those writes specify is the display
      controller's own item.

  - [x] The PROM reaches `000007AE` and stays there at 300000, 1000000 and
        3000000 instructions, with the fault count settled at 129. The
        instruction is `BTST #0,($102,A0)` followed by `BEQ` back to `0000078E`
        — a **status-poll loop**, waiting for a device bit to set.
  - [x] **Identified, and it is not time — it is input.** `A0` is `00010401`,
        so the polled address is `00010503`. That is **SIO2**, and
        `ap_sio_decode` shifts the offset right by one, giving register 1 —
        the MC68681's **status register A**, whose bit 0 is **RxRDY**. The
        firmware is waiting for a character on the second serial port.
        Detail in `PROJECT_STATUS.md`.
  - [x] `--boot-input TEXT`, delivering a byte sequence to SIO2 channel A as the
        firmware takes each one. `ap_sio_receive` and `ap_sio_receiver_ready`
        expose the DUART's receiver through the board; the bytes are decided
        before the run starts, so determinism is untouched — a `getchar()` here
        would have ended the reason this frontend exists.
        Detail in `PROJECT_STATUS.md`.
  - [x] Established: it is a **console read loop**, and it is responsive. The
        loop polls *both* ports — `BTST #0,($2,A0)` is SIO1's status register A
        and `BTST #0,($102,A0)` is SIO2's — so the firmware waits for a
        character on either. It consumes what is delivered and returns to the
        poll: `""`, `"\n"`, `"EX\n"` and `"H\n"` each leave the PC at a
        different point *inside* the same loop, which is where the limit fell
        rather than a new stop.
  - [x] `ap_sio_transmit` and `--boot-console`: the machine's own console byte
        stream, drained from both ports and both channels every step and written
        straight to stdout. `sio_suite` covers the path in both directions,
        through the registers a program actually writes and reads.
        Detail in `PROJECT_STATUS.md`.
  - [x] **And the PROM is silent** — 300000 instructions, nothing transmitted on
        either port or channel. That is now a fact about the firmware, and the
        next question. Possibilities, none yet established:
        - it polls for a console character *before* announcing itself, and the
          announcement is behind the branch we never take;
        - it has decided neither port is the console, having found something it
          did not expect while probing;
        - its console is not a DUART port at all on this configuration.
        Detail in `PROJECT_STATUS.md`.
  - [x] Per-region access counts on the board, reported by the frontend. The
        completion of C33: a count of *failures* cannot answer "what did the
        firmware want", because the interesting case is usually a device that
        answered and was not what the firmware hoped for. A region with **zero**
        accesses is as informative as one with thousands.
  - [x] It answers the silence. Over 300000 instructions:
        `serial 250244 reads / 38 writes`. The firmware configured both DUARTs
        — 38 writes is mode, clock-select and command registers for four
        channels — and then polls. **It never transmits.** So the first of the
        three possibilities holds: it waits for a console character before
        announcing itself.
  - [x] The table also shows what the firmware has *not* touched, which is the
        half a total cannot give: **no timer and no calendar accesses at all**,
        and the interrupt controllers written 10 times but never read.
        Detail in `PROJECT_STATUS.md`.
  - [x] `--boot-input-port N`, because the poll tests *both* DUARTs and branches
        differently for each — which port carries the console is a question the
        firmware answers, not one to assume. Every run until now fed SIO2, which
        was the port whose poll happened to be traced first.
  - [x] **SIO1 is the console.** Feeding a newline to port 1 instead of port 2
        moves the PROM from the poll loop to `0000220C`, an entirely new region,
        and the serial region goes from 38 writes to **11839**. Main memory
        writes go from 43328 to 177894 and core register writes from 7 to 2368.
        Detail in `PROJECT_STATUS.md`.
  - [x] **Settled: the PROM never transmits.** Per-register write counts show
        register 3 and register 11 — the two transmit buffers — with **zero
        writes on both ports**. Our capture was correct and there was nothing to
        capture. The `tx_enabled` mechanism that could have swallowed the output
        exists but is not what is happening, which is why it was recorded as a
        thing to test rather than believed.
  - [x] What the 11839 writes actually are: `sio1 reg 9` 4723 times,
        `sio1 reg 4` 2362, `sio2 reg 1` 2362, `sio2 reg 4` 2361 — the auxiliary
        control and clock-select registers, hammered, with the counter/timer
        preload registers written once each. That is the shape of something
        driving the DUART's **counter/timer**, which in this model never
        advances because nothing ticks.
        Detail in `PROJECT_STATUS.md`.
  - [x] **Refuted: it is not the counter/timer.** Per-register *read* counts —
        which matter more than writes on this part, since reading register 14
        starts the counter and 15 stops it — show the counter registers 6 and 7
        with **zero reads on both ports**, and register 14 read twice. Nothing
        is driving a timer. The inference from write volume alone was wrong, and
        was recorded as a reading to confirm for exactly this reason.
  - [x] What it *is*: a **write-only loop**. `sio1 reg 9` 4723 writes and
        `sio1 reg 4` 2362 — almost exactly 2:1, so one iteration writes the
        auxiliary control register once and clock-select B twice, about 2362
        times — with **no reads at all** on those registers. A loop that writes
        and never reads is not testing anything it can see.
        Detail in `PROJECT_STATUS.md`.
  - [x] Read the loop at `0000220C`. It is three instructions —
        `CMP.B (d8,PC,Xn),D1`, `BEQ`, `DBF` — a **table search**, comparing the
        received byte against a table at `000021D2`. The table is
        `CB DB FB C8 D8 F8 C9 D9 F9 5B 5D 7B 7D CA DA FA ... 0D 0D 1B 5C ...`:
        high-bit bytes interleaved with ASCII, which is the signature of a
        **keyboard scan-code to character map**, not a command table.
  - [x] **Confirmed by the oracle, and it names the channel.** MAME's DN3500:
        `m_keyboard->tx_cb().set(m_sio, apollo_sio::rx_a_w)` and
        `stdio.tx_cb().set(m_sio, apollo_sio::rx_b_w)`. So serial 1 **channel A
        is the keyboard** and **channel B is a terminal** — the reading was
        right, and the part it could not have given is that ASCII belongs on a
        channel, not just a port. Every run until now fed channel A.
  - [x] `--boot-input-channel A|B`. Feeding `\r` to serial 1 channel B moves the
        PROM to `00002542`, another new region, so the input is being consumed.
  - [x] **Established: the display is the console.** MAME's `dn3500()` wires the
        stdio terminal only inside `#ifdef APOLLO_XXL`, so a stock DN3500 has
        **no serial terminal at all** — just the keyboard on serial 1 channel A.
        Detail in `PROJECT_STATUS.md`.
  - [x] The **graphics memories decode**: `0A0000-0BFFFF` colour and
        `FA0000-FDFFFF` monochrome, matched *before* the AT bus windows.
        Detail in `PROJECT_STATUS.md`.
  - [x] The graphics memories **store**, caller-owned as main memory is — this
        core allocates nothing. `ap_graphics_attach_memory` takes either or
        both, which is what a machine with one controller and not the other has.
        Detail in `PROJECT_STATUS.md`.
  - [x] `--screen c4p|c8p|19i|15i` fits a display, allocating the graphics
        memories in the frontend — only when one is fitted, so a machine without
        a screen has *no* frame buffer rather than an empty one.
        Detail in `PROJECT_STATUS.md`.
  - [x] The **control register mode fields**: `CR0` bits 7-5 select one of eight
        operating modes, `CR2` bits 7-6 one of four access modes. A pure data
        module — names and bit positions — because that part can be got right
        before anything draws, and a mode field read from the wrong bits is a
        defect that survives every test of the thing above it: the blitter would
        run a real mode, just not the one asked for, and only a picture would
        show it. `graphics_suite`, 2 tests.
        Detail in `PROJECT_STATUS.md`.
  - [x] `CR0`'s **shift field** (bits 4-0) and `CR1`'s bits. Two gaps in the
        mode-field commit above, found by reading the oracle's definitions
        rather than by using them.
        Detail in `PROJECT_STATUS.md`.
  - [x] The rest of the 803 writes — `CR2`/`CR3`, the blitter's five modes and
        the colour lookup table — is the **drawing engine**, and it has a parent
        of its own now. Recorded here as what the console investigation
        established was needed, and tracked there as the work.
  - [x] Why the display controller matters, kept as the finding it is: it stops
        being a probe target and becomes the machine's output, the four regions
        already recorded (`05D800`/`05E800` registers, `0FA0000`/`000A0000`
        graphics memory) turning from addresses into a screen. This line said of
        itself that it "is not a separate piece of work", and it was carrying an
        unticked box for a job tracked in two other places.
  - [x] **The scan codes themselves, from `008778-03` Chapter 12** — the
        keyboard's own chapter in the machine's own technical reference, on disk
        the whole time. It states that the keyboard sends **one of two code
        sets**, ASCII or keystate, and this core had read one as the other: the
        PROM's translation table was taken for *release* codes because every
        entry has bit 7 set, when Table 12-1 shows them as the ASCII set's codes
        for keys with no character. `FINDINGS.md` C46 falls with it — `5B` and
        `7B` are one key's two codes, exactly as they looked.
        All 101 coded keys implemented, plus `ap_kbd_encode` for a frontend to
        type with. Detail in `PROJECT_STATUS.md`.
        *Verification: `kbd_suite`, 7 further tests (12 total) — including that
        a carriage return is `CB` and not `0D`, which is the mistake the encoder
        exists to prevent.*
  - [x] Superseded, and its questions answered by the children below it. The
        reading was **SIO1 is the keyboard, not a terminal**, and it asked what
        MAME attaches to each `apollo_sio` and whether its DN3500 drives a
        keyboard there. Both were then settled: *"Confirmed by the oracle, and
        it names the channel"* and *"Established: the display is the console"*.
        Right about the device, wrong about the consequence — feeding it ASCII
        is not wrong, but the codes are the keyboard's own set rather than a
        terminal's, which is what the item above now implements.
  - [x] **`--boot-watch` now refuses a non-memory address**, naming the region
        it landed in. It reads through `ap_board_read` every step, so watching a
        DUART would pop its receive FIFO and every read inflates the per-region
        counters — an instrument that changes what it measures is worse than
        none. A comment was not a guard: the mistake is one keystroke away, and
        its symptom is a run that is merely *different*, with nothing to say the
        watching caused it.
  - [x] `0000220C` established: a table search against the keyboard scan-code
        map at `000021D2`, settled because ASCII was being fed to the keyboard
        channel. Superseded by feeding serial 1 channel B, which moves the PROM
        to `00002542`.
- [x] **The tick loop**, the project's central design item — as far as this
      phase can take it. *Verification: one run loop and only one. The frontend
      used to step `ap_m68030_step` directly, so the boot ran on a machine where
      no time passed; it calls `ap_machine_run` with a limit of one instead, and
      the same boot now advances 3,370,481,136 base units. Devices advance
      against absolute time carrying their own remainders, the bus ticks per
      clock, and contention is emergent rather than computed — the processor
      loses the arbitration rather than being charged a penalty. `machine_suite`,
      41 tests. The **strict** reading of "one `tick()` per machine cycle" needs
      a cycle-steppable CPU and is filed in Phase 8 beside the item that
      unblocks it, not left implied by a ticked box here.*

  - [x] **The five named debts, discharged.** The item's own definition of done
        was that the five things it named begin to advance; three had been
        settled by other work without it noticing (the DUART's counter/timer,
        the MC146818's periodic interrupt, the bus's arrival clock) and two are
        done here. **Stop-bit timing**: the frontend paced input at ten bit
        times, which is 8N1 and nothing else, so the figure now comes from the
        mode registers through `ap_mc68681_character_time` -- `[68681]` Table
        4-5's stop lengths in sixteenths, whose *two columns* differ by half a
        bit for a 5-bit character. **Keyboard auto-repeat**: unmodelled because
        "a repeat interval would be a number with no clock behind it", and the
        number was in `008778-03` Chapter 12 all along -- 33 ms after a 500 ms
        delay, both exact on the time base. Detail in `PROJECT_STATUS.md`.
        *Verification: `sio_suite` 17, `kbd_suite` 17. The PROM still runs
        4,000,000 instructions with 129 bus errors and 129 unmapped reads, all
        at the FPA probe address -- unchanged.*
  - [x] **Moved rather than dropped, to Phase 8.** Devices advance once per
        instruction to an absolute instant and the bus is ticked per *clock*,
        which is not `CLAUDE.md`'s "one `tick()` per machine cycle" in the
        strict sense. It is architectural, not a debt, and it awaits a
        cycle-steppable CPU — so it now sits with that item, where the plan is
        read forwards from.
  - [x] That run was a machine waiting correctly rather than a runaway — the PC
        stayed on one instruction and the fault count was static, unlike the
        vector-table runaway. Confirmed since: it was waiting for console input
        on a channel nothing was feeding.
  - [x] Every `ap_board_t` counter now records the **first address** as well as
        the count — read-only writes and both AT bus empty-slot directions,
        matching the unmapped pair. Applied before an investigation needed it
        rather than during one, which is the point of C33's rule.
        Detail in `PROJECT_STATUS.md`.
- [x] **The display controller's drawing engine.** *Verification: the item's own
      standard, a decoded PNG — written by `--screenshot` and read back through
      libpng's own reader, since an encoder and a decoder that only agree with
      each other can agree on the wrong picture. A blit is checked *through* the
      scanout rather than beside it. `graphics_suite`, 66 tests.
      **What a picture of the running firmware waits on is not in this item**:
      the status register is the raster, so the boot polls forever and never
      reaches its drawing code, and the video clock domain that fixes it
      recomputes `AP_TIME_BASE_HZ` — Phase 5's first item, `FINDINGS.md` C112.
      The palette proves the controller is reachable in the meantime: the
      firmware had already loaded entry 0 black and 1-255 white through the
      Bt458's ports, a white-on-black console, read out of the machine once
      something was connected to receive it.*

  - [x] The rest of the display controllers: the blitter and the colour lookup
        table, verified on a decoded PNG rather than on register round-trips.
        Eight pieces, each with its own section in `PROJECT_STATUS.md`: the
        raster operation and `CR2`'s two encodings; the word-level data path and
        its two active-low fields; the blit that is the plane loop around them;
        the Bt458 from its datasheet; the scanout, whose buffer widths are the
        manual's printed memory capacities divided out; the register file, whose
        byte lanes are scrambled; `CR0`'s seven-mode dispatch, which makes a
        write to the image memory the blit cycle it really is; and the lookup
        table wired behind its two ports.
        Three defects and two misdiagnoses came out of it — a 16-bit guard latch
        that should be 32, an access width thrown away at the board boundary, a
        frontend allocating one eighth of a card's memory, and two "deliberate
        approximations" that were arithmetic already in hand.
        *Verification: `graphics_suite`, 66 tests; `frontend_common_suite` +2
        (12) for the PNG round trip through libpng's own decoder.*
- [x] **Bus faults and the exception frames.** The special status word and both
      fault frame layouts, the taker, `RTE` from either, address error, and the
      coprocessor mid-instruction frame. Two deliberate approximations, both
      `PROVISIONAL` with their cost to close in `PROJECT_STATUS.md`.

  - [x] The **special status word** and the bus fault frame layout,
        `cpu/m68030/ap_m68030_ssw.c` — Figure 8-9's bit positions, the SIZ1/SIZ0
        encoding that counts bytes *remaining* (so a long word is zero), the
        FC2-FC0 address space, and Table 8-6's field offsets for both fault
        frames. `ssw_suite`, 12 tests.
        Detail in `PROJECT_STATUS.md`.
  - [x] `ap_m68030_take_bus_fault()` — a faulting access now **takes** vector 2
        rather than stopping the step, building the `$A` or `$B` frame the SSW
        selects. The CPU records what faulted (address, size, direction,
        address space, and the value a faulted write carried) at the access,
        because by the time a status is chosen that detail is gone and a handler
        given the wrong address repairs the wrong location.
        Detail in `PROJECT_STATUS.md`.
  - [x] `RTE` from a `$A`/`$B` frame. Returning to the stacked PC re-executes
        the faulted instruction from the start, which is **exact** when the
        faulted access precedes any side effect — the common case, and every
        case the boot PROM hits — and wrong for an instruction that had already
        committed one. A deliberate approximation: closing it needs the internal
        registers, which needs a microsequencer model.
  - [x] **A write can now fault.** `ap_m68030_store_fn` returned `void`, so the
        memory system could count a write to an address nothing decoded and then
        had no way to refuse it — no write could raise a bus error, and an
        exception frame stacked into undecoded space succeeded. A signal a
        callee cannot send is one the caller assumes never happens.
        Detail in `PROJECT_STATUS.md`.
  - [x] The boot PROM reaches **89 instructions** — an honest reading this time.
        Detail in `PROJECT_STATUS.md`.
  - [x] **Address error (vector 3)**, which shares these frames and was defined
        but never raised. §8.1.3: "An address error exception occurs when the
        processor attempts to prefetch an instruction from an odd address."
        - Only a *prefetch*. Misaligned data is legal on this part — §7.2.1
          transfers a long word to an odd address in three bus cycles — so the
          check is on the program counter alone. Applying it to operands would
          fault programs the hardware runs, and there is a test for exactly that
          so the 68000's rule cannot creep back in.
        Detail in `PROJECT_STATUS.md`.
  - [x] **A write to a read-only memory is absorbed, not refused** — a defect
        introduced when the store path gained the ability to fault, and caught
        by asking the oracle rather than by reasoning. `ap_board_write` returned
        `ok = false` for the PROM and node ID, which was harmless while no write
        could fault and became a spurious bus error the moment one could.
        Detail in `PROJECT_STATUS.md`.
  - [x] **Resolved: there was never a handler bug** (`FINDINGS.md` C32).
        Detail in `PROJECT_STATUS.md`.
  - [x] The reset SSP question is answered by the same finding: the stack only
        ran off the bottom of RAM because faults were being raised that the real
        machine does not raise. `01000180` is not a stack the firmware is
        expected to build four exception frames on, and now it will not have
        to.
  - [x] **Recorded as the convention requires, which was the task.** The long
        frame's INTERNAL REGISTER words are stacked as zero, and the coprocessor
        mid-instruction frame (`$9`) does the same for the same reason: Table
        8-6 names the fields and gives them no defined contents, because they
        hold microsequencer state this model has none of. Marked `PROVISIONAL`
        in `ap_m68030_step.c` and carried as **two rows** of
        `PROJECT_STATUS.md`'s table — the zeroed words, and the `RTE` that
        re-executes rather than resumes — each with its cost to close.
        Kept as a plan item it was a third copy: the item said "record it as
        `PROVISIONAL` when it lands", and it has landed. Closing the
        approximation needs a microsequencer model, which the table's cost-to-
        close column names and the plan cannot usefully hold open.
  - [x] Closed: the store callback returns `bool` now, so a write can fault, and
        `step_suite` covers a faulted write taking the short frame. It was
        recorded as unreachable rather than quietly left untested, which is what
        made it findable when the store path changed.
- [x] **Archive SC-499 cartridge tape.** Controller, drive, `.ct` image and the
      §1.13.2 handshake with its timings, wired to the board on IRQ5.
      **Awaiting:** whether the controller asserts EXCEPTION at reset, and the
      handshake bounds' closing — both recorded in `PROJECT_STATUS.md`, both
      needing evidence this project does not have rather than work it has not
      done.

  - [x] The `.ct` image reader, `image/ap_ct.c`: block addressing, the
        whole-block size check, and boot-record parsing. `ct_suite`, 8 tests.
  - [x] The QIC-02 command set transcribed as far as the scan allows
        (`FINDINGS.md` C25) — ten of twelve codes legible; ERASE and SELECT Q11
        FORMAT are left blank rather than guessed, though both are constrained
        to the `2x` group.
  - [x] The drive and tape motion, `device/ap_qic.c`: the readable half of the
        command set over a `.ct` image, with writing refused and the two lost
        opcodes claimed by nothing. `qic_suite`, 18 tests.
  - [x] The drive joined to the SC-499's registers: a driver reaches it through
        `050000`, commands go via the request bit, and data comes back a byte at
        a time. `tape_suite`, 11 tests.
  - [x] **The per-byte QIC-02 handshake, timed.** §1.13.2's ordering was
        modelled; the intervals now are too. The device carries its own clock,
        advanced by `ap_board_advance` with every other device, and a command
        reaches its destination only when its figure's interval has passed.
        Every interval is `PROVISIONAL` — the figures publish bounds, not
        values. Detail in `PROJECT_STATUS.md`.
        *Verification: `sc499_suite` 16, `tape_suite` 16. Figure 1-9's total is
        a **sum** of two sequential intervals rather than one bound, and
        `T_REQUEST_TO_NOT_READY` is deliberately **not** taken at its bound —
        holding READY up for that microsecond shows a driver a device that
        looks finished with a command it has only just been handed.*
  - [x] Figure 1-6, the read data transfer, transcribed (C26): ACKNOWLEDGE and
        TRANSFER pace each byte while READY frames the block.
  - [x] Figure 1-8 transcribed and the READY/EXCEPTION defect fixed: exception
        clears ready in one call, and a command clears the exception and
        restores ready, which is Figure 1-8's own order.
  - [x] Figure 1-10, the status byte transfer, transcribed (C26): a repeating
        per-byte REQUEST/READY exchange with DIRECTION reversed, echoing a
        status *block* rather than one byte.
  - [x] **The status block: six bytes, and the manual did say so.** The length
        is not in Figure 1-10 but in §1.13.1's READ STATUS entry — "the device
        transfers the standard six bytes to the host". Three 16-bit fields
        LSB-first (exception flags, data-error count, underrun count), from
        Linux's `struct tpstatus` and the oracle, not from the QIC-02
        convention this item refused. Detail in `PROJECT_STATUS.md`.
        *Verification: `qic_suite`, 6 further tests (18 total) — including the
        power-on flag surviving until read and not after, which is how a driver
        tells a drive it has already talked to from one that just came up.*
  - [x] Figure 1-9, the command transfer with DIRECTION asserted, transcribed
        (C26). With 1-7 and 1-8 that makes the command handshake a **state
        machine with three entry conditions** — ready, exception, device holding
        the bus — one figure each, selected by the device's state on entry.
  - [x] The state machine's **ordering** implemented: the entry condition is
        selected from the device's state, and accepting a command clears the
        exception, hands the bus back and asserts ready — the three figures'
        common destination. A tape read now makes the device hold the bus, which
        is the state Figure 1-9 resolves. `sc499_suite` and `tape_suite`.
  - [x] Its **timings**, modelled at the documented bounds and marked
        `PROVISIONAL` in code and in `PROJECT_STATUS.md`, as `CLAUDE.md`
        prescribes for a quantity published as a range. All nine convert
        exactly to base units, so none is rounded as well as bounded.
  - [x] Closing them is tracked where a `PROVISIONAL` figure belongs — the
        table in `PROJECT_STATUS.md`, which carries the row, the reason and the
        cost to close ("measure edge timings against a running drive"). Kept as
        a plan item it was a second copy of that row, and the plan is read
        forwards to choose the next thing: an approximation already recorded,
        with no drive here to measure against, is not one.
  - [x] Figure 1-5, the write data transfer: **not required**, and not a gap.
        It is reachable only through a write-back path, and `ap_qic` refuses
        WRITE and WRITE FILE MARK outright rather than discarding them — a
        deliberate decision with its own test. Transcribing a figure for a path
        that is designed not to exist would be work with nothing to check it.
  - [x] Note C25's constraint is **implemented**, not merely recorded: the
        controller identifies a cartridge from BOT-to-load-point *distance*,
        which a raw block image has no geometry to supply, so `ap_qic_load`
        takes the type from its caller and refuses `AP_QIC_CARTRIDGE_NONE`
        rather than defaulting one. `qic_suite`'s
        `test_the_cartridge_type_must_be_supplied`. The image format is
        `image/ap_ct.c` (`FINDINGS.md` C24): a raw 512-byte-block image,
        104,841 blocks for the boot cartridge, no wrapper to parse.
  - [x] Wired into the board at `050000`: four registers at stride 1, the upper
        four addresses of each eight floating to `FF`, aliased through the
        range, on IRQ5. `tape_suite`, 6 tests, including the measured reset dump
        reproduced over two aliasing periods.
  - [x] **Settled, and it cost three wrong conclusions.** The scan lost the bit
        numbers *and a polarity column*; the **page image** has both. RDY and
        EXC are asserted **low**, so the measured `40` means *not ready* — this
        core had it active high and set `ready` at reset, two errors cancelling
        into the right byte with the opposite meaning. That also voided the
        argument for reading the interrupt flag as a conjunction (it is a
        disjunction), and for disbelieving "a reset sets DONE" (bit 4, and it
        does). Linux's `tpqic02.h` and the oracle's `sc499.cpp` both confirm the
        polarity. Detail in `PROJECT_STATUS.md`.
        *Verification: `sc499_suite`, 16 tests; `tape_suite` and `board_suite`
        expectations updated. This core reads `70` where the oracle reads `40`.*
  - [x] One question is left **open and recorded** rather than answered:
        whether the controller asserts EXCEPTION at reset. The oracle does;
        `[SC499]` says nothing either way, and inferring it from a commented-out
        line in MAME is not a source. It lives in `PROJECT_STATUS.md` and in
        `ap_tape_reset` beside the code that would change, because it is an
        unknown awaiting evidence — a driver reading status after a reset — and
        not a task anyone here can pick up. Detail in `PROJECT_STATUS.md`.

      Placement, the card-removal check and the register
      model's recovery: detail in `PROJECT_STATUS.md`.
- [x] Winchester and floppy media handling (`.awd` for the disk, as the oracle
      names it). Placement: `04D000`-`04D007`, AT `1A0`-`1A7`, eight registers,
      confirmed by aliasing period. **Note for any oracle comparison:** the
      DN3500 has the OMTI in `isa1` by default, so `-isa1 ctape` *removes the
      disk controller* — they must go in different slots, and the failure is
      silent (C16). *Verification met: the oracle's idle controller reads
      `FF C0 FC 00` across the four ports and `omti_suite` reproduces all four,
      `board_suite` the `C0` through the map. Both halves move data —
      `image/ap_awd.c` and `image/ap_afd.c` — and a controller with no drive
      answers "not ready" rather than looking like one with blank media.*
- [x] QIC-II cartridge tape, `.ct` images. *Verification met, against the real
      distribution: all five SR10.x cartridges read end to end — 562,616 blocks
      in total — through the drive's own `READ` path rather than by indexing the
      buffer, which would test nothing but `memcpy`. `--tape PATH` is the
      reading counterpart of `--volume`, added because `--boot-tape` correctly
      refuses the four data cartridges for having no boot record and so says
      nothing about whether their blocks are readable. The boot cartridge's
      header comes back as C24 recorded it: load `0013D800`, entry `0013D82A`,
      length 7868.*
- [x] `.awd` / `.afd` image formats, so oracle and emulator share media.
      *Verification: the addressing agrees **by construction** on both, which is
      what "byte-identical reads of the same image" is really asking. The disk:
      `AP_AWD_SECTOR_BYTES` is 1056 and `omti8621.cpp` seeks
      `diskaddr * OMTI_DISK_SECTOR_SIZE`, also 1056; `--volume` parses a real
      348 MB image. The floppy: three independent sources give the geometry
      field by field — `[OMTI]` §6, MAME's driver page (`ibs=16384` is
      2 x 8 x 1024), and `apollo_dsk.cpp`'s format table, which names the
      **first sector id as 1** that `[OMTI]` §6.2's `R` had given us
      independently. `--floppy` reads every sector through the reader and checks
      the linear numbers come out consecutive, which a head-major layout would
      fail and a set comparison would not. Detail in `PROJECT_STATUS.md`.
      Not claimed: content. A blank image reads the same under any geometry, and
      the literal "same image under both" wants a Domain-written floppy, which
      comes off a running system — it waits on the install, not on this item.*

- [x] **Complete implementation: every declared-but-inert signal.** A sweep for
      declines, then a triage separating behaviour that is merely unwritten from
      facts no document states -- the grep that built the list conflated them,
      and `ap_dmapage` was the casualty. Everything in the first class is now
      implemented: `SQWE`, `DSE`, `OPCR[7]`, `tx_break`, the tape's block-level
      READY, and both format extensions. Serial framing turned out to have been
      modelled all along behind two stale claims.
      Six of the eight were declined in our own files with a stated reason --
      "nothing is wired to it", "that section has not been read", "rather than a
      guess" -- and in every case the document had the answer. Four "not
      modelled" notes were simply out of date. Detail in `PROJECT_STATUS.md`.
      *Verification: `mc146818_suite` 31, `mc68681_suite` 38, `sio_suite` 25,
      `tape_suite` 17, `awd_suite` 38, `qic_suite` 18 -- each signal asserted to
      do something observable.*
- [x] **The declines that need a document or a measurement, not code.** Three
      of the five closed, and two of those on a **sibling manual already on
      disk** -- `002398-04`, the Domain Engineering Handbook, whose DN3000
      chapter prints what `008778-03` omits. `ap_dmapage`'s channel mapping is
      p. 12-25's table (channel 4, the cascade, has none); the keyboard beeper
      is p. 12-2's sequences and its 300 ms auto-off, the observable that was
      being thrown away. The MC146818 decline stands but **its arithmetic was
      two base-recomputations stale**. The graphics A/D and the Series 4000
      Master Request bit stay declined, with the search now recorded as
      exhausted; the graphics refresh trigger became a recorded signal rather
      than a discarded write. Detail in `PROJECT_STATUS.md`.
      *Verification: `board_suite` 29 -> 32, `kbd_suite` +3, `graphics_suite`
      and `mc146818_suite` extended -- the last asserting the base's power of
      two, so a recomputation that makes the six fast rates representable fails
      a test naming the decline to reopen.*
- [x] **`IRQ6` and `DRQ2`, the floppy's**, are driven. Both were placed on the
      board and left undriven because this half had nothing to derive them from.
      It has Table 4-3's Digital Output Register bit 3, which gates both, the
      same shape as the fixed disk's `IREQ` on its MASK register. `IRQ6` follows
      the **result** phase, the FDC's completion; `DRQ2` the **execution**
      phase, a byte in flight -- two different conditions, which is what the
      board's own comment said and why they are two derivations.
      *Verification: `omti_suite` 19 -- a command with a result phase and no
      execution phase raises the interrupt and not the request, collecting the
      result bytes takes it down, and with the enable bit clear the same state
      raises nothing.*


- [x] **Stage 2's reading, taken once Stage 1 was complete.** With every
      implementation item closed the boot ends in **exactly the same place** --
      the `3C456BB0` blink loop at 400M and 600M instructions, image MD5
      unchanged. The thermometer doing its job: none of that work was aimed at
      this halt and none of it moved it, so the remaining distance is not hidden
      in the parts. What it *did* settle is the harness: Normal mode puts **zero
      bytes** on the serial line over ten minutes and so does the oracle, because
      Domain/OS talks to the display. Detail in `PROJECT_STATUS.md`.
      *Verification: the reading itself, and its consequence carried out -- the
      boot item's "console byte-identical to the oracle" was rewritten against
      the framebuffer below, since a byte-identical nothing is not evidence.*
- [x] **Full-state differential against the oracle.** Both machines dump every
      field of architectural and device state and the dumps are compared field
      by field: `--dump-state FILE` on ours, `apollo_dump_state` on the oracle
      (`tools/mame-oracle/apollo-state-dump.patch`), the correspondence in
      `tools/mame-oracle/state-map.txt`, the diff in `tools/state-diff.py`, and
      `tools/state-compare.sh` driving both. Sync is on a program event -- the
      Nth execution of a PC, or the Nth write to a physical address -- never on
      an instruction count, and `tools/mame-oracle/statesync.lua` steps the
      oracle once past its breakpoint because MAME stops before an instruction
      and this core stops after. It is the instrument that found the bus fault
      frame defects. Detail in `PROJECT_STATUS.md`; the design deliberation this
      item carried is archived at the end of that file.
      *Verification: as asked -- empty at a matched instant and naming a field at
      the fault. 27 of 27 mapped CPU fields match at `AST_$LOAD_AOTE+43C`, and
      at the fault the difference is the special status word: `0162` against our
      `0000`.*

- [x] **Integration check, not a milestone:** DN3500 boots Domain/OS SR10.4 to
      a login prompt, from its own disk. The screen reaches `login:` with no
      `CRASH_STATUS`, no `FAULT IN DOMAIN/OS`, no salvage and no reboot, and the
      run ends `EXECUTED` at its limit sitting at the prompt. MMU faults across
      the boot go **30,837,461 -> 4,212**. Three defects in the bus fault frame
      closed it, each read off the frame the *oracle* builds for a fault it takes
      and recovers from: the special status word described no bus cycle, the
      frame format was chosen from that word rather than from Table 8-6's
      position split, and a faulted extension word named the opcode's address as
      the fault address. Detail in `PROJECT_STATUS.md`, whose working notes for
      this item are archived at the end of that file.
      *Verification: `tools/e0007-boot.sh --boot-limit 1500000000 --screenshot
      FILE` reaches `login:`; the decoded PNG is
      `docs/images/dn3500-sr10.4-login.png`. `ctest` 136/136.*
  - [x] **Answered: the kernel was declining nothing.** It was being asked about
        the wrong page. A faulted extension word recorded `regs.pc` -- the
        opcode's address, in a page resident by definition -- as the fault
        address, so `PTEST` answered `0803` (valid) and the kernel correctly
        concluded there was nothing to fetch. Detail in `PROJECT_STATUS.md`.
  - [x] ~~`0012000A`, *unimplemented instruction*~~ -- moot, and closed by the
        boot rather than by a diagnosis. `FAULT IN DOMAIN/OS` at
        `FIM_$FSAVE+60` does not occur at all now: the run to `login:` contains
        the string zero times, and its 124 vector-11 traps are Domain/OS's own
        lazy floating-point switching, which is the mechanism this item mistook
        for a fault. **Not closed on "a boot that got further"** -- that warning
        was attached to this item and is archived with it -- but on the terminal
        state the item is about being absent from a run that reaches the prompt.
        Detail in `PROJECT_STATUS.md`.
  - [x] **Three bytes is a transfer size, and the board refused it.** Past test
        7, the boot stopped in `Memory Module 1  Test # 0` with `Unexpected CPU
        bus error referencing 0100A005` -- an address that answers perfectly.
        Dumping the exception frames found the real one: `000075CC` is
        `MOVE.L D0,$5(A0)`, a **misaligned long word**, written on purpose
        because a 68030 can and a 68000 cannot. `[030]` Table 7-2, from the page
        image: `SIZ1 SIZ0 = 11` is **3 Bytes**, and misalignment is what
        produces it. The board's access helpers took 1, 2 and 4 only, so every
        misaligned long word was a bus error; the CPU side had always been
        right. Detail in `PROJECT_STATUS.md`.
        *Verification: `machine_suite` +1 (42), running the firmware's own
        instruction and checking the bytes land where addressed;
        `board_suite` +1 (27) for the width itself. The boot now passes Memory
        Module 1 and is into Module 2 with no self-test failure at all.*
  - [x] **The memory array's parity circuit, and self-test 7 passes.** This
        core had none, so a test that forces bad parity and expects a trap could
        not pass. `008778-03` §3.3 gives four F280 checkers, one per byte lane;
        §3.2 gives the interrupt whole, **level 7 and autovectored at `007C`**,
        the only one on this board no 8259 answers. The lane bits are active low
        and the *firmware* proves it -- Series 4000 PROMs write `08`, the
        DN3000's write `F8`, for the same effect -- so it is a model table
        field. One bit per byte of main memory, caller-supplied like the RAM.
        Carries two things found while wiring it: the control register's LED and
        parity bytes are different halves, and a run now reports its first and
        last faulting address. Detail in `PROJECT_STATUS.md`.
        *Verification: `parity_suite`, 9 tests, a new CTest entry -- and the
        boot, which now announces `Memory Module 1  Test # 0` where it stopped
        at test 7, reporting exactly 4 forced writes and 4 errors in 30M
        instructions. `PROVISIONAL`: which lane bit is which byte, which no
        image distinguishes.*
  - [x] **A status-register write acknowledges conditions; it does not throw
        the switch.** This file argued bit 0 was a switch *input* and then
        cleared it on every write, and the PROM writes the register three times
        before it gets anywhere -- so every boot so far put a normal machine
        back into service mode. `008778-03` §3.2 says a write clears the
        interrupt status; `019411-A00` gives the FP trap its own clear location,
        which is the argument that a status write does not clear it. The
        **selective clear locations** land with it: five functions at one
        address each, the one core-board range where the low bits are the decode
        rather than an alias. Detail in `PROJECT_STATUS.md`.
        *Verification: `boardreg_suite` +4 (20). Two existing tests asserted the
        old behaviour -- C10's sweep was measured in service mode, where the
        three preserved bits are already what a flat `8000` says, so the suite
        had made the gap permanent rather than caught it.*
  - [x] **It was waiting because it was in Service mode.** Bit 0 of the CPU
        status register is the Normal/Service switch, inverted from its own
        name — the bit reads 1 for *normal*. `CPU_STATUS_RESET` was `8100` and
        it was measured against the oracle in its shipping configuration, which
        is Service; `mdsession.lua` had recorded that default years ago without
        connecting it to the boot. Modelled as a switch with a default of
        normal. `FINDINGS.md` C114. Detail in `PROJECT_STATUS.md`.
        *Verification: `boardreg_suite` +1 (13). Setting it moves the PROM from
        `000007A2` to `0000658C`, stops the 66,138 blit cycles of diagnostics
        that service mode is for, and starts a different poll entirely.*
  - [x] **Serial 1's input port is the RAM configuration**, strapped to
        `IP0`-`IP6` and read by the PROM to size memory before anything else —
        so a machine answering zero has no memory fitted, which this core was.
        Modelled as a **table** and not an encoder: `20` is "8-8-8-8" on a
        DN3500 and "2-2-2-2" on a DN3000, so the model is part of the decode and
        four points determine no scheme. `FINDINGS.md` C115.
        *Verification: `sio_suite` +3 (20) — the same byte meaning two machines,
        an unlisted size refused, and only the seven pins the part has. The
        refusal found the frontend building **4 MB**, which is not a DN3500
        configuration at all; it builds 16 MB now.*
  - [x] **OP3 is wired back to IP0**, and the poll is a timing measurement: the
        PROM programs the timer, routes it to OP3, starts it and counts five
        whole cycles of IP0. The board loops the **memory refresh** square wave
        back to the part's own input port. This core already produced that wave
        at the right period with its own tests, and it was connected to nothing
        — the third time this campaign the missing piece was a connection rather
        than a model. `FINDINGS.md` C116.
        *Verification: `sio_suite` +2 (22) — the pin following the wave and the
        change flag actually setting, since a pin that changed without one would
        leave the PROM spinning; and the loopback leaving the six RAM-config
        pins alone. The boot moves from `0000658C` to `00007026` and from 0 to
        **655,368 blit cycles**.*
  - [x] **Two corrections to the raster, and a missing bit.** `BLANK` and
        `V_BLANK` are **active low** — this core had them set while blanking,
        with a comment arguing for it, and the test asserted the same mistake.
        The **vertical sync** pulse was missing entirely and it is the bit the
        PROM waits on at `007026`. And "held in reset" is not silent: the
        register holds a per-family value with the sync bit set, which is a
        *display present* probe made before the controller is programmed — this
        core answered zero, reporting no display on a machine that has one.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `graphics_suite` +1 (74), with the two polarity tests
        rewritten against the oracle's structure. The boot walks on from
        `00007026` to a delay loop, doing 2,097,183 display reads and 1,311,743
        writes on the way.*
  - [x] **The machine was telling us what failed.** The boot settles in a delay
        loop whose caller writes `010100` — `008778-03` §3.7's nine diagnostic
        LEDs, "the upper byte of the control register". A machine that fails a
        self-test posts a code there and flashes it for ever, having no console
        to complain to, and this core counted those writes and discarded the
        values. Detail in `PROJECT_STATUS.md`.
        *Verification: `boardreg_suite` +3 (16). A boot now reports
        `FF 00 EF DF FE EE DE CF BF AF 9F ED DD 9D 8D 0D 8D 0D ...` — a
        self-test progress sequence, then an alternation differing only in bit
        7, which is one LED blinking on a steady code.*
  - [x] **The failing test is a DAC check, and the A/D is a video monitor.**
        `007090` reads two A/D channels and range-checks each into `[52, 70)`.
        The converter measures the controller's *own video output* — one gun, at
        the pixel under the beam, through the lookup table — so it needed the
        palette and the raster first, and it confirms the blanking polarity
        corrected two commits earlier. Detail in `PROJECT_STATUS.md`, C117.
        *Verification: `graphics_suite` +3 (77) — each gun answering for its own
        channel, a non-video channel refused rather than answered with a zero,
        and the floors outside the visible field. The failure **moves** with the
        A/D answered, from `8D 0D` to `8D 7D 0D`, so the reading reaches the
        firmware and is used.*
  - [x] **The stepped counters, and the self-test walks past the DAC check.**
        `DH_CK`, `DV_CK` and `DP_CK` step on the **falling edge**, the
        horizontal carries into the vertical, it counts *words*, and `DV_CK`
        does not exist on a single-plane board. The A/D reads through these
        rather than the running raster. Detail in `PROJECT_STATUS.md`.
        *Verification: `graphics_suite` +5 (82); the posted codes stop
        alternating.*
  - [x] **Typing interrupts the boot either way, and the keyboard has its own
        framing.** In normal mode a console character still enters MD — same
        banner, same prompt — so the difference between the modes is not the
        console path. And the keyboard runs **1200 baud 8E1**, measured from
        `apollo_kbd_device::device_reset`, where `deliver_key` had been sending
        at *the port's* rate with a comment calling that an assumption. Detail
        in `PROJECT_STATUS.md`.
        *Verification: `board_suite` +1 (26) — the keypress test now programs
        the port to the keyboard's framing before the byte arrives, and its new
        sibling shows a 9600 port receiving the byte **damaged** rather than
        cleanly, which a board delivering at the port's own rate could not
        show.*
  - [x] **The keyboard's command channel**, which this core did not have: it
        sent scan codes and received nothing. Every command begins `FF` and
        accumulates — `FF12` is a prefix and `FF1221` the identify — and it
        **powers up in loopback**, echoing rather than acting, which is how a
        host discovers it is there. Wired both ways through serial 1 channel A.
        `FINDINGS.md` C118. Detail in `PROJECT_STATUS.md`.
        *Verification: `kbd_suite` +6 (23). It did **not** change the boot —
        same resting PC, same posted codes — because the firmware does not
        reach the keyboard in that window; modelled because it is the machine's,
        not because it moved anything.*
  - [x] **Checked, and clean: the machine is behaving correctly.** The console
        poll is three `BTST`s and a backward branch — no timeout, no deadline,
        no fourth exit — and the oracle's keyboard never transmits unprompted,
        so a real DN3500 with nothing attached that speaks waits exactly where
        this core waits. The "it does not auto-boot" reading is **correct
        behaviour**, and the search for a missing device was looking for
        something that does not exist. `ACR[7]`'s baud-set selection was checked
        too and is already honoured. Detail in `PROJECT_STATUS.md`.
  - [x] **`H` returns a command table, and three sessions reasoned from a
        fragment.** `MD.md` had it returning nine letters and *inferred* a
        command list; it returns **thirty-six commands in four columns**, `D`
        among them, and `EX`, `EY`, `LO`, `FO` and `DL` — every one the earlier
        reading ruled out. The nine letters were a truncated capture. Corrected
        in `MD.md` in place. Detail in `PROJECT_STATUS.md`.
        *Verification: the table read out of this core over the serial console.
        It also invalidates three downstream conclusions drawn from the
        fragment, which are marked where they were made.*
  - [x] **`FO` reaches the disk.** `FORCE LOAD` at the MD prompt is accepted —
        echoed with no `E`, the PC moves to `0000303A` — and the region table
        shows **6,328,241 disk reads against 7 writes**: a Command Descriptor
        Block going out and the poll waiting for a completion that never comes.
        No run in this project had touched the controller before.
        Detail in `PROJECT_STATUS.md`.
  - [x] **Which register the poll is on, measured.** Three attempts to settle it
        from the disassembly failed — the base `a0` holds is set far from the
        loop. Per-register counters say it in one line: `STATUS` read
        `0x100000 + 1` times, one whole timeout expired exactly, then five more
        on `DATA`. The controller is selected and never answers.
        Detail in `PROJECT_STATUS.md`.
  - [x] **The status register was missing the two bits the protocol runs on.**
        Table 4-2 gives eight and this core had six: `I/O` and `REQ`, and `REQ`
        is what every phase of §4.3 turns on. With the selection handshake,
        the per-byte request, idle-only selection and `DREQ` gated on the MASK's
        DMA ENABLE, six command bytes go out where one did, the status byte
        comes back, and the machine returns to the **MD prompt** instead of
        resting in a timeout. `FINDINGS.md` C119.
        *Verification: `awd_suite` +4 (15) — the selection handshake, each byte
        clearing and re-asserting the request, a stray select ignored, and the
        two data modes told apart. `omti_suite`'s reset comparison caught a
        second defect: reset cleared the status and left the phase.*
  - [x] **Clearing the status byte's own bits let the firmware walk on.**
        Reading the completion cleared `IREQ` and `DREQ` and left `C/D`, `I/O`,
        `BSY` and `REQ` standing — and `BSY` is how a driver knows it may start
        the next command, so a controller that collected a completion and stayed
        busy never finishes one. A command counter shows what that was worth:
        **one command became three**, `00` TEST DRIVE READY, `03` REQUEST SENSE
        and `EC` READ CAPACITY. Detail in `PROJECT_STATUS.md`.
  - [x] **`EC` implemented, and it has two names in one manual.** §5.4.29 calls
        it READ CONFIGURATION and §5.1.2's summary calls it READ CAPACITY; the
        description's name is taken, because it is the one that says what the
        ten data bytes are. Cylinders, heads and sectors are each the **highest
        valid number, not the count** — the "(-1)" the table marks and the
        obvious implementation gets wrong. Detail in `PROJECT_STATUS.md`.
        *Verification: `awd_suite` +2 (17), asserting both that the fields are
        `count - 1` and that they are not the counts. `03 REQUEST SENSE` drops
        out of the boot's command sequence, which is the improvement: a driver
        asks for sense after a failure.*
  - [x] **The command set finished as a module, not chased.** `CLAUDE.md` says
        "complete modules, don't chase the boot", and the previous steps here
        were driven by whichever command the firmware hit next — the method the
        discipline warns against. Worked from §5.1.2's table instead: five more
        commands are derivable from a geometry and a sector image and are in —
        `05 READ VERIFY`, `E2 READ ID`, `E0`, `E3` and `E4`. Detail in
        `PROJECT_STATUS.md`.
        *Verification: `awd_suite` +3 (20) — a verify transferring nothing and
        still failing off the end, READ ID's split cylinder, and the controller's
        diagnostics passing with no drive where the drive's does not.*
  - [x] **The drive configuration word, measured, and it did not move the
        boot.** §5.4.29 names bytes 4-5 and does not define them; the oracle's
        `set_configuration_data` writes `02 44`. The same function corroborates
        bytes 0-3 field by field — it computes them the way the page image says,
        which is the "(-1)" reading taken before it was looked at.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `awd_suite`'s configuration test extended to the whole
        ten bytes. The boot is **unchanged** — same commands, same resting PC —
        which is the finding: the word was not what stopped it.*
  - [x] **The node ID checksum was one register early.** Self-test 8's eleven
        instructions at `008218` give the rule outright: sum the bytes at stride
        2 below `0112 1E`, then compare with the byte *at* `0112 1E`. So the
        checksum is register **15**, and `ap_nodeid.h`'s own recorded dump had
        always shown `69` there -- the prose said 14, the code followed the
        prose, and the suite's golden array transcribed the dump the same way,
        so the test made it permanent rather than caught it. It also settles
        what that file recorded as unanswerable: the checksum covers registers
        0-14 and is a plain sum, not a complement. Detail in
        `PROJECT_STATUS.md`.
        *Verification: `nodeid_suite` +1 (8), one of them running the firmware's
        own arithmetic. Every self-test now passes and the boot reaches the file
        system: `Loading SELF_TEST diagnostics from boot device`.*
  - [x] **`SYSBOOT VER`: the byte order settled, and Domain/OS code runs.** The
        `PROVISIONAL` note on the data port's packing lasted one commit, and
        what settled it is what the note asked for -- a transfer of known
        content. The PROM loads `sysboot` to `010FD800` and demands `0013D800`
        there; it arrived as `13 00 00 D8` followed by `YSBSOO TER V`, so the
        **earlier buffer byte belongs in the high half**. The oracle packs it
        the other way and is wrong. Detail in `PROJECT_STATUS.md`.
        *Verification: `omti_suite`'s word-read test now asserts the order
        rather than "either" -- and the machine loads and runs `SELF_TEST
        Revision 2.4` off its own disk, which is Domain/OS code executing.*
  - [x] **`CPU (mmu) Test #0` passes: the cache was tagged and filled from the
        same address.** The MC68030's caches are *logically* addressed, so a hit
        is decided before translation -- and the bus cycle filling a miss uses
        the address the **MMU** produced. `ap_m68030_cache_read` took one
        address for both, which is invisible with the MMU off and wrong the
        moment a page is mapped anywhere but on top of itself. The write path
        had always used the physical address, so a mapped page could be written
        where the MMU said and read from where it was not -- the identity the
        diagnostic saw. Detail in `PROJECT_STATUS.md`.
        *Verification: `access_suite` +1 (14), asserting the fill was asked for
        the physical address and not the logical one. Descriptor fetches go from
        15 to 42,579, and the boot passes the MMU test and moves to
        `CPU (interrupts) Test #0`.*
  - [x] **`CPU (interrupts)` and `CPU (timer)` pass: bit 4 of the cache
        register is the master's request line.** `ap_boardreg` had `010200` as a
        byte with one writable bit and a fixed `6F`, measured -- and a register
        probe drives bits on a **quiet machine**, so every sample it took had no
        interrupt standing. Bit 4 follows the master 8259's `INT`. Derived, not
        latched: a stored copy would need clearing on acknowledge and the line
        already does. Detail in `PROJECT_STATUS.md`.
        *Verification: `boardreg_suite` +1 (21), including that no program write
        can put the bit down. The boot passes the interrupt and timer tests and
        reaches `CPU (dma) Test #0`.*
  - [x] **`CPU (dma) Test #0` passes: the map is the whole region.** `ap_atmap`
        held 128 entries and aliased the 2 KB region onto them every 256 bytes,
        from `019411-A00` §4.2.1.4's "one of the 128 entries" -- which counts
        what a **transfer** indexes, not what the map holds. This file had even
        recorded the discrepancy and a test asserting the gap. The gap was real
        and the conclusion drawn from it was wrong: the diagnostic writes every
        word of `017000`-`0177FE` and reads each back distinct, so there are
        **1024**. Detail in `PROJECT_STATUS.md`.
        *Verification: `atmap_suite` +1 (17) including the diagnostic's own
        walk, and three tests rewritten from the old reading. The boot passes
        the DMA test and now takes real interrupts -- vectors `A0` and `AD`.*
  - [x] **`CPU (calendar) Test #0` passes: `NBCD` was charged no time at all.**
        The test delays and requires the seconds register to have changed; the
        clock was running and the *delay* was twelve times too short. The PROM's
        delay service is 500,000 iterations of fifteen `NBCD.B`, calibrated so
        its microsecond argument comes out right at 25 MHz. `ROW_NBCD_DN` was in
        the timing table with `[030]`'s six clocks and **nothing ever returned
        it** -- family 0100's dispatch handles rows 0,1,2,3,5 and row 4 fell to
        `default`. Detail in `PROJECT_STATUS.md`.
        *Verification: measured across the delay, 8,500,157 instructions in
        0.16 s where the hardware takes about two. The console passes the
        calendar test and reaches `CPU (fp trap) Test #0`.*
  - [x] **`CPU (fp trap) Test #0` passes: the trap is the coprocessor coming
        off the bus.** Control-register bit 2 does not arm a flag, it
        **disconnects** the FPU; an FPU opcode then takes F-line, and taking it
        sets the status register's FP trap bit. The oracle sets that bit at the
        control write and its own comment says it should not, guarded on a
        condition with no hardware meaning that would fail here anyway. Detail
        in `PROJECT_STATUS.md`.
        *Verification: the console passes the FP trap test and reaches
        `CPU (bus error) Test #0`.*
  - [x] **`CPU (bus error) Test #0` passes, and with it every CPU self-test.**
        Two defects behind one another. A word read of a core register returned
        its **low byte twice** -- the board serves a word as two byte reads and
        `ap_boardreg_read8` ignored the lane -- so `8001` read as `0101` and bit
        8 was set whatever the register held; the write side had been given this
        lane split earlier and reads were left alone. And **nothing ever set**
        bit 8: an unanswered access is the CPU timeout, which the addendum gives
        a clear location of its own. That corrected the power-on value too, from
        the probe's `8100` to `8000`. Detail in `PROJECT_STATUS.md`.
        *Verification: `board_suite` +1 (28) for a selective clear reaching the
        register through the board. The console passes mmu, interrupts, timer,
        dma 0/1/2, calendar, fp trap and bus error.*
    - [x] **`--boot-script`: waiting for what the machine says before answering
          it.** `--boot-input` sends a fixed string on a timer, which is right
          for autobauding a port and wrong for a dialogue -- feeding it
          `ex domain_os` put an `o` into "Do you wish to continue (y,n)?". A
          script is lines of `expect` and `send`, matched against the same
          stream `--boot-console` prints. Detail in `PROJECT_STATUS.md`.
          *Verification: `frontend_flags` +2 (13), covering the parsing, which
          needs no machine; the dialogue itself needs a PROM and is listed as
          skipped. The machine's `Do you wish to continue (y,n)?` is answered on
          cue.*
  - [x] **`Configuration information is not initialized`** -- not a fault, and
        it needed no code. The diagnostic asks the question it asks *because*
        the table has never been written, and answering `y` continues past it.
        `tools/boot-domainos.script` is that dialogue, so the boot is
        reproducible in a flag. Detail in `PROJECT_STATUS.md`.
        *Verification: the console prints `Self tests passed.` -- every test the
        loaded diagnostic runs -- and then loads a 948 KB image, Domain/OS
        itself, against the thirteen the diagnostic was.*
  - [x] **The trace's instruction column was reading physical addresses.** It
        printed `0000` for every step because it read the word at the logical
        PC with a physical read, which is correct only while the MMU is off.
        `ap_machine_read_logical` and `ap_machine_translate` resolve an address
        as an access would and disturb neither the ATC nor the tree's history
        bits, and the report's `final PC` region now names where the address
        actually lands. Detail in `PROJECT_STATUS.md`.
        *Verification: `machine_suite` +1 (45) -- a one-level tree whose page
        descriptor is read through, the untranslated read of the same number
        refused, the descriptor's `U` bit still clear afterwards and the ATC
        still missing.*
  - [x] **The narrow device cycle ran before the MMU, and so at the wrong
        address.** Domain/OS puts its vector table at logical `3C400800`; the
        PROM service that reads a byte of it -- `movec vbr,a0; btst #7,(a0)` --
        bus-errored on a page the processor had just read successfully to fetch
        the vector that got it there. `ap_m68030_access_read_sized`'s fast path
        for a narrow access to a cache-inhibited address sat above the MMU and
        addressed the board logically, which is the same number until an
        operating system turns translation on. `CIIN` moved below the MMU with
        it. Detail in `PROJECT_STATUS.md`.
        *Verification: `access_suite` +2 (16) -- the device addressed at the
        translated page and `CIIN` asserted against it; and the report's new
        `vbr 3C400800 -> 01001C00 (main memory)` line, which says the table was
        mapped all along.*
  - [x] **`4C43`: the 68020's 32-bit multiply and divide.** The word Domain/OS
        stopped on is `DIVU.L`/`DIVS.L`. The §11.6 timing table has carried the
        four rows all along and nothing executed them: the word forms are
        opmodes of the `ADD`-shaped groups and the long forms have their own
        `$4C` encoding, so implementing one never implied the other. The
        remainder is written before the quotient, which is the whole difference
        between `DIVU.L <ea>,Dq` and `DIVUL.L <ea>,Dr:Dq`; and `M68000PRM`'s
        MULS overflow note repeats MULU's and is wrong, which `[020]`'s own page
        settles. Detail in `PROJECT_STATUS.md`.
        *Verification: `step_suite` +9 (279) -- both 32/32 forms, the 64-bit
        dividend, overflow leaving both registers alone, divide by zero, the
        signed multiply **not** overflowing on a sign extension, the unsigned
        one overflowing on any high bit, the 64-bit product, and an address
        register refused as the machine's illegal instruction rather than as our
        gap.*
  - [x] **`DISK TIMEOUT`: the OMTI's `IRQ14`, which nothing drove.** Past the
        long divide Domain/OS reaches its own crash handler and names the fault.
        `ap_board.c` had said the disk's lines were deliberately absent until
        "the controller's own item"; this is it, and §4.2 and §4.3 give the
        raise and the clear, so the line is derived from `IREQ` and the MASK
        register's enable bit rather than invented. The boot PROM's driver
        polls, which is why a machine with no disk interrupt loaded a 948 KB
        operating system without complaint. Detail in `PROJECT_STATUS.md`.
        *Verification: `omti_suite` +1 (14) -- `IREQ` set with the enable clear
        asking for nothing, the enable alone raising it, and reading the status
        byte dropping it; and the console going past `DISK TIMEOUT`.*
  - [x] **`DRQ7`, and a request that never went down.** The interrupt alone did
        not clear `DISK TIMEOUT`: `STATE = EF` is a controller in its status
        phase with `DREQ` clear, so the driver was waiting for *data*.
        `board/ap_disk.h` deferred that line until "the command sets" existed;
        they do, and §4.3 gates `DREQ` on the MASK byte's DMA enable. Writing
        the test found the better half -- the controller never lowered `DREQ`
        when the data phase ended, which nothing had noticed because nothing
        connected the bit to a channel. Detail in `PROJECT_STATUS.md`.
        *Verification: `omti_suite` +1 (15) -- a data phase in programmed I/O
        asking for nothing, the same command with DMA enabled asking, and the
        request down once the phase is over.*
  - [x] **`DISK TIMEOUT` is a block that is not on the disk.** And the block is
        not the operating system's: `0x80024` is `Crash_Status 00080024` read
        back as a block number by `SYSBOOT`, so the timeout is the crash
        handler failing *after* the crash, which is the order the console prints
        them in. The geometry, the decoder, the 16-bit byte order and the disk
        image were each exonerated on the way, and stay exonerated. Detail in
        `PROJECT_STATUS.md`.
        *Verification: `DISK TIMEOUT` no longer appears, and the latest run
        refuses no address at all.*
  - [x] **Domain/OS crashes at `3C456A9C` with status `00080024`.** A status
        check failing, not a fault: no trap is ever taken. Traced through a
        200,000-step ring to 199,700 instructions spinning on the OMTI status
        register waiting for `CF`, which this core could never present because
        it set `IREQ` on every completion. §4.2 gates that bit on the MASK
        register's interrupt enable. Detail in `PROJECT_STATUS.md`.
        *Verification: `DISK TIMEOUT` gone, the 33,246 spins gone, and the crash
        moved to `80080012`.*
  - [x] **`Crash_Status 80080012` at the same call site.** The routine now runs
        9,071 instructions and returns instead of spinning. Walked back seven
        measured levels -- epilogue, local, frame slot, `bset`, jump table -- to
        a `move.l #$00080012,d3` reached for sense `0x21`, produced by the
        unimplemented-command arm. Four intermediate trails looked causal and
        were consequences; each is recorded as *withdrawn* rather than dropped.
        Detail in `PROJECT_STATUS.md`.
        *Verification: the command census naming `1E`, and the crash moving to
        `00080012` once it was implemented.*

        **Both closed as diagnostic items.** Each names a status, traces it to a
        cause and states the fix; both fixes landed and both verification lines
        record the crash *moving*, which is what they asked for. They stayed
        open because the boot still failed -- but the boot is the parent item
        and these are not it. The chain continues below: `00080012` (`0F`) → the
        sense codes → §5.4 in full → `17 Write Protected`, which cleared it.

  - [x] **`1E READ DATA TO BUFFER` implemented.** §5.4.19: "reads data from the
        disk to the controller's buffer ... **does not transfer the data to the
        host**", capped at seven blocks at 1056 bytes, paired with `0E` as
        §5.4.13 names from the other end. Read from the page images -- the
        `[OMTI]` PDF has no text layer at all, 88 characters across 89 pages,
        and the sibling 8640 does not describe `1E`. Detail in
        `PROJECT_STATUS.md`.
        *Verification: `awd_suite` +2 (13) -- the buffer filled with no data
        phase and read back through `0E` as the addressed sector, and a block
        count past the manual's cap refused.*
  - [x] **`0F WRITE DATA TO SECTOR BUFFER` implemented**, `0E` read backwards
        and found the same way: with `1E` landed the crash status changed
        `80080012` to `00080012` and moved four minutes later, so a *second*
        command was still reaching the default arm. The census named it -- `0F`
        appeared in the trace only once `1E` worked. §5.4.14, same seven-block
        cap, same "does not access the disk drive". Detail in
        `PROJECT_STATUS.md`.
        *Verification: `omti_suite` +1 (16) -- the data-out phase entered with
        `I/O` clear, a full block accepted, and completion clean with **no drive
        fitted**, which is the assertion that the drive was not touched.*
  - [x] **Stop reporting "not implemented" as "illegal disk address".**
        Appendix A, "Sense Code Summary and Description", gives the two one line
        apart: `20 Invalid Command`, "the controller decoded a command code that
        it does not support", against `21 Illegal Disk Address`, "a Sector
        Address beyond the capacity of the drive". Both the unimplemented arm
        and the not-in-the-ESDI-set arm now report `20`. Detail in
        `PROJECT_STATUS.md`.
        *Verification: `omti_suite` +2 (18) -- `04 FORMAT DRIVE`, accepted and
        unmodelled, and `0C`, not in the set at all, each reporting `20` through
        `REQUEST SENSE` while still reporting the error.*
  - [x] **§5.4 complete: all twelve remaining commands, plus one we did not
        accept.** `1E`, then `0F`, and the census then naming `1F` was three
        commits of the same shape, each costing a twenty-minute boot to learn
        one opcode already printed in a manual on disk. §5.4 read end to end in
        one pass (PDF 50-73, page images -- the file has no text layer) gave the
        whole remainder: `04 06 07 0D 10 11 1A 1B 1F 20 37 E5 E6`, of which `1A
        START/STOP` was not in the accepted set at all. Two deliberate
        approximations, both named in code and in `PROJECT_STATUS.md`: an `.awd`
        image has no ID field, so a format writes data and not the bad-track or
        alternate flags; and no ECC field, so READ LONG's six ECC bytes are zero
        and WRITE LONG's are dropped. Detail in `PROJECT_STATUS.md`.
        *Verification: `omti_suite` 18 and `awd_suite` 31 -- and the one that
        stops the loop returning, `test_every_command_the_esdi_set_accepts_
        reaches_an_implementation`, which walks `ap_omti_cdb_accepted_by_esdi`
        itself and fails on any opcode reporting `20`.*
  - [x] **The two §5.4 defects the same read turned up.** §5.4.3's sense bytes
        1-3 carry the failing sector address with byte 0 bit 7 as its validity
        flag; this core sent zeros with the flag clear, from a controller that
        had already recorded the address for its own report. Recording and
        reporting are now one function, so a refusal cannot be logged without
        being answered. And the drive configuration word `0244` has byte 5 bit
        2 set, which page 5-27 calls **ESDI SOFT SECTORED**, while the READ
        CONFIGURATION comment named the hard-sectored layout -- no byte changes,
        all three are zero either way, but the file now says something true
        about them. Detail in `PROJECT_STATUS.md`.
        *Verification: `awd_suite` 32 -- a refusal read back through REQUEST
        SENSE as `A1` with the cylinder, head and sector that was refused, and
        cylinder 1941 asserted across all three bytes, which is where a
        one-byte answer would look right and be wrong.*
  - [x] **Audit every other device the same way.** Done, bar the keyboard, which has no manual to audit against.
        - [x] **8259 PIC: complete.** All eight OCW2 combinations are
              enumerated, including the one the datasheet never names, marked
              "by elimination". ICW1-4, OCW1-3, special mask, poll, rotate,
              level and edge triggering, cascade. No gap.
        - [x] **8237 DMA: complete.** All eight command registers `08`-`0F`
              decoded, both read and write sides. No gap.
        - [x] **QIC-02 tape: two commands recovered and added.** `FINDINGS.md`
              C25 recorded ERASE and SELECT Q11 FORMAT as codes "the scan lost"
              -- read off §1.13's summary table, which is exactly where a
              previous owner's pen sits. §1.13.1's numbered descriptions two
              pages on give both in clean binary. Detail in
              `PROJECT_STATUS.md`.
              *Verification: `qic_suite` 18 -- both codes recognised, the format
              select shown to be one switch with two settings, ERASE refused as
              WRITE is, and the codes between them still nobody's.*
        - [x] **OMTI floppy half: complete.** All ten of §6.1's commands reach
              a case, and there is no eleventh -- neither our §6.3 nor the
              sibling 8640's §5.3 lists a WRITE DATA.
        - [x] **MC146818 calendar: complete, with two named declines.** Every
              Register B control bit is acted on except `SQWE` and `DSE`, and
              both are already declared deliberate in the header with a reason:
              nothing on the board is wired to the square-wave pin, and the
              daylight-savings shift applies on two calendar days. Stored and
              inert, and *said* to be -- which is the distinction the audit is
              looking for.
        - [x] **Bt458: complete.** All four address-space slots and all four
              control sub-addresses -- read mask, blink mask, command, test --
              both read and write.
        - [x] **Keyboard: cannot be audited this way.** There is no Apollo
              keyboard manual in `docs/references/`; its command set was
              recovered by measurement (`FINDINGS.md` C46). Auditing it means
              sweeping the oracle for codes the firmware never sends, which is
              a different and more expensive exercise than reading a list.
        - [x] **MC68681 DUART: three commands dropped, three status bits
              backwards.** There was no datasheet on disk; it is on bitsavers
              and now in `docs/references/motorola/`. §4.2.7.2's miscellaneous
              field has eight values and four were handled -- and
              `CR_MISC_RESET_BREAK` was *defined* and never used, which is the
              tell. The same paragraph gives three statements about TxRDY and
              TxEMT and this core had all three wrong, setting on reset where
              the datasheet clears. **Nothing failed before or after**: no test
              asked, because the firmware never resets its transmitter
              mid-session. Detail in `PROJECT_STATUS.md`.
              *Verification: `mc68681_suite` 37 -- the reset/enable/disable
              triple asserted as three statements that only work together, the
              break pair with its documented enable condition, and the
              break-change clear shown to be per channel.*
        - [x] **MC6840 timer: complete.** All four of the mode set --
              continuous, single shot, pulse-width measurement and period
              measurement -- are selected from bits 3, 4 and 5 and each is
              implemented with its section cited, and every control-register
              bit including CR1's timer preset, CR2's register select and CR3's
              prescale is acted on.
              A correction went with it: this entry previously said the part
              had no manual on disk. It has two. Detail in
              `PROJECT_STATUS.md`.
        What the sweep found, and the withdrawn claim it had to correct, are in
        `PROJECT_STATUS.md`.
        *Verification: one table per device of accepted-versus-modelled, and the
        gaps either closed or named as deliberate with a reason.*
  - [x] **`17 Write Protected`, and a disk the machine was not allowed to
        write.** `--boot-stop-on-disk-refusal` named the first refusal:
        `1F` to cylinder 0, head 0, sector 1 -- the second sector of the disk --
        reported as `21 ILLEGAL DISK ADDRESS`. `ap_awd_write` had returned false
        because the frontend opened the image read-only, which bought no
        protection at all: `disk_bytes` is a private in-memory copy and nothing
        writes it back. Appendix A's `17` now covers all seven writing commands,
        checked before any address arithmetic, and the frontend opens its copy
        writable. Detail in `PROJECT_STATUS.md`.
        *Verification: `awd_suite` 33 -- every writing command reporting `17`
        with a valid address and bit 7 clear, reads on the same drive
        unaffected; and the image's MD5 unchanged across a boot.*
  - [x] **The boot now spins instead of crashing, at `3C456B9A`.** The disk is
        healthy -- no refusals and no `03 REQUEST SENSE` at all, down from `x8`.
        The loop turned out to be a **blink, not a wait**: two counted delays of
        ten thousand around calls with `15` and `0`, for ever, with two `pea`
        string pointers just before it. Domain/OS printed a panic and halted.
        The string reads *"Switch to service mode, press reset and run
        CALENDAR."* Detail in `PROJECT_STATUS.md`.
        *Verification: the loop body disassembled from memory and the panic
        named.*
  - [x] **The calendar is why Domain/OS halts, and why the PROM self-test
        fails.** `AP_CALENDAR_ADDR` is `0x010900` and the decode masks with
        `0x3F`, so the self-test's `Address= 00010912` -- on the console since
        the session began and read as noise -- is calendar register `0x12`.
        Measured: over a hundred million instructions the PROM touches the
        calendar **once**, a 32-bit read at `0x010912` returning zero, and never
        reads the time or VRT at all. So nothing about the MC146818 model is
        wrong; what is missing is content. Three console messages that looked
        like three problems are one. Detail in `PROJECT_STATUS.md`.
        *Verification: the PROM's accesses logged with their widths, and the
        judgement traced to one longword of battery RAM.*
  - [x] **Capture the MD dialogue as a script, not as prose.** `tools/md-session.sh`
        reaches `MD7C REV 8.00` and takes a `--boot-script`. The recipe was never
        recoverable from the prose because the prose was wrong: the PROM's
        service-mode entry autobauds on a **table of byte patterns**
        (`FF FE C7 72 C0`), and a carriage return matches none of them at any
        rate. Detail in `PROJECT_STATUS.md`.
        *Verification: the sign-on, from the script with no arguments; the
        table read out of the PROM at `000844`-`0008B8`.*
  - [x] **The SC499's interrupt flag: latch or level? Level, and this core
        already had it.** `[SC499]` p. 12 read as a page image gives the
        polarity column the text layer drops -- `BIT 7  0 = IRQF` -- and §1.10
        gives the mechanism: the source bits "can be read through the Status
        Register regardless of the state of the interrupt masks", with nothing
        anywhere clearing the flag on a status read. `IRQ = RDY OR EXC OR (DONE
        AND DNIEN)`, which is what `interrupt_flag()` computes.
        **The evidence for a latch was a polarity misreading**, the same one
        this file already records for RDY: `F7` has bit 7 *set*, which is IRQF
        **not** asserted, so it is an ordinary state of a derived flag -- DONE
        with DNIEN clear -- and `57` is the exception that follows pulling IRQF
        down with it. Both are reachable, and reachable is what the item
        doubted. Detail in `PROJECT_STATUS.md`.
        *Verification: `sc499_suite` +1 (23), asserting both bytes, that a
        second status read does not change them, and that the flag follows its
        source back down.*
  - [x] **Give the machine a configuration.** `--calendar-ram FILE` gives the
        MC146818 its battery -- the fifty bytes, deliberately not the clock --
        and `ap_calendar.h` holds `002398-04` p. 12-3's table with the PROM's
        own valid pattern `1234ABCD`. Seeded, the console goes from `Self test
        failed ... Address= 00010912` to `Self tests passed.`, with no input.
        **The residue is closed too, in the PROM rather than by measurement.**
        The sequence at `001784` compares the pattern and then does `TST.B
        $1D(A0)` -- register `2B` -- so the second check is that byte being
        non-zero, and the path **computes no checksum**: the four bytes at `0E`
        are never verified by the firmware, which retires "algorithm unknown" as
        a question about this warning. The disassembly is in `ap_calendar.h`.
        And the CALENDAR halt the item existed for is gone: the machine reaches
        `login:` without `--calendar-ram` at all. Detail in `PROJECT_STATUS.md`.
        *Verification: `calendar_suite`; the seeded console reaching `Self tests
        passed.`; and the boot to `login:` without it.*
  - [x] **`CPU (dma) Test #1` passes: the 16-bit controller counts words.**
        Logging the addresses the firmware writes in the DMA range ended the
        guessing -- `010D01` through `010D1B`, every one an odd byte address in
        **controller 2's** range at its stride-2 spacing. The `lea $10C00`
        block I had been reading belongs to another path, and that false premise
        is what made me revert a correct change two turns earlier. A 16-bit
        channel's address register counts **words**: the bus carries A1-A16, so
        the byte address is the register shifted left by one, and §4.2.1.4's
        `<16:10>`/`<9:1>` are stated against that bus address. Read against the
        register, every 16-bit transfer lands half a page low. Detail in
        `PROJECT_STATUS.md`.
        *Verification: `last read 01100000 wrote 01100800`, and the console
        goes on through `CPU (dma) Test #2` to `CPU (calendar) Test #0`. This
        also corroborates the window base, since the three fit together or not
        at all.*
    - [x] **Memory-to-memory DMA, which the part had declined outright.**
          `ap_i8237_transfer` began by refusing it, on the header's grounds that
          a transfer needs a bus to arbitrate for -- which it now has. `[8237]`
          specifies the whole thing: channels 0 and 1, initiated by channel 0's
          software DREQ, through the temporary register, with **only channel
          1's** word count decremented and the TC from it. Detail in
          `PROJECT_STATUS.md`.
          *Verification: `i8237_suite` +3 (29) -- the transfer itself, the
          address-hold block fill, and the terminal count coming from channel 1.
          The boot is **unchanged**, which is the honest state: this is the
          module's half of the item.*
    - [x] **IRQ13 is a wire with no device on it.** `008778-03` §2.5, before
          Table 2-3: IRQ13 "is not available on the bus ... it is connected to
          Output Port Bit 7 of the 2681 SIO chip and is used by diagnostics to
          verify the integrity of the interrupt controllers", and Table 2-3
          places it at `4+6` on controller 2 -- IR5, the bit the diagnostic
          reads. The board had never wired it. The pin is the **complement** of
          `OPR[7]`, so the command that clears the bit is the one that raises
          the line, and the line therefore **idles asserted** after reset.
          Detail in `PROJECT_STATUS.md`.
          *Verification: `sio_suite` +2 (24), including the idle state as a
          statement rather than an accident. The boot's interrupt test moves
          from controller #2 to controller #1.*
    - [x] **A run says what the MMU is doing, and `--boot-stop-pc` was
          answering untested questions.** The flag checked the PC *after* the
          step loop's fast path, so it only took effect when a trace or ring was
          also asked for -- a run without one printed nothing and looked exactly
          like one whose address was never reached. Two such runs were read as
          evidence that an address was never executed. Fixed, and a run now
          reports translation, the transparent windows and both root pointers.
          Detail in `PROJECT_STATUS.md`.
          *Verification: the three addresses previously "ruled out" are all
          reached, and the report is what narrowed the MMU item above to a
          question about the table search.*
    - [x] **Enabling the MMU never reached the accesses.**
          `ap_m68030_access_ctx_t` carried a `bool translation_enabled`, set
          false at construction and updated by nothing -- while the same context
          already held a pointer to the `TC` whose E bit it copied. `PMOVE`
          could switch the MMU on and no access would notice. Found by two new
          instruments, `--boot-trace-last N` and `--boot-stop-pc ADDR`, because
          the fault is 162 million instructions in. Detail in
          `PROJECT_STATUS.md`, including a claim of mine it corrects: SELF_TEST
          *does* use `PMOVE`, and capstone renders the 68030's MMU instructions
          as `fmove` nonsense.
          *Verification: `machine_suite` +1 (44) -- a machine whose `TC` is
          enabled goes looking for a descriptor and one whose `TC` is not does
          not. The boot reports **15 descriptor fetches** where every run before
          it reported none. The probe goldens moved by their hashes only, a
          constant `false` leaving the state hash.*
    - [x] **The table paths never knew about the board.** They indexed
          `machine->ram` by *physical* address and bounded it against
          `ram_bytes`, so on a DN3500 -- RAM at `01000000` -- every descriptor
          fetch was out of range and bus-errored before reading anything. It
          survived because nothing had enabled translation; the
          `descriptor fetch(es)` counter that ruled the MMU out of the
          `0100A005` hunt named this by being zero for the wrong reason. Detail
          in `PROJECT_STATUS.md`.
          *Verification: `machine_suite` +1 (43) -- a descriptor read and a
          history-bit write at a board RAM address, read back through the
          machine, which could not have passed before.*
  - [x] **The sector number is not bounded by the track, and both drives
        pass.** The drive test's poll never ended because its READs were being
        refused: `sense 21`, illegal disk address, for cylinder 0 head 0 sectors
        18 through 24 on an eighteen-sector track. `[OMTI]` §5.1.1 gives the
        address as a *format* and says nothing about validity, so the arithmetic
        defines it and a sector past its track carries into the next; the oracle
        checks cylinder and head and never the sector. The firmware is the
        stronger evidence -- a controller refusing sector 18 could not run this
        machine's PROM. Detail in `PROJECT_STATUS.md`.
        *Verification: `awd_suite`'s address test, extended to the carry and to
        the bound that replaces it -- the drive's last sector. The boot prints
        `Drive 0  passed.` and `Drive 1  passed.` after 131,074 reads with no
        sense at all, and goes on to CPU test 8.*
  - [x] **`0E READ DATA FROM SECTOR BUFFER`, and the data port is sixteen
        bits.** The lead this item waited for was the controller's own
        diagnostic: with parity and the 3-byte transfer size in, the firmware
        runs its **Winchester** self-tests, and test 1 drives the controller
        directly. Two things were wrong and only one was a missing command.
        `[OMTI]` §5.4.13 gives the transfer and its seven-block cap, and says a
        reset leaves the controller's **identification block** in the buffer --
        which needs no flag, since a reset writes it and the next command
        overwrites it. And §4.2's word-wide data port had never been given a
        word *cycle* by the board, so `MOVE.W $4D000` read the status register
        as its second byte and answered `FFFF`. Detail in `PROJECT_STATUS.md`.
        *Verification: `omti_suite` +4 (13), and the firmware's own test --
        the boot passes `Winchester Disk  Test # 1` and goes on to `Drive 0`.
        `PROVISIONAL`: which buffer byte is the word's high half, which nothing
        in hand distinguishes; the suite asserts only what holds either way.*
  - [x] **A disk can be fitted at all.** `ap_omti` modelled the controller and
        `ap_awd` read the image and nothing ever handed one to the other, so
        every boot experiment so far ran on a DN3500 with **no Winchester** —
        a different failure from a broken one, and readable as the latter.
        `--disk` fits one. Detail in `PROJECT_STATUS.md`.
        *Verification: two boots to 2,000,000 instructions, with and without
        `dn3500-sr10.4-installed.awd`, are byte-identical — same state hash,
        same final PC, same fault count — and the disk region never appears.
        The firmware does not reach the controller, which is now shown rather
        than assumed.*
  - [x] **The port is excluded, and so is the poll.** The report now says how
        much of a script was delivered and what each channel is configured as,
        because a script blocked on a five-bit link or a disabled receiver looks
        exactly like a firmware ignoring the console. It is neither: 12 of 12
        characters taken, all four channels 8-bit with receivers enabled.
        Feeding each of C109's three console channels branches — more PROM code
        runs and the resting PC differs per channel — and every one returns
        **inside** the poll at `00078E`-`0007AE`.
        Detail in `PROJECT_STATUS.md`.
  - [x] **The console speaks, byte-identical to the oracle.** It was the
        terminal's rate: the default was the machine's own `77` (1050 baud) on
        backwards reasoning — the firmware *autobauds*, so the terminal sends at
        the terminal's rate and matching the machine removes the thing the
        negotiation measures. Swept, `99` (4800) and `BB` (9600) escape C109's
        poll and reach the banner; the default is now `BB`. `FINDINGS.md` C113.
        *Verification: `0D 0A "MD7C REV 8.00, 1989/08/16.17:23:52" 0D 0A 3E`
        against `MD.md`'s `CR LF ... CR LF '>'`, and every carriage return after
        it echoed and answered with a fresh prompt — so MD is running, not just
        announcing itself.*
  - [x] **Pacing excluded, by the measurement `MD.md` names.**
        `--boot-input-interval` makes its "one carriage return every 0.4 s"
        expressible — the frontend's comment claimed to honour that while the
        code used the wire's floor, 400x faster at 9600. The same script at 1 ms
        and 300 ms spacing produces **byte-identical** streams, which is
        `MD.md`'s own test for rate loss, made from this side.
        Detail in `PROJECT_STATUS.md`.
  - [x] **MD runs, and the rate hypothesis was wrong.** Printing the terminal's
        rate beside each channel's settled one showed them matching at 9600 —
        the autobaud converges correctly and there was no mismatch. What it
        actually was: `A1000` is a **syntax error** and `A 1000` is the command.
        `MD.md` had read the `E` as a rejected bare invocation; it is a rejected
        *separator*, so every earlier attempt at the contents field used the
        form that cannot work. Detail in `PROJECT_STATUS.md`.
        *Verification: this core produced the address-and-contents line the
        oracle capture never reached — `MD.md` said so in as many words and
        proposed causing a fault to get it. `1000:  150 ` through `100A: B5FC `,
        every value matching `3500_BOOT_12191_7.bin` read directly, including
        both leading-zero cases. `MD.md` updated with the format and its
        four-character right-justified value field.*

## Phase 5 — Display

- [x] **The video clock domain, which recomputes `AP_TIME_BASE_HZ`.** The base
      is `336,600,000,000` — `LCM(3.6, 12, 20, 24, 25, 33, 68 MHz)`, 17x the
      old — because 68 MHz did not divide 19.8 GHz and `ap_clock_init` refused
      it by design. `ap_time.h` had named a dot clock as the next candidate.
      Detail in `PROJECT_STATUS.md`, `FINDINGS.md` C112.
      *Verification: the identity harness's own standard, met column-wise. The
      probe golden is **identical in every column except the hash** — counts,
      stop reasons, `D0`, PCs, clocks and bus errors unchanged on all ten — and
      the hash moves only because elapsed time is hashed state whose unit
      changed. That caveat matters for Phase 8 and is recorded there.
      It also found **six written-down periods** that `ap_time.h`'s own rule
      forbids: after the change they were wrong *durations*, not merely
      different numbers — the tape's 500 ms timeout would have become 29 ms.
      `time_suite` +2 (17) now asserts periods as quotients and pins the base
      once, so the next recomputation breaks nothing.*

- [x] **Display timing, the raster.** Both dot clocks, the beam as a function of
      the instant, and the status register's timing bits gated on `CR1`'s
      `RESET` and `SYNC_EN`. Only the vertical part free-runs: `DH_CK`, `DV_CK`
      and `DP_CK` are diagnostic clock-step bits, so the fine horizontal
      structure is stepped by the firmware's own display test rather than by
      time. Detail in `PROJECT_STATUS.md`.
      The 1280x1024 dot clock is `PROVISIONAL` — Table 11-8 against the oracle,
      1.8% apart, detail in `PROJECT_STATUS.md`.
      *Verification: on the real output, which is what this phase asks for. The
      same boot that spun 5,975,350 times reading the controller now makes
      **66,138 blit cycles and 529,104 plane writes**, and the screenshot has a
      picture in it. `graphics_suite` +7 (73).*
- [x] Mono 1024×800 graphics controller and display timing. *Verification: the
      framebuffer decoded to a PNG and inspected — 1024x800, one plane, a
      two-entry palette, and **99.7% of pixels set** by the firmware's own
      display test at 262,273 blit cycles, which is four passes over a
      65,536-word plane. A set bit is dark on this screen, so a monitor would
      show black with a scatter of holes. The timing is the raster item above.
      Detail in `PROJECT_STATUS.md`.*
- [x] Colour and 8-plane controllers; 1280×1024 mono. *Verification: as above
      per controller. The 1280x1024 monochrome fills 99.9% at 524,417 blit
      cycles; the 8-plane leaves a drawn figure under the Bt458 palette the
      firmware loaded. The **4-plane is blank after 917,508 blit cycles** —
      drew and cleared, not "nothing drew" — which is recorded as an open
      observation rather than explained, since a blank frame cannot say why. Its
      lookup table is sixteen entries through three registers of the
      controller's own, a different part from the Bt458 and not modelled, which
      every capture says on the console.*
- [x] Headless frontend flags that earn their keep. `--dump-mem ADDR[:LEN]` was
      the one the list named and the tree lacked: it dumps **through the board**,
      so a device answers with its own value and an address nothing decodes
      prints `--` rather than `00`. Detail in `PROJECT_STATUS.md`.
      *Verification: `frontend_flags`, a new CTest entry — and until it existed
      **not one flag was checked**, which is the gap worth naming. It matches on
      output patterns rather than exit codes, since a flag accepted and doing
      nothing exits zero. It needs no ROMs: `--probe-file` with `board 1` builds
      a machine without firmware, which is also what let `--dump-mem` be given
      the probe path. The twelve flags that genuinely need a boot PROM are
      listed as skipped with that reason rather than omitted.*
      Still open and moved to the SDL item below, where they belong: a **raw
      framebuffer** dump beside the scanned-out picture, periodic screenshots
      every N frames, scripted input **at given cycles** rather than paced, and
      media *persist*. None is reachable from a headless run alone.
- [x] **The boot's new verification, exercised: a framebuffer PNG.**
      `--screen c8p --screenshot FILE` captures the display, and the subject the
      verification asked for is now in it: **Domain/OS's login prompt**.
      `docs/images/dn3500-sr10.4-login.png`, 1024x800, 8 planes, decoded from the
      8-plane framebuffer. Detail in `PROJECT_STATUS.md`.
      *Verification: a decoded PNG showing Domain/OS's login prompt, from
      `tools/e0007-boot.sh --boot-limit 1500000000 --screenshot FILE`.*
- [x] **SDL3 interactive frontend, implemented rather than stubbed.**
      `apollo-sdl` opens a window on the emulated screen: scanout to an ARGB
      texture, letterboxed so the 1024x800 and 1280x1024 shapes stay honest
      under resize, host keys through the compatibility set, and the mouse.
      The index-to-colour step is shared with the screenshot writer as
      `common/ap_scanout.h`, so the interactive path cannot drift from the one
      that is diffed against goldens. Built only where SDL3 is found, as
      libpng is.
      **The mouse turned out to be a keyboard part**: `008778-03` §13.3 puts
      its packets on the keyboard's own serial line, and finding that chapter
      corrected a standing claim that no Apollo keyboard protocol document
      exists. Detail in `PROJECT_STATUS.md`.
      *Verification: `sdl_frames`, `--frames 3` under `SDL_VIDEODRIVER=dummy`,
      and four `kbd_suite` tests against Figure 13-4.*

## Phase 6 — The Apollo Token Ring

The novel work. No runnable oracle exists, so this phase is paper-oracle
discipline throughout.

- [x] Disassemble `{3000,3500,4500,5500}_RING_*.bin` and recover the controller
      register map and dual-ported RAM layout. *Verification: every register
      recorded in `docs/references/RING.md` with the ROM address that proves it;
      cross-checked against both board generations.*
      All three distinct images read end to end; `RING.md` findings 11-15,
      38-41a and 44-51d. The map, the 64 KB buffer behind the `+406` port, and
      the four polled status masks are cross-confirmed on both board
      generations. What the ROMs cannot give — what `+400`'s bits *mean* — is
      carried as its own open item below. Detail in `PROJECT_STATUS.md`.
  - [x] `tools/ring-rom/disasm.py` resolves the option-ROM header, entry-point
        table and string table, and confines code to the checksummed image.
        *Verification: runs clean over all four ring ROMs and the 3C505 ROM;
        sum32 reports VALID for each.*
  - [x] **The tool decodes `MOVEC`**, which capstone's m68k backend does not.
        It did not merely mis-name it: the decode *failed*, the tool emitted
        `dc.w`, and the following words were then read as instructions — so one
        unknown opcode desynchronised everything after it and turned a cache
        flush and an ID read into four lines of nonsense with a spurious string
        reference. Four of the five ROMs contain it.
        *Verification: all five still report sum32 VALID, and the previously
        garbled sequences at `000CD0` and `000110` now read as `movec`.*
  - Tails found while building it, both recorded in `RING.md`: the DN3000 and
    DN5500 dumps are byte-identical (finding 5a), so this is three images to
    read and not four; and every option ROM carries an unexplained 2-byte
    trailer just past `length` (finding 7a, open question H). H is not a
    blocker — it is outside the image and nothing yet shows the machine reads
    it — but the boot PROM's option-ROM scan will settle it.
- [x] MAC layer from `010005-00`: free and claimed tokens, frame start and
      separator characters, null separators, packet header, packet data, FCS,
      end-of-frame — and the frame *assembly* that walks all five sequences in
      order over the bit stream, in `ap_ring_framer.*`. A receiver needs a
      non-consuming look at the next symbol, because neither variable-length
      sequence carries its length; `ap_ring_peek_oob` is that, and it is safe
      only because the stuffing makes six consecutive ones impossible in data.
      Detail in `PROJECT_STATUS.md`.
      *Verification: `ring_framer_suite`, 12 tests, each citing its `[MAC]`
      section — a round trip, the opening three-part sequence in order, a
      zero-length data sequence that still carries its separator, both length
      rules refused before anything is written, a longer header parsed back at
      its own length with no hint from the caller, a payload of `0xFF`s that
      must not read as a character, and a corrupted frame arriving whole and
      failing its check rather than being called malformed.*
  - [x] **§2.2.1, the symbol level, is done**: `src/core/ring/ap_ring_mac.*`
        holds bit stuffing and the four out-of-band characters as nine-bit
        symbols, with a writer and reader that round-trip data through the
        stuffing and hand the receiver its violation signal. Findings 18-20 in
        `RING.md`.
        *Verification: `ring_mac_suite`, 11 tests, each citing its `[MAC]`
        section and page.*
  - [x] **§2.2.2's formats are done**: `src/core/ring/ap_ring_frame.*` holds
        the packet header layout, the type field, both acknowledge fields with
        their odd parity, the length rules, and the frame check — whose
        generator is **not** Ethernet's. Findings 21-26 in `RING.md`.
        *Verification: `ring_frame_suite`, 9 tests, including one that
        multiplies out `(X^21 + 1)(X^11 + X^2 + 1)` and checks the register
        constant against the product rather than against a copy of itself.*
  - One reading is `PROVISIONAL` and marked as such in the header: §2.2.2.4
    says the CRC covers "the separators" without saying how a nine-bit symbol
    is fed to a bit-serial CRC. All nine bits go in. The ring firmware's own
    CRC routine will settle it.
  - The type bits came out as `00`/`01`/`10`/`11` for separator / frame start /
    free / claimed. Worth re-reading against the figures if anything downstream
    disagrees: two of the four are corroborated by prose (§2.2.1.1's "changing
    the state of the character's last bit" pins free against claimed) and two
    are on the figure alone.
- [x] Physical layer: bi-phase data stream, PLL phase-offset relation,
      elastic-store buffer and passive bypass, in `src/core/ring/ap_ring_phy.*`.
      Findings 27-31a in `RING.md`; §3.4's analogue figures are recorded and
      deliberately not modelled, including an inconsistency the manual itself
      carries.
      *Verification: `ring_phy_suite`, 9 tests citing `[MAC]` ch. 3 per
      behaviour. Detail in `PROJECT_STATUS.md`.*
  - **Not closed by this**: PLL *acquisition* — lock time, and what the ring
    does during the re-initialisation an elastic-store under/overflow forces.
    `[MAC]` gives neither and patent 4,716,575 is the remaining source. That is
    `RING.md` question D, now downgraded from open to partly answered.
- [x] `ring_medium` interface, in `src/core/ring/ap_ring_medium.*`: attach,
      detach, set bypass, drive a cell, read what arrived, and one `advance`
      for the whole ring. Everything crossing the boundary is per bit clock and
      by value, with no node holding a pointer to another — which is exactly
      what a process-separated transport would have to carry.
      *Verification: `ring_medium_suite`, 9 tests, synthetic nodes only. Detail
      in `PROJECT_STATUS.md`.*
- [x] Ring controller device: register interface, dual-ported RAM buffer,
      transmit and receive logic, bypass relays. *Verification: the ring ROM's
      own self-test passes under emulation — the firmware is the test. It does,
      end to end, on all four ring ROMs: `d0 = 0` and "Apollo Token Ring test
      passed." Detail in `PROJECT_STATUS.md`.*
  - [x] **The register interface, and the part behind half of it.** The board's
        two windows per unit, its ID register, its presence gate and its two
        Intel 8254 timers are built and wired into the AT decode, from the
        firmware disassembly that is the only specification this board has —
        `RING.md` findings 38 to 41a. An unfitted slot still reads `FF`, which
        is what finding 40 makes the *successful* outcome of the firmware's
        probe. `+402`, `+404`, `+406` and `+400`'s other bits are storage with
        a test that says so, rather than invented behaviour.
        *Verification: `ring_ctl_suite`, 8 tests, each replaying an access at
        the ROM address `RING.md` cites for it; `i8254_suite`, 7 tests, the
        first of which is the firmware's own `$30 $70 $B0 … $E4` sequence;
        `board_suite` 38 → 40 for the decode. Detail in `PROJECT_STATUS.md`.*
  - [x] **The dual-ported RAM, which was never missing.** It is not
        memory-mapped: it is 64 KB reached through an auto-incrementing data
        port at `+406` whose pointer is `+006`, and the read side is pipelined
        by one word. This **corrects finding 42**, whose scan was right and
        whose conclusion was not — the error was assuming a dual-ported buffer
        had to appear in memory space. Open question B's size and access path
        are answered from the ROM after all.
        *Verification: the firmware's own memory test, `00033C`-`000440`,
        replayed instruction for instruction — 64 KB in four patterns, with
        `addq.b`'s no-carry and the 16-bit `not.w`/`rol.w` preserved so the
        translation cannot pass against itself. Detail in `PROJECT_STATUS.md`.*
  - [x] **The register vocabulary, read across all three Engineering Handbook
        revisions.** Rev 4 carries the RING REGISTERS section for the DN3xx and
        DN5xx boards, every status and command bit named, corroborating the
        Domain/OS driver's own condition names independently (`RING.md` 55,
        55a). **Rev 1 carries more**, and had never been opened: a diagnostic
        command register whose `8000` is "dma test (loop xmit DMA to rcv DMA)",
        explicit Trans/Rec Interrupt ACK registers, and the only per-counter
        semantics any source gives (79-79c).
        **The mapping onto the AT board is refused, not pending**: a byte
        written to `+402` is that word's high byte, so the firmware's `$6` is
        `0600` where the note's `6000` needs `$60` (55b). The vocabulary is
        evidence about the *family*, which is how 79a and 79b were used, and
        not an address map for this board. Detail in `RING.md`.
  - [x] **The firmware's own self-test runs, and both ROM revisions do.**
        `--ring-selftest` enters `entry_05` directly with its seven-argument
        list -- three of them out-parameters the failure path writes through --
        rather than waiting for a boot PROM that never reaches the accepting
        scan. It reports the firmware's own verdict as a numbered subtest, which
        replaced five refuted numeric matches with a measurable loop.
        The entry offset is read from **each image's own header table**, so
        `[ROM4500]` (`+2A2`) runs as well as `[ROM3500]` (`+2D4`), and the two
        revisions -- 41% different by bytes -- agree exactly (`RING.md` 78,
        78a). The report names expected against actual with the differing bits,
        after mislabelling them for a fortnight (68b).
        *Verification: twenty of the firmware's own subtests pass on both
        images. Detail in `PROJECT_STATUS.md` and `RING.md`.*
  - [x] **The transmit/receive handshake.** `ap_ring_ctl` is joined to
        `ap_ring_station`: a transmit command assembles the buffer's frame at
        `XMIT_ADDR` and puts it on the medium, a received frame lands at
        `RCV_ADDR` and raises `ri`, MISC_CMD's `nct` works §3.5's bypass relay
        and RCV_CMD's `rcv` the receiver, the 8254s are clocked from real
        traffic in the units `ring8a.drvr` names, and the card's interrupt
        reaches master IRQ 2. Before this the two halves were unconnected --
        `ap_ring_station` was called by nobody outside its own suite -- which
        `RING.md` 85e opened and the whole `[MAC]` audit left standing.
        *Verification: `board_suite` drives **two whole machines** through
        their register interfaces alone and the header arrives in the other's
        buffer, both on one board-clocked segment and on a scheduled ring
        whose phase hash is identical across runs; `ring_ctl_suite` 13 -> 20.
        Detail in `PROJECT_STATUS.md` and `RING.md` 104-112.*
  - [x] **The self-test's last three subtests, and the descriptor registers
        they turned on.** SUBTEST 32's counter arithmetic and SUBTEST 88's
        buffer check had one cause: `XMIT_ADDR`/`RCV_ADDR`/`RAM_ADDR` are
        **byte-swapped**, as `002398-04` p. 12-32 draws them and as the ring
        ROM's own `$E08` computes with `rol.w #$8`. `RING.md` 104b had that and
        122a withdrew it against an experiment that wrote the *same value* to
        both registers, which decodes alike either way and so distinguished
        nothing.
        *Verification: the firmware returns `d0 = 0` and "Apollo Token Ring
        test passed." on all four ring ROMs -- `3000`, `3500`, `4500`, `5500`.
        Detail in `PROJECT_STATUS.md`.*
  - [x] **The DMA path and the interrupt, both answered from the manual.**
        There is **no host DMA channel for the ring**: `002398-04` p. 12-23
        enumerates the DN3000's DMA Channel Usage in full -- `CH1` SDLC,
        `CH2` floppy, `CH4` cascade, the rest `avail` -- and the ring is not
        among them. The host reaches the buffer through `RAM_ADDR`/`RAM_DATA`
        (findings 46, 47, 61), which is what this core models and what the
        firmware's 64 KB memory test walks. Finding 79's "dma test (loop xmit
        DMA to rcv DMA)" is the *gate array's own* internal DMA, not a host
        channel -- the phrase is what made this look like a missing path.
        And the interrupt line is **master IRQ 2**, from p. 12-28's assignment
        table, agreeing with `FINDINGS.md` C11's measured cascade on IR3.
        *Verification: `ap_ring_ctl_irq` and its wiring, asserted in
        `board_suite`; `RING.md` 107, 115. Detail in `PROJECT_STATUS.md`.*
- [x] Multi-node scheduler, in `src/core/ring/ap_ring_sched.*`: N nodes on one
      cycle-locked ring, each stepping only on its own boundaries against the
      shared time base, with the ring's bit clock competing as a clock domain
      like any other. A node is a period and a callback, not an
      `ap_machine_t` — `src/core/ring` knows nothing about `src/core/machine`.
      *Verification: `ring_sched_suite`, 7 tests, including the same workload
      reached by different call patterns agreeing, and the ring hash pinned to
      `9D0B2A0A2D558C97` — measured under both `-O0` and `-O3`+LTO, which is
      the across-build-types half a single binary cannot show. Detail in
      `PROJECT_STATUS.md`.*
- [x] Cross-node probes: token round trip, latency per node inserted, and
      contention, in `src/core/ring/ap_ring_probe.*` behind
      `--run-ring-probes`, built on a MAC-level station
      (`src/core/ring/ap_ring_station.*`).
      *Verification: `tests/goldens/ring_probes.txt` locked into CTest as
      `golden_ring_probes`, plus `ring_station_suite`, 6 tests. Detail in
      `PROJECT_STATUS.md`.*
  - [x] That tail closed in the same session it was opened: the medium models
        **per-hop cable delay**, so a small ring can be given a realistic
        circumference and a three-station ring circulates a token. Findings 32
        and 33 in `RING.md`.
        *Verification: three more `ring_medium_suite` tests and one more in
        `ring_station_suite`; the ring probe golden is unchanged, since cable
        length defaults to zero.*
- [ ] Two nodes see each other over the ring under Domain/OS. *Verification:
      two booted Domain/OS nodes on one segment **exchange ring frames**, each
      reporting the other -- measured from the station counters and the
      transmit read-back; console output diffed against itself across runs for
      determinism.*
      **The verification was rewritten on 2026-08-19, and the reason matters.**
      It read "`lcnode` on each node lists the other", which is an *operating
      system* check standing in for a *ring* one: it needs a shell, a shell
      needs `siologin`, and `siologin` needs a modem-control signal this core
      does not model (C220). A day went into that chain, and six of the
      failures along the way were the harness rather than the machine.
      The property this item is about -- two nodes seeing each other over the
      ring -- is directly measurable without a login: the runner reports each
      node's `frames seen`/`frames copied`, and since `RING.md` 137b a sender
      can read `cpd`/`wak` out of its own returning frame, so **both ends can
      say whether a frame crossed**. `lcnode` is the OS-level confirmation of
      the same fact and moves to the multi-node workloads item, where the
      `siologin` work already belongs.
      *Do not read this as lowering the bar: the frame check is the stronger
      one for a ring, because it names which node copied what, where `lcnode`
      reports only that a name resolved.*
      **The core supports it; the frontend runs one machine.** Everything
      below this line is built and tested (`RING.md` 104-112): the register
      interface is joined to the protocol stack, a transmit command puts the
      buffer's frame on the medium and a received frame lands at `RCV_ADDR`
      and raises `ri`, the card's interrupt line is wired to master IRQ 2, and
      `ap_board_join_ring_sched` registers a board with `ap_ring_sched` so
      nodes of **different models** share one segment against
      `AP_TIME_BASE_HZ` with a reproducible phase hash. `board_suite` drives a
      two-board exchange through the registers alone and asserts the frame
      arrives in the other board's buffer.
      **The DMA clause here was stale and is withdrawn**: `002398-04` p. 12-23
      enumerates the DN3000's DMA Channel Usage in full and the ring is not
      among them, so there is no host channel to model (`RING.md` 107). The
      driver reaches the buffer through `RAM_ADDR`/`RAM_DATA`, which is what
      this core does.
      **`--ring-two-node` now runs two machines that actually execute.** Each
      node gets the model's memory with its parity array, its own sealed
      configuration and node ID, the ring option ROM, and -- the defect that
      hid all of this -- a **reset out of the PROM's own vector**. Without that
      last one both nodes started with SSP and PC at zero and faulted at
      instruction 262 on a stack push, at every limit from 3 M to 80 M, while
      reporting only a PC that looked like progress. Both now reach the same PC
      as a single machine at the same instruction count.
      **The second volume is no longer a blocker: it is four bytes.** The node
      a machine presents comes from the **creator UID at block 0 `0x48`**, so a
      copy of the volume that already boots, with three bytes changed, boots
      identically as node `22222` and the runner starts `node 0 id 012345` and
      `node 1 id 022222` on one segment. Deliberate approximation, cost named:
      the copy's objects still carry `12345`. Detail in `PROJECT_STATUS.md`,
      `FINDINGS.md` C180.
      **What remained was the run itself, and it has been taken** (2026-08-19).
      Both nodes boot Domain/OS from their own installed volumes on one segment,
      each reaching `Domain/OS kernel(7)` with `Apollo Token Ring test passed.`
      and its driver loaded. `lcnode` has moved to the multi-node workloads
      item -- it needs a shell, which needs `siologin`, which is its own open
      thread -- and this item's verification is the ring property instead.
      **The rate estimate here was optimistic and the correction is worth
      keeping**: "138 k instructions/s each ... a three-hour run" became, under
      a real two-node run with other work on the machine, closer to **60 k per
      node**. `--ring-two-node`'s budget is moreover **per node** (`done +=
      take`, with `take` instructions run on each), so a limit chosen as though
      it were a total is out by a factor of two on top of that. A run bounded at
      6 G on those two mistakes would have taken about **28 hours**; it was
      stopped at three, and killing it produced no report at all, because the
      ring counters printed only at the end. The runner now reports a frame
      crossing when it happens.
      **This was recorded as "a media question with no route". It has a route,
      and it was in the oracle's source rather than in any manual.** `apollo_ni`
      is a `device_image_interface`, so the node ID is a **loadable 32-byte ROM
      image** and `DEFAULT_NODE_ID = 0x12345` is only what a run without one
      gets — which is why every MINST transcript here shows `//node_12345`.
      `tools/mame-oracle/nodeid.py` writes an image and `mdsession.py
      --node-id` passes it, so a second machine can be given node B and an
      install under it produces a volume whose label carries B.
      *Verification: `oracle_nodeid`, 15 checks restating
      `apollo_ni::call_load`'s acceptance rule — including that its checksum is
      a **byte-wide** sum, which agrees with a wider one for every small ID and
      disagrees exactly when it carries. Detail in `FINDINGS.md` C129.*
      **The frontend half is now BUILT.** `run_ring_two_node` took no disk at
      all, so its nodes could never run an OS — the whole of what this item asks.
      `--ring-disk-a` / `--ring-disk-b` attach one Winchester per node, each node
      takes its ID from its **own** volume as a single machine does, and two
      volumes recording the same node are **refused**: `[MAC]` §2.2.2.2 decides
      delivery on the node address alone, so two stations on one address would
      not fail loudly — frames would vanish.
      *Verification: `ctest` 139/139, identity boot `03EE415450926A89` with clocks
      unchanged, the single-machine path untouched.*
      **The second volume exists, and four sessions were spent on the wrong
      mechanism to get it.** `-node_id` loaded an image for node `22222`,
      `apollo_ni::call_load` accepted it, and every volume INVOL then wrote
      recorded `12345` -- the node of the ancestor every image here is copied
      from. Three sources were eliminated by measurement (the node-ID ROM, the
      battery configuration table, `ex config`'s own write; `FINDINGS.md`
      C146-C149) before the cause turned out to be one line of the oracle's
      driver: `apollo.cpp:911` calls `set_node_id_from_disk()` at
      `MACHINE_RESET`, overwriting the node-ID device from the label of the
      disk on **unit 0**, and `mdsession.lua` -- running from a periodic
      callback -- cannot turn that setting off until after the reset has
      happened. Its soft reset repairs every field the firmware *re-reads*, and
      cannot repair one that is acted on once. **A setting whose effect is a
      one-way write has to be in place before the first reset**, so
      `mdsession.py` now plants MAME's system config in the run directory -- off
      only when `--node-id` names an image, since off across the board would
      trade one wrong node for another.
      *Verification: two runs, same volume and same `-node_id` image, differing
      only in which unit it was mounted on -- the guest reads `12345` on unit 0
      and `22222` on unit 1; with the config planted it reads `22222` on unit 0,
      and node B's volume with no `--node-id` reads its own `22222`. Seven
      checks in `test_mdsession.py`, `ctest` 139/139. `FINDINGS.md` C151.*
      **And the volume is made**: INVOL options 7 then 1 on a copy, run to
      `Initialization complete.` and ended with `!exit` -- which is enough
      *here* because INVOL is a standalone utility and writes through; an
      install under Domain/OS is not, and needs the guest itself shut down
      (C192) -- gives
      `APOLLODN3500B`, creator UID `77536D6F10022222`, node ID **`22222`** --
      read back by this project's own `node_id_from_volume`. `--ring-two-node`
      now runs two machines with two *different* node IDs, each taking its ID
      from its own volume, both reaching `pc 0000269E` at 2,000,000
      instructions.
      **Node B runs Domain/OS SR10.4**: installed, booted, salvaged, calendar
      set, cleanly dismounted at `1996-08-18 04:54:26`, paging file added
      (`FINDINGS.md` C153). Both volumes exist and record different nodes.
      **And the frontend is finished**: `--ring-console` tags each node's
      output by line so two machines running one firmware stay attributable,
      `--ring-script-a/-b` drive a dialogue at each with its own autobaud
      knock, and `--clock` reaches the ring path -- which it did not, and the
      runner's hardcoded zero epoch is 1900, so **neither node could ever have
      reached a shell** whatever else was in place (C158).
      *Verification: this core boots one node to a live Domain/OS -- `Apollo
      Phase II Environment`, Init, global libraries, `Node ID = 12345`, daemons
      -- at `--clock 2002-11-28`, read from the volume's own dismount stamp.
      1.5 G instructions, 5,419,460,924 clocks, hash `98874E148005986A`.*
      **`lcnode` RUNS, and the way in was never a login server.** Booting the
      OS from MD -- `di w`, `ex domain_os` -- leaves a `)` prompt, which is not
      a shell (`lcnode` there gives `? Unknown command`) but `sh` at it is:
      `login: user`, and then, by absolute path because `/com` is not on the
      path,

          $ /com/lcnode
           The node ID of this node is 22222.
           No other nodes responded.
           Node ID      Boot time           Current time      Entry Directory
           22222   1996/08/18  6:16:41   1996/08/18  7:05:58  //node_22222

      *That is this item's verification command, working, on the node this
      session built.* `No other nodes responded` is right for one machine with
      no cable.
      **The earlier "there is no login" conclusion was measured on one path
      only** -- the PROM's autoboot, which goes to SPM and never prompts. True
      of that path, false of the MD path, and the difference went untested for
      a session. `siologin` was configured on the volume anyway
      (`FINDINGS.md` C163) and is a real capability; it is simply not what this
      check needed. Detail in `FINDINGS.md` C164.
      **So what remains is one run**: `--ring-script-a/-b` driving each node's
      MD through `di w`, `ex domain_os`, `sh`, `user`, `/com/lcnode`, with both
      volumes and `--clock`. Everything it needs is built.
      **That sentence is stale and C165 already refuted it** -- left standing
      here for a session while the finding sat in `FINDINGS.md`. **This core
      cannot put MD on the serial line**: a *key press* releases the boot PROM's
      console-selection poll and serial characters do not, so MD here talks to
      the frame buffer. The oracle's MD answers a serial port only because MAME
      fits an `apollo_stdio` device, which is a MAME convenience and not this
      machine. The MD route is the *oracle's*, and `ring-a.script` was written
      for it.
      **The route on this core is C165's**: boot **Normal**, where a node prints
      its whole Domain/OS startup on serial 1 channel B and then goes quiet at
      `SPM system init complete.`, with **`siologin`** configured -- which is
      precisely the thing that turns that silence into a `login:` prompt,
      because it waits for a carriage return from a terminal on the SIO line.
      Then knock with carriage returns, log in as `user`, and run
      **`/com/lcnode`** by absolute path, since `/com` is not on the path.
      **So the remaining work is named**, and 2026-08-19 measured it exactly.
      Searching the images for `siologin1_local` -- the string C163 wrote --
      finds it in **`media/dn3500-nodeB-siologin.awd` and nowhere else**:
      neither `dn3500-sr10.4-installed.awd` (node A) nor `nodeB4.awd`, the two
      volumes the two-node runs actually use, carries the configuration. So the
      chain is three steps and none of them is a ring question:
      1. ~~Salvage `dn3500-nodeB-siologin.awd`.~~ **DONE** (C210): 32 seconds
         in the oracle, against 40% of the way through 1.5 G instructions on
         this core. `di w` / `ex salvol` / `w` / `1 -f -t -s`; **not**
         `ex domain_os`, which offers to bring the OS up *without* salvaging and
         says so. Saved as `media/dn3500-nodeB-siologin-salvaged.awd` with
         `siologin1_local` intact.
      2. ~~Give node A's volume the same treatment.~~ **DONE** (C211), and no
         re-install was needed: C162 called the MINST window "the only window
         for configuring a node from a command line", and **C164 supersedes
         that for an installed volume**. `di w` / `ex calendar` / `ex domain_os`
         leaves a `)` prompt, `sh` at it gives `login:` -- **that prompt is the
         shell's, not `siologin`'s**, which is exactly why a volume lacking
         `siologin` can still be reached here to be given it. Both files written
         and read back:

             siomonit_file:  -repeat /dev/sio1 -n siologin1_local
             startup.spm:    cps /com/tctl -line 1 -insync
                             cps /sys/siologin/siomonit -n siomonit /sys/node_data/siomonit_file

         Saved as `media/dn3500-nodeA-siologin.awd`, `shut` giving
         `Shutdown successful` and a dismount stamp of 2002-11-27T23:30:15.
         C162's claim was true of a **bare** disk, which is the case it was
         measured on.
      3. **A third step the first two exposed, now done too**: the volumes had
         to be brought into **one era**. A two-node run shares a single
         `--clock`, and after step 1 node B's volume had a **zero** dismount
         stamp and a 1996 mount stamp against node A's 2002 -- no clock
         satisfies both, and Domain/OS halts with *"More than 14 days have
         elapsed since the last shutdown"* on whichever it misses. Both volumes
         were booted in the oracle with `ex calendar` set to 2002/11/27 and shut
         cleanly, which is what writes the stamp:

             dn3500-nodeA-siologin.awd  mount 22:06:42  dismount 23:30:15
             dn3500-nodeB-ready.awd     mount 22:34:40  dismount 22:42:09

         both carrying `siologin1_local`. **Read the stamps from the label
         rather than guessing a clock** -- block `0x440`, `+0xBC` mount and
         `+0xC0` dismount, in 262144 µs ticks from 1980.
      4. **Model the SIO lines' modem control** (C220) -- the step the first
         three uncovered. `siologin2_local` is a live process and a carriage
         return delivered to its line reads
         `sio2 A  1 discarded unread`: taken by the port, never read by the OS.
         The line is configured `tctl -line 2 ... -dcd_enable ...` by the
         template the volume ships, and `008778-03` Figure 3-5 carries
         `SI01_DCD`/`SI02_DCD`/`SI03_DCD` with §3.9 listing Data Carrier Detect
         and Data Terminal Ready among the six signals each line supports.
         **This core models one of the six**: `ap_mc68681.h` has CTS and RTS,
         nothing has DCD or DTR, serial 1's `IP0` is §3.9's refresh loopback,
         and serial 2's input port is never driven. Sources for it are on disk
         -- §3.9 and Figure 3-5 for the board, `[MC68681]` §4.2.11 for the
         part's ports -- and the oracle's `apollo_m.cpp` cross-checks the pin
         assignment, which Figure 3-5 does not give.
      5. Run the two nodes with a script that waits for
         `SPM system init complete.`, knocks, logs in as `user`, and runs
         **`/com/lcnode`** by absolute path -- now with `--boot-script-line`
         pointing the dialogue at the line `siologin` serves.
      *What is already proven and needs no repeating*: both volumes boot clean
      (no salvage line in a two-node run), and **both nodes reach
      `Domain/OS kernel(7)` on one segment** with distinct node IDs.
      **And the run itself is affordable and measured**: about 1.5 G
      instructions per node against 125 K instructions/s per node (C156), so
      roughly three hours for two nodes. A run to launch deliberately rather
      than a gap.
      **No copied volume is used and none is needed** (C199, C200). Node B's
      own volume boots -- what stopped it was this frontend not giving a machine
      its disk's node on the `--disk` path -- so the run uses two genuinely
      installed volumes, `012345` and `022222`. The relabelling C180 proposed is
      withdrawn: besides being an approximation, it could not have worked here,
      because the runner takes each node's ID from its disk and the copy's
      objects carry the *other* node's, which is the mismatch Domain/OS shuts a
      node down for.
      **Diskless boot is the other route and is now sketched from the web**: a
      diskless node's PROM broadcasts a partnership request, and a partner
      running `netman` with the client listed in `/sys/net/diskless_list`
      answers with `netboot` and then the OS image. That is a protocol project,
      but it is no longer an unnamed one.
- [x] Node insertion and removal mid-run, stripping, token loss, **and the
      frame path**, in `src/core/ring/ap_ring_station.*`.
      **Unticked by the `[MAC]` audit and re-earned** (`RING.md` 85-90d): the
      claim "the transmit sequence of §2.1 ... implemented" had been false --
      three of its eight steps were absent and no frame ever reached the medium.
      All of it is now built: a station transmits a queued frame while stripping
      (step 3), releases the ring with a new free token (step 6), stops
      stripping when its own frame returns (step 7), takes §2.2.2.2's
      destination-or-broadcast decision, and modifies **both** acknowledge
      fields in flight with parity maintained.
      *Verification: `ring_station_suite` 12 -> 18, each test citing its `[MAC]`
      section, and the ring probe golden plus the reference boot hash
      byte-unchanged throughout. Detail in `RING.md`.*
- [x] 3c505 802.3 controller, so Domain networking can also be checked against
      MAME the way MAME does it. *Verification: **the card's own firmware
      self-test passes** — `802.3 Network Controller-AT test passed.` — and the
      host-interface oracle diff agrees at the instruction level, our 22 writes
      to `058006` coming from the PCs `ETHERNET.md` finding 10a measured on the
      oracle before this model existed; `etherlink_suite` 50 tests.*
      **One named approximation, `PROVISIONAL` in `ap_3c505.h`**: the adapter's
      power-on flag handshake is host-side, where MAME runs the card's real
      80186. Cost to close is an 80186 alone (18c).
      **The line-by-line audit found six further defects**, the structural one
      being that §3.1.2's host→adapter half was never wired and the test that
      appeared to cover it supplied the missing wiring from its own body
      (`ETHERNET.md` 19-24). Detail in `PROJECT_STATUS.md`.
  - [x] `src/core/device/ap_3c505.h`, the interface, transcribed from `[DEV]`
        §1.3.3 and §1.9: the five registers in sixteen I/O locations, the
        20-byte half duplex FIFO, the PCB's 64-byte limit, and the eleven named
        flags — at the time **without positions**, because §1.9 defers those to
        a document this project did not then hold. Superseded in both respects
        by the `[HIS]` item below: the map's `+2`/`+6` reading was wrong and
        the flags now have positions.
        *Verification: `etherlink_suite`, 5 tests at the time.*
  - [x] The PCB command set, `[DEV]` §3.1 and Table 1 — all seventeen commands,
        the `00`-`2f` / `30`-`5f` split, and the invariant that **a response
        code is its command plus `0x30`**. The two codes Table 1 marks `n/a`
        are the PIO transfers, which the host drives and the adapter therefore
        never answers, so the hole in the response space states who moves the
        data. Read from a page render: the table is two-column and the PDF's
        text layer interleaves it. Closes open question B.
        *Verification: `etherlink_suite`, now 8 tests — the `+0x30` rule
        checked across every implemented command, the two PIO commands proved
        to have no response while their DMA counterparts do, and the reserved
        codes proved not to be commands.*
  - [x] **The mailbox itself, `ap_3c505.c`.** `[DEV]` §1.9 as a device: the
        command byte each way, the 20-byte half duplex FIFO, and the four flag
        registers -- with `HSR` and `ASR` **derived** on each read rather than
        stored, so `HCRE`/`HCRF` and `ACRE`/`ACRF` cannot disagree about the one
        byte they both describe. A change of `DIR` empties the FIFO, `FLSH` acts
        from either side, and `ATTN|FLSH` is a reset rather than a large flush.
        The adapter half is a peer (`take`/`post`), so the open question of
        emulated 80186 versus host-side PCB protocol stays open.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `etherlink_suite`, 26 tests, the direction-change one
        checked to fail against a model that keeps the FIFO across the turn.*
  - [x] **Command dispatch for the single-PCB commands, and the wire.**
        `[DEV]` §3.2's `01`/`02`/`03`/`0A`/`0B`/`10` execute against adapter
        state that is only what a documented command sets or a documented
        response reports. The four transfers answer nothing, per Table 1, and
        commands whose response format is not yet read are refused via §3.1.1's
        state `10` rather than answered with invented contents.
        `ap_3c505_wire_t` is a context and a `transmit` callback, so `src/core`
        owns no socket and a deterministic capture backend and a live one look
        identical to the device. Detail in `PROJECT_STATUS.md`.
        *Verification: `etherlink_suite`, 37 tests, including that every
        response dispatch produces agrees with `ap_3c505_response_for`.*
  - [x] **The data phase, `08H` and `09H`.** Neither is a single-PCB command:
        the packet crosses the data register *after* the PCB is accepted, and
        the response comes when it has. A frame reaches the wire only on its
        last byte; a frame arriving with nothing armed is counted as no
        resources; one longer than the host's buffer is truncated with both
        lengths reported. Detail in `PROJECT_STATUS.md`.
        *Verification: `etherlink_suite`, 37 tests, against a recording wire.*
  - [x] **Wired into the board, opt-in, with a TAP backend behind it.**
        `ap_board_attach_ethernet` places the card at `058000` and is **off by
        default**: the boot PROM tests a card it finds, so an empty slot is the
        machine that boots to `login:` and the identity hash covers what it
        always did. `--3c505` fits it; `--3c505-tap IFACE` puts its wire on a
        Linux TAP device through `frontend/common/ap_tap.c`, so `src/core` still
        owns no socket. A live wire is non-deterministic and the frontend
        **refuses to print a state hash** for such a run rather than trusting
        the operator to remember. Detail in `PROJECT_STATUS.md`.
        *Verification: `board_suite` 44 -- absent until fitted, exactly sixteen
        locations, and the oracle-measured probe bytes through a bus read.*
  - [x] **The interrupt line and the DMA channel, from `008778-03` chapter 14** —
        a chapter `ETHERNET.md` had never cited. Figure 14-3's standard AT-slot
        strapping is "DMA Channel 6 and Interrupt Level 10 Select", read from
        the page image because this chapter's text layer gives `IRQ?` and `SA?`;
        both of the card's accessors existed and were joined to nothing.
        `[DEV]` §1.9.4's request rule is `HRDY` in **both** directions — the
        `ARDY` rows are the adapter's — with its three deactivating conditions
        including the 9-transfer demand pause, and `[HIS]` p. 3-4 supplied a
        `DONE`-clearing rule the model was missing entirely. `ETHERNET.md`
        findings 12-13c. Detail in `PROJECT_STATUS.md`.
        *Verification: `etherlink_suite` 38 -> 43, `board_suite` 45 -> 47, and
        `dma_suite` 17 -> 18 for a byte moved end to end over DRQ6 — controller
        2's channel 2, through the 16-bit half of the translation map — with its
        terminal count reaching `HSR`'s `DONE`. The `login:` boot with the card
        fitted returns the reference hash `A354786119A3931D` unchanged, which is
        the two new lines sampled all boot and changing nothing.*
  - [x] **Open question C answered, and the card's own firmware self-test
        passes**: `802.3 Network Controller-AT test passed.` The option ROM is
        the ring ROM's twin, so `tools/ring-rom/disasm.py` read its `entry_05`
        unchanged, and that specified the adapter's power-on handshake from the
        firmware that talks to the real card. Two defects found, the larger
        being that the adapter half ran **only with a live TAP wire** attached.
        `--3c505-rom FILE` added, as `--ring-rom` is for the ring. Two earlier
        claims of mine are retracted in place: `ETHERNET.md` 14b and 15a.
        Detail in `PROJECT_STATUS.md`.
        *Verification: the firmware's own verdict, on three 350 M boots —
        `--3c505` returning the reference hash unchanged, `--3c505-rom` failing,
        and `--3c505-rom` passing after the fix — each with its configuration
        confirmed from the run's own header.*
  - [x] `docs/references/ETHERNET.md`, the findings file, written from the
        manual before any code — the map, the 20-byte half duplex data FIFO
        with its `DIR` bit, the five general-purpose status flags the hardware
        "does not decode in any way", and the two adapter interrupts. Base
        `300H` puts the card at physical `058000` through this machine's AT
        decode, agreeing with `ap_board.h` from a *manual* rather than from the
        oracle. Detail in `PROJECT_STATUS.md`.
  - [x] **`[HIS]` read, and it corrects the map as well as filling the gap.**
        All four flag registers (`HCR`, `HSR`, `ACR`, `ASR`) transcribed from
        the page images, closing open question A. It also settles a host I/O
        map that `[DEV]` §1.3.3 gave wrongly and `[DEV]`'s *own* §2.1 and §2.5
        contradict: `+2` on a write is the **Aux DMA Register**, and the Host
        Control Register is at `+6` — three tables to one. The header and one
        test had followed §1.3.3. Detail in `PROJECT_STATUS.md`.
        *Verification: `etherlink_suite`, now 12 tests — each register eight
        distinct bits covering the byte, the general-purpose flags crossing at
        the same bit, the handshake flags belonging to the side that reads
        them, `ATTN`+`FLSH` as the hard reset, and the corrected `+2`/`+6`.
        Independently, the layout decodes finding 10a's option-ROM handshake
        exactly — `C0` and `50` at `+2` are an empty FIFO in the two
        directions — which a day-old oracle measurement had left ambiguous.*

## Phase 7 — Completing the model range

- [x] Close the DN4500 clock `PROVISIONAL` from a cited configuration guide.
      Detail in `PROJECT_STATUS.md`.
      *Verification: two independent citations in the same document, both
      recorded in `docs/PROJECT_STATUS.md`.*
- [x] Add DSP4500, which `[CFG]` documents and the table was missing.
      *Verification: `model_suite` checks it matches its DN4500 sibling exactly.*
- [x] Close most of the DN2500 `PROVISIONAL` set: 68030 @ 20 MHz, 68882 @ 20 MHz,
      on-board mono graphics, 4–16 MB RAM, all from `[CFG]`'s Series 2500 Product
      Summary. Only `ram_base` remains open.
- [x] Closed DN2500 `ram_base` **and** its extent, from the Series 2500 boot
      PROM's own memory sizing code — the second of the two routes this item
      named, and in the end the only one available: no Series 2500 allocation
      table exists on disk or on the web, and the oracle has no 2500 driver.
      *Verification: `model_suite`, 19 tests, one pinning `04000000` and 16 MB
      against the PROM's own `OR.L #$04000000,D1` and `ANDI.L #$04FFFFFF,D1`;
      `tests/goldens/model_table.txt` regenerated. Detail in
      `PROJECT_STATUS.md`.*
  - Tail found here and belonging to the model range, not to this item: the
    Series 2500 PROM is **131072 bytes** against `AP_BOARD_PROM_SIZE` of
    `0x010000`, so `ap_board_load_prom` would refuse it. Not a board defect —
    `ap_board` is the DN3500 and 64 KB is `008778-03` Table 2-8's figure. The
    PROM extent is model variance and belongs in the model table beside
    `ram_base`.
- [x] DN4500 Matrox graphics. *Verification: the board's own ROM draws
      `APOLLO DOMAIN DN4500` into its frame and the PNG is legible — paper- and
      firmware-verified, there being no oracle. `matrox_suite` 6 → 8, the two
      new tests pinning the frame's geometry as relationships rather than
      restated constants, so a drifting stride fails there instead of shearing
      a picture later.*
      **The frame is `$900000`, 256 KB, 1280×1024 at 1bpp on a 256-byte
      stride**, on four independent routes: `ENTRY_03`'s clear loop, the glyph
      routine's margin and row arithmetic, `019411-A00` Table 2-5 placing it in
      AT bus memory space, and the machine executing both — 163,840 bytes, which
      is 1024 × 160 exactly. The pitch was then discriminated by the test set
      before the answer was known: only 256 renders unsheared.
      **Two tails stay open and are named rather than closed by assertion**:
      whether these addresses decode identically on a real DN4500 (the runs use
      the DN3500 map), and the meanings of `$DA0006` bits 3-5. Detail and the
      seven earlier retractions in `PROJECT_STATUS.md`.
- [x] **DSP variants confirmed as true subsets.** All four boot headless and
      strap correctly -- they were all four *unstrapped* until the memory byte
      was keyed on the board rather than the model. `model_suite` holds the
      relation up: a headless row must agree with its workstation in every board
      respect and differ only in the display.
      **The oracle diff runs clean for both models that have a reference**:
      `dsp3500` and `dsp3000`, **29 of 29 mapped CPU fields with nothing
      differing**, synced on each PROM's own program counter. It found and
      closed one defect of ours (a root pointer's descriptor type) and one hole
      in the instrument (the field map named an `MC68030`, and a Series 3000 is
      an `MC68020PMMU`). Detail in `PROJECT_STATUS.md`.
      *Verification: the two diffs above; `model_suite` for `dsp4500`, which
      MAME does not register at all and so has no runnable reference. `dsp5500`
      is `MACHINE_NOT_WORKING` on the oracle and stops at `cinva` here -- the
      68040 blocker, which is Phase 2b's item and not this one.*
- [x] **The DS5500 gets its own address map**, from `019411-A00` Table 2-5. It
      had been running on the Series 4000's since the model was added; the
      addendum's replacement for handbook page 2-7 is the DS5500's own
      allocation table and differs in three places. Detail in
      `PROJECT_STATUS.md`.
      *Verification: `board_suite` 57 → 60 and `boardreg_suite` 21 → 26,
      including all **35** configurations `019411-A00` §4.2.1.18 publishes for
      the memory present register. Identity harness re-run: `03EE415450926A89`,
      unchanged, which is what says the DN3500 is untouched.*
- [x] **Two writable registers were outside the identity hash.** Master request
      and task alias were added to the model as Table 2-8's byte-wide storage
      and never added to `ap_board_hash_registers`, so state software can change
      was invisible to the one instrument every optimisation in Phase 8 is
      checked with. The reference boot writes one of them — its report has
      always read `master request 0/1`. Detail in `PROJECT_STATUS.md`.
      *Verification: the reference re-baselines `03EE415450926A89` →
      `D7BA23DD7961F1A1`, and that it moved at all is the measurement: a
      register left at zero would have changed nothing. No golden embeds a state
      hash, so nothing else moved; `ctest` 139/139 either side.*
- [x] **The ring station's buffers were lent by nobody, so a booting node could
      neither transmit nor receive.** `attach_tx` was called by tests only and
      `attach_rx` by nothing at all, so `ap_ring_station_queue_frame` returned
      false on its first line in every real run and every received byte was
      dropped — with a green ring suite over it, because each test attached its
      own. Found by sweeping `src/core` for exported functions nothing calls.
      Detail in `PROJECT_STATUS.md`.
      *Verification: `board_suite` 60 → 62, and the three existing board tests
      no longer attach their own buffer — they pass on the board's wiring or not
      at all. Identity harness re-run: `03EE415450926A89`, unchanged.*
- [x] **The memory bank layouts were comments, and two callers divided by four
      instead.** Every row of the RAM strap table carries a layout read out of
      the boot PROMs' own decode chain — `4-4-4-0`, `8-4-4-4`, `2-2-2-2` — and
      those layouts had never been data. `ap_calendar_set_memory_boards` and the
      new memory present register both computed `total / 4`, which agrees with
      the firmware on 16 and 32 MB and disagrees on every other row it can
      reach, and disagrees with the DN5500 even at 16 MB (`8-8-0-0`). Now one
      source, `ap_sio_ram_bank_layout`. Detail in `PROJECT_STATUS.md`.
      *Verification: `sio_suite` 31 → 34 asserting all ten rows including the
      three where an even split differs, `calendar_suite` 13 → 14 on `8-4-4-4`
      and `4-4-4-0`'s empty slot. Identity harness re-run: `03EE415450926A89`,
      unchanged — 16 MB is `4-4-4-4` either way, which is why this was invisible
      for as long as it was.*
- [ ] **The DS5500's address translation map is 4 KB, and ours is 2 KB.**
      `PROVISIONAL`. Table 2-5 gives `017000`-`017FFF` where `[S3K]` §2.5 gives
      `017000`-`0177FF`, and `AP_ATMAP_LIMIT` follows the latter. Placing the
      wider region as things stand would alias its upper half onto its lower —
      the exact fault `ap_atmap.h` records the loaded diagnostic catching — so
      the DS5500 map keeps 2 KB and this is named rather than approximated.
      What would close it: the region size becoming a property of
      `ap_board_map_t` rather than the constant `AP_ATMAP_ENTRIES`, which is
      what `ap_board_hash_translation_map` walks. Growing that constant globally
      would change **every** model's state hash, so the widening has to be
      per-map or the reference hash and every golden go with it.
- [x] **The DS5500's `010200` is a cache *status* register**, and is now one:
      `019411-A00` §4.2.1.14, 8-bit read-only, `HSI Present <3>` and
      `MEM Time <0>`, where Table 2-8's row at that address is the writable
      cache *control* register this core measured on a DN3500. Both bits
      derived, not stored. Detail in `PROJECT_STATUS.md`.
      *Verification: `boardreg_suite` 26 → 29 — read-only against all 256
      writes, `HSI Present` cleared by a fitted display, and `MEM Time`
      following the latched bus error through the selective clear that clears
      it. Identity harness re-run: `03EE415450926A89`, unchanged.*
      **One reading is documentary rather than stated and is flagged**: bit 0's
      polarity is nowhere given, and it is taken as active high because the same
      figure marks `HSI Present` "cleared (0) to indicate" and marks bit 0
      nothing. The bits the figure calls "not used" read all-ones, which is
      `PROVISIONAL` — no DS5500 runs on this core or on the oracle, so a single
      read would settle both. Both siblings on disk carry neither section;
      `007861` remains the missing document.
- [ ] **The DN5500 needs a 68040 instruction core, measured rather than
      assumed.** Run on 2026-08-19 with `--boot-trace-last`: the machine
      executes `NOP` at `00060C` and then takes **vector 11, the F-line
      emulator trap**, on `F4D8` at `00060E` — `CINVA BC`, a 68040
      cache-invalidate. `4E7B 0004` follows it, `MOVEC` to `ITT0`, so the PROM
      is programming 68040 transparent translation two instructions in. Our
      68030 core's response is *correct*; there is simply no 68040 to execute
      it. The handler then runs 135 more instructions and dies at `005070`,
      `MOVEA.L $156(A6),A7`, loading a stack pointer from `010002D6` that
      nothing initialised — so the reported `FAULT on 6100` at `005074` is a
      `BSR` pushing to `FFFFFFFC`, three failures downstream of the real one.
      **Confirmed independent of the new DS5500 map**: the identical 137
      instructions and identical fault occur on `DS4000_MAP`, checked by a
      temporary one-line revert.
      `src/core/cpu/m68040/` is 3,181 lines of ATC, descriptors, caches, FPU
      and timing tables and contains **no step and no decoder**, so this is a
      Phase 2b project rather than a tail of this item. What would close it is
      a 68040 instruction core; nothing in `docs/references/` is missing for
      it — `motorola/` holds the manual.
- [ ] **Table 4-6's added line, which cannot be implemented from what is held.**
      `PROVISIONAL`. The addendum says "On page 4-19, add the following line to
      Table 4-6: `PC ON/OFF — Physical Cache (DS4500 Only)`", and page 4-19 is
      in the handbook this project does not have. A row's name and meaning
      without the table it belongs to says a DS4500 has a physical-cache
      control and does not say which register or bit carries it. What would
      close it is `007861` itself; bitsavers does not hold it.
## Phase 8 — Verified fast mode

Only after the reference core is proven, and only under an identity harness.

- [ ] **A per-cycle processor.** `ap_m68030_step` runs a whole instruction, so
      Phase 3's tick advances every *device* against absolute time while the CPU
      is stepped by instruction — exact in device state, quantised in when a
      change is noticed, bounded by the longest instruction. "One `tick()` per
      machine cycle" read literally wants the processor split the same way.
      It is here rather than in Phase 3 because it is a rewrite of the run loop
      under everything already built on it, and this phase is the one that
      begins "only under an identity harness". Rewriting first and checking
      afterwards is the mistake the whole phase exists to avoid.
      *Verification: probe goldens and boot state hashes byte-identical across
      the change, which is this phase's standard and not a weaker one.*
      **Scoped 2026-08-17, and the shape is not what the item assumes.** The
      **bus is already cycle-accurate**: `ap_m68030_bus_t` carries S0-S5,
      `advancing_to_s4`, and ECS/OCS/AS/DS/DBEN as `[030]` §7.3 names them,
      plus burst and RMC, and `ap_m68030_bus_tick()` advances **one clock**.
      What is instruction-granular is the *sequencer*, not the bus.
      **The obstacle is that the bus runs inline.** `ap_m68030_access.c:334` and
      `ap_m68030_cache.c:317` each construct a **local** bus (`&write_bus`,
      `&bus`) and tick it to completion inside one access, so its cycles are
      invisible to the machine's tick. Per-cycle stepping therefore means
      hoisting bus ownership into `ap_m68030_cpu_t` and letting the machine
      drive `bus_tick`, not writing a cycle model — that part exists.
      **Increment 1 is ALREADY BUILT, and this text was describing it as work
      to do.** Checked in the source, not inferred: `ap_m68030_access.h:241`
      has `ap_m68030_bus_t bus` as a field of the access context,
      `ap_m68030_access.c:310` takes `&access->bus`,
      `ap_m68030_cache_read` already takes the bus as a parameter
      (`ap_m68030_cache.h:319`), and the machine owns two long-lived contexts
      set up once in `ap_machine_init` (`ap_machine.h:334-335`,
      `ap_machine.c:600`). `ap_m68030_bus_begin` still clears the bus per
      access, which is not a gap -- each access is a new bus cycle on the part,
      and the increment was about where the storage lives.
      **So the next increment is the real one**: `ap_m68030_access.c:329` and
      `ap_m68030_cache.c:313` both loop `while (ap_m68030_bus_active(...))` and
      tick to completion inside one access, so the machine cannot drive
      `bus_tick` until an instruction can *stop* inside an access. That is a
      resumable sequencer, and it is the rewrite this item is actually about.
      **And it has a prerequisite it did not know about** (`FINDINGS.md` C152):
      four parts -- disk, tape, keyboard, graphics -- date deadlines from a
      stored `now` that only `ap_board_advance` refreshes, so any schedule that
      stops advancing the board once per instruction must carry those cursors
      or the disk completes its commands early. Detail in `FINDINGS.md` C154.
      **Step 1a is DONE**: the write path's bus now lives in
      `ap_m68030_access_ctx_t` and `ap_m68030_access_write` uses it instead of a
      local. Behaviour-neutral by construction — `ap_m68030_bus_begin` assigns
      every field except `rmc`, which it documents as deliberately preserved and
      which the caller sets immediately before, so a persisting bus is identical
      to the fresh local it replaces. No signature changed.
      *Verification: `ctest` 138/138, identity boot `A354786119A3931D`
      unchanged.* **Step 1b remains, and it is not as mechanical as 1a.**
      `ap_m68030_cache_read` already takes **ten parameters** and has **twelve
      call sites** (`ap_m68030_access.c`, `tests/cache_suite.c`), so an
      eleventh is poor design rather than merely tedious — the honest options
      are (a) add the parameter anyway and bundle later, or (b) bundle the
      argument list into a request struct *first*, which touches the same twelve
      sites once instead of twice.
      **And a correctness question with it**: whether the read path shares
      `ap_m68030_access_ctx_t`'s bus or gets its own field. They are never live
      simultaneously — a read-modify-write is a read *then* a write — so one bus
      is defensible and would match the hardware, which has one. **Settled from §7.3.6, page 7-54**: "Like the
      asynchronous operation, the synchronous read-modify-write operation is
      **indivisible**", and "the burst mode is never used during
      read-modify-write cycles". Indivisible means nothing interleaves between
      the read and the write, so the two are strictly sequential and never
      concurrent — **one bus field is correct**, shared by both paths, which is
      also what the hardware has. RMC then spans the pair naturally, since
      `ap_m68030_bus_begin` preserves it across a `begin`, and the explicit
      `rmc` copy in the write path becomes redundant rather than load-bearing.
      So 1b is: give `ap_m68030_cache_read` access to the same
      `ap_m68030_access_ctx_t` bus, and delete its local.
      **And the twelve call sites are one plus eleven**: exactly one is
      production — `ap_m68030_access.c:182` — and the other eleven are
      `tests/cache_suite.c`. So the behavioural change is a single line, and the
      churn is entirely in a suite that already constructs its own caches.
      **STEP 1 IS DONE.** `ap_m68030_cache_read` takes the bus as its second
      parameter, its local is gone, and the one production caller passes
      `&access->bus` — so both paths now share the single bus §7.3.6 requires,
      and **no `ap_m68030_bus_t` local remains in the core**. The eleven test
      sites pass a named `g_test_bus`.
      *Verification: `ctest` 138/138, identity boot `A354786119A3931D`
      unchanged — the criterion this increment exists to meet.*
      **Increment (2) is not achievable as written, and the scouting says so
      before anyone starts.** `ap_m68030_step` is the tail of a **6,966-line**
      `ap_m68030_step.c`, entered at `5946`, and it sequences an instruction in
      ordinary nested C — calls, loops, switches. "Give it a resumable state"
      means one of: rewriting it as an explicit state machine (the whole file);
      coroutines (not in C); a fiber or thread per CPU (heavyweight, and it
      trades a determinism guarantee this project is built on); or re-executing
      an instruction on resume (invalid — instructions have side effects before
      they commit).
      **The alternative that meets the item's actual purpose**: keep `step`
      atomic, but have it *report its cycle timeline* — the bus is already
      cycle-accurate and now owned by the access context, so the cycles exist —
      and let the machine distribute those cycles to devices at the instants
      they occurred. That gives per-cycle device visibility without a resumable
      CPU. **Its one limitation, stated rather than discovered**: it still
      cannot express a device output feeding back into an instruction *still
      executing*, which is the case the tick-loop item names as the reason to
      want a cycle-steppable CPU at all. So this is a real narrowing of the
      item, not a free win, and taking it is a decision to record explicitly.
      **And the alternative is smaller than it sounds, because the per-clock
      delivery already exists.** `ap_machine.c:839` calls
      `ap_board_bus_ticks(board, last_instruction_clocks)`, and that loops the
      arbiter **one clock at a time**, batching only where the board can prove
      the ticks identical — no DMA able to ask and an idle arbiter. So the
      machine is not quantised in *resolution*; it is quantised in *ordering*,
      because those clocks are charged **after** the instruction, as the comment
      there says: "clocks that already happened".
      That reframes increment (2)/(3): not "invent a cycle timeline" but "move
      the existing per-clock delivery from after the instruction to during it".
      The bus already knows when each cycle happened, and after step 1 it
      survives the access. What is missing is a record of *which* clock each
      cycle fell on, and a `bus_ticks` call sited to consume it.
      **Increment 2a is DONE: the one-instruction lag is gone.**
      `ap_board_bus_ticks` was charged with `last_instruction_clocks` *before*
      the step, so every bus cycle reached the arbiter **one instruction late**
      — the total right, the ordering wrong. It now runs after the step, for the
      instruction that just ran.
      *Verification: `ctest` 138/138, identity boot `A354786119A3931D`
      unchanged.* That the hash does not move is itself the measurement this
      item wanted: the two schedules agree on everything this boot exercises,
      which is what the tick-loop item asserted and had not shown.
      **What still blocks true intra-instruction delivery**: `bus_ticks`
      receives the instruction's **whole** clock count, internal cycles
      included, not just bus cycles. Delivering per access would change the
      total the arbiter sees unless the internal clocks are delivered too, and
      the CPU does not currently report when those elapse. So (3) needs the
      sequencer to emit a clock timeline, not merely the accesses to call back.
      **Scoped, and tractable.** 117 sites accumulate clocks across the CPU, but
      only **37 are `cpu->clocks`** directly; the rest are local accumulators
      (`out.clocks`, `result.clocks`, `bus->clocks`) folding into those. So the
      timeline is one helper `ap_m68030_charge(cpu, n, kind)` that accumulates
      *and* appends, one bounded per-instruction buffer on `ap_m68030_cpu_t`,
      and 37 mechanical replacements. The bound is the longest instruction — a
      `DIVS` at ~150 clocks — so a fixed array cannot overflow on a real
      program, and a dropped-entry counter proves that rather than assuming it.
      **3a is DONE.** `ap_m68030_charge(cpu, n)` is the single accumulation
      point — every site that wrote `cpu->clocks` directly now goes through it,
      so the timeline cannot drift from the total — and it appends to a
      256-entry per-instruction buffer on `ap_m68030_cpu_t`, reset at the top of
      `ap_m68030_step`. `clock_events_dropped` is run-long and deliberately not
      reset per instruction, since resetting it would hide the one case it
      exists to catch.
      *Verification: `ctest` 138/138, identity boot `A354786119A3931D`
      unchanged*, which is what "emits and consumes nothing" predicts.
      **3a emitted and consumed nothing, and was provably neutral**: a record
      nothing reads cannot change behaviour, so the identity hash must not move
      — and if it does, the helper changed something it should not have. The
      buffer is deliberately unhashed until 3b.
      **3b is DONE**: `ap_machine_run` walks the timeline, advancing the bus in
      the order the processor spent the clocks rather than in one lump at the
      end. The total is identical by construction — `ap_m68030_charge` is the
      only accumulator, so the entries sum to `clocks` — with a fallback to the
      batched call if `clock_events_dropped` is ever non-zero.
      **And the hash still does not move**: `A354786119A3931D`, clocks
      identical at 1,497,270,792, `ctest` 138/138. This was the increment where
      a moved golden would have been a *finding*, so an unmoved one is the
      finding instead — over a 350 M-instruction boot, per-charge ordering and
      end-of-instruction batching are **indistinguishable**. That is the
      tick-loop item's claim ("the two schedules agree on everything measured so
      far") demonstrated rather than assumed.
      **What is still not reached**: a device output feeding back into an
      instruction *still executing*.
      **But it does NOT need a resumable sequencer, and that is the finding.**
      Resumability is only wanted where an instruction *observes* the machine —
      a bus access — because between accesses the CPU is internal and can see
      nothing. And at an access the CPU need not suspend: what is required is
      that the **devices be advanced to that instant** before the access
      completes.
      **The hook already exists.** `ap_machine.c:594` sets
      `.wait_states = machine_wait_states` on the access context, and that
      callback is invoked *during* every access with the physical address. It
      takes a `const ap_machine_t *` today, so it cannot advance anything — but
      the mid-instruction instant is computable there, since `cpu.clocks` is
      live and 3a's timeline records how they were spent.
      So the remaining half is: drop the `const`, advance devices to
      `now + duration(clocks spent so far)` at that callback, and the processor
      observes an up-to-date machine mid-instruction. **No state machine, no
      coroutines, no threads, and no rewrite of the 6,966-line file.** **Both risks were checked and refuted from
      the source**: `ap_board.h` names neither `cpu` nor `m68030` and
      `ap_board_advance` takes only a board and a time, so devices cannot
      re-enter the CPU; and `ap_board_access_time` is `const`, deriving from
      `ap_atbus_timing` and the *address*, so advancing devices cannot change
      the answer that triggered the advance.
      **IMPLEMENTED, and the identity hash MOVED — which is the finding.**
      `A354786119A3931D` -> `27AAE57F4EF4E97E`, **clocks identical at
      1,497,270,792**, `ctest` 138/138. Identical clocks say execution timing is
      unperturbed; the difference is device *state*, because devices now advance
      to the instant each access happens rather than to the instruction
      boundary. That is exactly what this half of the item is for.
      **So the "byte-identical" criterion cannot apply to this increment, and
      that needs a decision rather than a silent re-bless.** It was written for
      the ordering half, where it held five times running. The options are to
      re-bless the goldens deliberately with this change named as the reason, or
      to keep the old schedule behind a flag and run the two as an A/B.
      **Resolved as an A/B rather than a re-bless.** The new schedule is behind
      `machine.devices_advance_mid_access`, **default off**, exposed as
      `--mid-access-devices`. So every golden stands unchanged — the default
      boot is `A354786119A3931D` again — and the new schedule is opt-in.
      A flag rather than a replacement because the two are *observably
      different* and this project does not know which matches the hardware:
      same boot, same clocks, different device state. Replacing the schedule
      would be choosing an answer where the oracle can measure one, and this
      core expects to out-accurate MAME on exactly that kind of question.
      **The A/B is now runnable in one command each**, which is what makes the
      oracle comparison a next step rather than a project.
      **And the A/B has a cost dimension, not only a correctness one.** The
      mid-access schedule calls `ap_board_advance` on *every access* rather than
      once per instruction, and the B run of the identity boot was still going
      well after the A run had finished. **RETRACTED: the >119x below was my own bug, not a cost.** The
      `--mid-access-devices` flag was added inside
      `for (int i = 1; i < argc;)` — a loop with **no increment in its header**,
      where every branch must advance `i` itself — and the new branch did
      `continue` without one. B never ran slowly; it **hung in argument
      parsing**. A `perf` profile said `__strcmp_avx2` **81.18%** with `main` as
      the caller, which is not a shape any emulation cost can have, and that is
      what found it.
      **The real figures, fixed and re-measured**: 50 M instructions in
      **4.9 s** default against **5.3 s** mid-access — **1.08x** — and the state
      hashes are *identical* at `80899FCE206623A1`. **And they agree at 350 M too**: the mid-access
      boot is `A354786119A3931D`, identical to the default, in 37 s against the
      default's ~35 s.
      **Which explains the earlier `27AAE57F4EF4E97E` and retires it.** That
      reading came from advancing *every* device at *every* access, RAM
      included. Advancing only the device an access actually reaches — what
      `ap_board_advance_one` does, and all the correctness argument ever
      licensed — is **indistinguishable from the boundary schedule** over 350 M
      instructions. The earlier divergence was devices being dragged forward by
      accesses that never touched them, which is not more accurate, only
      different.
      **So the feedback half is done, and its result is a negative one**: with
      the bus cycle-accurate, the timeline walked, and devices advanced at the
      instant an access reaches them, this machine's 350 M boot is byte-for-byte
      what instruction-boundary batching produced. The per-cycle item's premise
      — that the two schedules "do not agree in general" — remains true in
      principle and is **unobserved here**, which is worth more than another
      assertion of it.
      **Which also means `ap_board_advance_one` was built to fix a cost that did
      not exist.** It is correct and default-neutral, so it stays, but the plan
      no longer claims it was needed. Two lessons, both this project's own:
      profile before optimising, and a figure that extreme is a bug in the
      instrument before it is a fact about the program.
      **The superseded claim follows.** The
      default schedule runs 50 M instructions in **5.0 s**; the mid-access
      schedule **did not finish the same run in 595 s**, so the ratio is a lower
      bound rather than a figure. `ap_board_advance` walks every device, and
      calling it per *access* instead of per instruction is the whole cost.
      **So B as implemented cannot be a default, and probably cannot be a
      routine measurement either** — a 350 M identity boot would be days. If the
      oracle shows B is the truer schedule, the work is not "switch the flag"
      but "advance only the device being accessed, or advance lazily", which is
      a design item of its own — **and its correctness argument is already
      written, in `ap_board_advance`'s own comment**: *"Order does not matter
      and must not: two devices advanced to the same absolute time cannot
      influence each other through the advance itself, which is what makes this
      a tick rather than a schedule."*
      If devices cannot influence each other *through the advance*, then
      advancing **only the device being accessed** is equivalent for that
      access. The rest catch up at the instruction boundary exactly as now, and
      interrupts are unaffected because the 68030 samples them at instruction
      boundaries anyway — which this same item established rather than assumed.
      So the design is `ap_board_advance_one(board, address, now)`, dispatching
      by region to the per-device advances that already exist
      (`ap_timer_advance`, `ap_calendar_advance`, …), called from
      `machine_wait_states` in place of the whole-board walk. The >119x is paid
      per access across *every* device; this pays it on one.
      **IMPLEMENTED, and it did not help.** `ap_board_advance_one` dispatches by
      region — RAM and PROM advance nothing, `SIO`/`CALENDAR`/`DISK`/`TAPE` reach
      one device each, and the other thirteen regions are *listed* rather than
      defaulted so `-Wswitch-enum` makes a new region a compile error instead of
      a silent fallback. Default behaviour is unchanged (50 M in **5.7 s**, hash
      `80899FCE206623A1`, `ctest` 138/138), since it is only used on the
      mid-access path.
      **But B still does not finish 50 M in 590 s.** The lazy advance was
      supposed to be the fix and is not, so the cost is **not** the per-device
      walk — which was the assumption behind the whole optimisation.
      **Diagnose before optimising again**, and this time measure rather than
      reason: whether `machine_wait_states` is even reaching `advance_one` on
      the hot path, whether `ap_board_region`'s lookup is itself the cost at one
      call per access, and what `perf` says the profile actually is. The
      previous guess cost an implementation; the profile costs one run.
      That the figure is a lower bound is deliberate: the run was cut at 595 s
      rather than left to complete, because because `CLAUDE.md` allows the
      reference core to be slow but this is the run loop and the cost is paid on
      every access for the whole life of the project.
      **Three things the oracle comparison needs, then**: which schedule matches
      the hardware, what B costs, and whether any *probe* golden separates them
      — the boot hash does, but a probe that separates them would localise the
      difference to a device rather than leaving it as a whole-machine hash. The bus now advances mid-instruction, but
      the processor cannot yet observe it mid-instruction — that needs the
      resumable sequencer increment (2) showed to be a rewrite of a 6,966-line
      file.
      **But a cycle-steppable processor is buildable without any of those four,
      and 3a is why.** The timeline already records an instruction's clocks in
      order, so a machine-level tick entry point (to be added)
      can **run one instruction ahead and hand its cycles out one at a time** — draining `clock_events` — instead of
      suspending the sequencer.
      **Its approximation, named up front**: the CPU's *internal* state commits
      at the instruction's start rather than progressively across its cycles.
      Everything outside the CPU — bus, arbiter, devices, interrupts — sees a
      true per-cycle machine, which is what "one `tick()` per machine cycle" is
      written for. The 350 M A/B is evidence the distinction is unobservable
      here: advancing devices at access instants already gave a byte-identical
      boot.
      **Cost to build**: `ap_machine_run`'s body splits into "execute one
      instruction" and "consume one cycle", with `pending_cycles` and an index
      on `ap_machine_t`; `ap_machine_run` becomes a loop over that tick. The identity harness is the check and the goldens must
      not move — a tick loop that reorders nothing must reproduce
      `A354786119A3931D` exactly.
      **BUILT.** `ap_machine_tick` advances the machine by exactly one machine
      cycle: a tick with nothing pending runs one instruction ahead and then
      hands its clocks out one at a time, `pending_cycles` at a time, with
      `defer_cycle_delivery` telling `ap_machine_run` to leave that delivery to
      the caller.
      **Additive rather than a rewrite**, which is why it landed without
      breaking the core loop: both fields are inert unless the tick is used, so
      the instruction-stepped path is byte-for-byte what it was.
      *Verification: `ctest` 138/138 and identity boot `A354786119A3931D`
      unchanged.*
      **DRIVEN AND VERIFIED.** `--cycle-stepped` runs the boot one machine cycle
      at a time through the tick instead of one instruction at a time, and the
      two produce **byte-identical state**: `9E6CD6DA5B9DD8A8` at 20 M and
      **`A354786119A3931D` over the full 350 M boot**, `ctest` 138/138, all
      reachable frontend flags exercised.
      **So the machine now steps by machine cycle, and it is the same machine.**
      That is the item's central claim demonstrated rather than designed — and
      the path is exercised rather than merely present, which is what
      `check_what_is_called_by_nobody` warns about and what a hash from a flag
      nothing drove would have hidden.
      **The one approximation, named and unhidden**: the CPU's *internal* state
      commits at the instruction's start rather than progressively across its
      cycles. Everything outside the CPU — bus, arbiter, devices, interrupts —
      sees a true per-cycle machine. Closing that last gap is the 6,966-line
      sequencer rewrite, and the 350 M A/B says nothing on this machine can
      currently tell the difference.
      **And the case that *would* tell the difference is nameable**, which turns
      the approximation from vague into testable. Nothing inside the CPU is
      visible to anything else, so the only observer is a **second bus master
      taking the bus part-way through a multi-transfer instruction** — a DMA
      cycle landing between two of `MOVEM`'s writes would see half the registers
      stored on real hardware and either none or all of them here.
      **Two reasons it does not bite today, both already established**: `RMC`
      makes a read-modify-write indivisible and refuses arbitration for its
      duration (`[030]` §7.3.6, and `ap_m68030_arb` implements it), so the
      RMW case is closed by the hardware's own rule; and the 350 M boot's DMA
      never lands inside a `MOVEM`, which is why the A/B is byte-identical.
      **So the test to write before the rewrite** forces DMA to take the bus
      mid-`MOVEM` and asserts what memory holds — **and it cannot be a probe**.
      `ap_probe_run` executes over flat caller-owned RAM with **no board**, so
      there is no arbiter and no DMA controller to contend with; probes are a
      CPU-only harness by construction.
      **Nor does a suite host it yet.** `master_suite` covers the AT bus-master
      handshake at signal level — cascade, `MASTER_L`, `DACK`, `AEN` — and
      `arbiter_suite`/`dma_suite` likewise. None of them runs a *CPU
      instruction* while a master holds the bus, which is the whole of what this
      test needs.
      **And the scaffolding cannot be built as a test at all, because the board
      has no bus master to build it from.** `ap_board_t` owns an
      `ap_arbiter_t` and **no `ap_master_t`**: the master model exists, is
      tested at signal level by `master_suite`, and is **connected to no
      board**. So nothing in this core can currently make an external master
      contend with the processor, and the `MOVEM` test has nothing to assert
      against.
      That is the `check_what_is_called_by_nobody` pattern again — a complete,
      green-suited module wired to nothing — and it is a **core gap in its own
      right**, independent of the per-cycle item: a DN3500 with an AT bus master
      cannot presently take the bus from the CPU at all.
      **Step one is DONE**: `ap_board_t` now owns an `ap_master_t`, and it is
      ticked wherever the arbiter is — all three sites — against
      `board->dma.controller[0]`. A module that was complete, green-suited and
      **attached to nothing** is now attached, so a board finally has a second
      claimant for its bus.
      *Verification: `ctest` 138/138 and identity boot `A354786119A3931D`
      unchanged, which is what an **idle** master must do — it stays idle until
      something asserts `DRQ`, so a board with no card in a master-capable slot
      behaves exactly as before.*
      **And it is attached and exercised.** `ap_board_attach_master(board, unit,
      channel, drq)` records what a card announces — passed rather than fixed,
      because this board's DMA cascade wiring has not been measured and
      inventing a pairing would put a number in the core no document backs.
      `board_suite` then drives the wiring itself: idle before attach, idle
      after attach, and `REQUESTING` one board tick after `DRQ` — so the board's
      own clock is shown to reach the port, which is the whole of what the
      wiring claims.
      *Verification: `board_suite` 54 -> 55, `ctest` 138/138, identity boot
      `A354786119A3931D` unchanged.*
      **And it is acknowledged on the board's own clock.** With
      `board->dma.controller[0]`'s channel put in cascade mode and unmasked —
      `[8237]` Figure 6's register 11, then register 10 — the port reaches
      `ACKNOWLEDGED` under `ap_board_bus_ticks`. `board_suite` asserts the
      negative first: requesting is *not* winning, and a port that reached
      `ACKNOWLEDGED` without the channel programmed would be granting a bus
      nobody offered.
      **So a DN3500 now has a real second claimant for its bus**, which it did
      not before, and `ap_master_t` is exercised through a board rather than a
      rig of its own.
      *Verification: `board_suite` 55 -> 56, `ctest` 138/138, identity boot
      `A354786119A3931D` unchanged.*
      **What remains for the `MOVEM` test**: a master that *owns* the bus while
      the processor is mid-instruction.
      **And attempting it found a contradiction worth chasing.** `master_suite`
      is explicit that "acknowledged is not owned" — the adapter is *offered*
      the bus and takes it by asserting `MASTER.L` — and asserts
      `!ap_master_owns_bus` immediately after `ACKNOWLEDGED`. On a **board**,
      after `ap_board_bus_ticks(32)` with `DRQ` held, `ap_master_owns_bus` is
      already **true** with `MASTER.L` never asserted.
      **Read `ap_master.c`, and both readings are wrong — which makes the
      observation the interesting part.** `AP_MASTER_OWNS` is reached only by
      `if (port->master_l && in_cascade(port, dma))`, and `ap_master_init`
      **memsets the port**, so `master_l` is false after
      `ap_board_attach_master`. The model and `master_suite` agree with each
      other; the board observation agrees with neither.
      So something in the board path is not what it appears: either the port
      being ticked is not the one being asserted on, or `master_l` is set by
      something unlooked-for, or the run did not use the binary it appeared to.
      **RESOLVED: there was never an anomaly.** The failing line was the *last*
      assertion, not the first — `owns_bus` is correctly false at
      `ACKNOWLEDGED` and correctly true after `MASTER.L`. What failed was my own
      final assertion that *"releasing MASTER.L ends the tenure"*, which
      `ap_master.c` contradicts in a comment I had already read and not applied:
      **"until it releases the DRQx and MASTER.L signals" — both.** Dropping
      `MASTER.L` alone leaves the tenure standing, the mirror of
      `master_suite`'s `releasing_drq_alone_does_not_give_the_bus_back`.
      So the model, `master_suite` and `008778-03` §2.4.7 all agreed throughout;
      the wrong thing was a comment I wrote from assumption. **Step 4 is done**:
      a master attached to a board is acknowledged, takes the bus on `MASTER.L`,
      and holds it until *both* lines drop — asserted on the board's own clock.
      *Verification: `board_suite` 56 tests, `ctest` 138/138, identity boot
      `A354786119A3931D` unchanged.*
      **A real defect was found on the way, though**, and it was mine: `ap_master_tick` was inserted by string replacement, and
      `"  ap_arbiter_tick"` is a substring of `"    ap_arbiter_tick"`, so both
      replacements fired at the two indented sites and the master was **ticked
      twice per clock**. It compiled and every test passed, because an idle
      master does nothing twice as readily as once — which is exactly how a
      wiring defect hides behind a green suite.
      Collapsed to one call per arbiter tick, three sites, `ctest` 138/138,
      identity boot unchanged. **The `owns_bus` anomaly should now be re-tested
      against the fixed wiring before anything is concluded from it.**
      *(Original guidance, still sound:* — a two-assertion test
      that attaches, ticks, and prints the state — because an unexplained
      observation that contradicts both the source and an existing suite is
      more likely a fault in the experiment than a fault in the model, and this
      session has already produced one of those (the `>119x` that was an
      argument-parsing hang).
      The attempted test was reverted rather than adjusted: patching the
      assertion to match the behaviour would have buried exactly this.
      **Step 5, now unblocked and specified.** Build a machine with a board,
      attach a master, put its channel in cascade and unmask it, stage a
      `MOVEM.L` storing several registers to RAM, and make the master take the
      bus *while that instruction is executing*. Assert what memory holds.
      **The expected result is all-or-nothing**, and the test must be written so
      that this is a *finding* rather than a pass: our CPU runs whole
      instructions, and `ap_machine_run`'s arbitration stall is checked
      **between** instructions, so a master cannot land inside a `MOVEM`. On
      real hardware it can, and would leave some registers stored and others
      not.
      **The trap to avoid**: a test that merely runs a `MOVEM` with a master
      requesting will pass trivially, because the master never wins mid-
      instruction and memory is whole. It has to assert that the master *owned
      the bus* across the instruction boundary — `ap_master_owns_bus` true
      before and after — or it proves nothing. Run it under **both** stepping
      modes; if they agree, the approximation is consistent, and the number of
      registers stored is the figure the sequencer rewrite would change.
      **MEASURED, and both "defects" below were misreadings of my own test.**
      Probing `arbiter->master` either side of the run gives
      `owns=1 master=3 may_run=0` **before and after, unchanged**. So the
      master's claim *does* reach the arbiter, `ap_board_processor_may_run` *is*
      false, and the tenure *does* survive the run — (b) is simply wrong, and
      (a)'s premise ("nothing translates ownership into a claim") is wrong too.
      **What actually happens**: `ap_machine_run`'s stall is
      `while (!ap_board_processor_may_run(...) && stalled < AP_MACHINE_STALL_LIMIT)`
      — **bounded**. The processor waits, hits the limit, and proceeds anyway.
      So a held bus delays the CPU by a fixed cap rather than stopping it, which
      is a deliberate anti-hang measure and not a wiring gap.
      **That is a real and different finding**: a master can hold the bus
      indefinitely on this hardware, and this core will run the processor after
      `AP_MACHINE_STALL_LIMIT` clocks regardless. **And its own comment answers it**: "Not a
      timeout: a master that never releases the bus is a broken machine, and a
      reference core that spun forever inside a bounded run would turn that into
      a hung harness instead of a visible fault... Generous enough that no real
      transfer reaches it."
      So 4096 is a documented anti-hang guard, not a hardware figure — and **the
      test was the unrealistic thing, not the model**. It held the bus
      indefinitely, which is precisely the "broken machine" the guard exists to
      make visible.
      **So the `MOVEM` test asserts the hold, with a bounded tenure.** Take the
      bus, run the machine for well under 4096 clocks, assert the processor made
      no progress and memory is untouched; release both lines, run again, assert
      the whole transfer landed. That is a realistic transfer and it exercises
      the stall rather than the guard — and the all-or-nothing result it should
      then show is the per-cycle approximation this whole chain was built to
      measure.
      **Written, and it gets three assertions of four.** The tenure is real —
      `owns_bus` true and `processor_may_run` **false**, so the arbiter has
      genuinely granted mastership away — and a held bus costs the processor
      `>= AP_MACHINE_STALL_LIMIT` clocks for one instruction. The stall is
      *demonstrated*, which no earlier attempt in this thread managed.
      Two corrections on the way, both test-side: the handback takes clocks
      exactly as winning the bus did (two ticks is not enough, sixty-four is),
      and asserting the PC never moved would have been asserting the anti-hang
      guard rather than the stall.
      **And it passes.** The readback was the last obstacle and it was a
      harness question, as suspected: board tests read the **flat `ram[]`
      array**, not `ap_machine_read` at a mapped address — the pattern was
      already in `machine_suite` at line 263 and I had reached for the wrong
      one.
      **So the measurement is taken.** A cascaded AT master, taken to ownership
      on the board's own clock, makes `ap_board_processor_may_run` false and
      costs the processor `AP_MACHINE_STALL_LIMIT` clocks for a single
      instruction; with both lines dropped the bus returns and the `MOVEM.L`
      lands **whole** — all four registers, never part of the transfer.
      **That is the per-cycle approximation measured rather than asserted**, and
      the figure the sequencer rewrite would change is now on record: four
      registers of four. On real hardware a tenure beginning between two of the
      transfer's cycles would leave some subset stored; this core cannot express
      that, and now has a test that says so in those terms.
      *Verification: `machine_suite` 55 -> 56, `ctest` 138/138, identity boot
      `A354786119A3931D` unchanged.*
      **The superseded reading follows, kept because the route to it matters.**
      A `MOVEQ`/`MOVEA`/`MOVEM.L D0-D3,(A0)`/`STOP` program, with a cascaded
      master taken to `owns_bus` on the board's own clock, then run:
      **(a) an owning master does not stall the processor.** The PC advanced
      `01000100` -> `01000116` — the whole program — while the tenure stood.
      `ap_machine_run` has the stall ("the processor is the lowest-priority
      claimant of a bus somebody else is holding", `[030]` §7.7) and a master
      that owns the bus does not trigger it: `ap_master_owns_bus` is the port's
      own state and **nothing translates it into a claim the arbiter grants
      away**, so the CPU never loses.
      **(b) the tenure does not survive the run.** After
      `ap_machine_run(&machine, 32)` the master no longer owns the bus, with
      `DRQ` and `MASTER.L` both still asserted — so something in the board's
      own ticking drops it.
      **The test was reverted rather than bent to pass**, because a red suite is
      this project's stop-everything condition and an assertion trimmed to the
      observed behaviour would have buried both. Fix (a) and (b) first — they
      are wiring defects in the step-1-to-4 work, not the per-cycle
      approximation — then this test measures what it was written for.
      **Where (a) has to be fixed, traced**: the stall consults
      `ap_board_processor_may_run`, which is exactly
      `ap_arbiter_processor_may_run(&board->arbiter)`. So a master's tenure only
      stops the processor if it becomes **a claim the arbiter grants away**, and
      `ap_master_tick` does raise one — `holding = state == ACKNOWLEDGED ||
      state == OWNS`, passed to the arbiter on `port->drq`. The next read is
      therefore why that claim does not make `ap_arbiter_processor_may_run`
      false: whether the line needs enabling, whether the arbiter ranks it below
      the processor, or whether the DRQ number chosen by
      `ap_board_attach_master` is not a line the arbiter arbitrates. **Read, and it narrows to one question.**
      `ap_arbiter_processor_may_run` is
      `ap_m68030_arb_processor_is_master(&arbiter->cpu) && arbiter->master ==
      AP_ARBITER_PROCESSOR` — two conditions, and the manual's §7.7.3 window is
      why. So the processor stalls only when `arbiter->master` is **not** the
      processor.
      And the master *did* reach `ACKNOWLEDGED`, which `master_suite` says can
      only happen once arbitration has been won — so the arbiter granted, and
      then either handed mastership back during `ap_machine_run`'s own board
      ticking, or never set `arbiter->master` away from the processor in the
      first place. **Those two are distinguishable by printing `arbiter->master`
      either side of the run**, which is where this goes next and is a smaller
      question than any so far. It is also the same question as (b): a tenure
      that ends without either line dropping is one the arbiter took back.
      **Then the rest of the order**:
      *then* the `MOVEM`-versus-DMA test becomes writable; *then* it decides
      whether the sequencer rewrite is worth its cost. Three items, and the
      first was not previously known to be missing. If it passes under
      both stepping modes the approximation is invisible even to the case built
      to catch it; if it fails, that is the first evidence the sequencer rewrite
      is worth its cost — and better evidence than the item's own assertion.
      **Passed the bus explicitly at all twelve rather than defaulting a NULL to
      a local.** A NULL fallback would cost zero test edits and is the tempting
      shortcut, but from increment (3) the bus is *resumable state*, and a test
      that does not name the bus it ran on is a test that cannot check one.
      Better to pay the eleven edits now, while they are trivial, than to hide
      the parameter that the last increment depends on. Do it as its own
      commit with `ctest` and the identity boot before anything in (2).
      **Adding the field is hash-safe, checked**: `ap_m68030_hash_cpu`
      (`ap_m68030_state.c:234`) walks **named fields** — `regs`, `tc`, `crp`,
      `srp`, `tt0`, `tt1`, `mmusr` — not the struct's bytes, so a new member is
      invisible to the identity hash until deliberately hashed. **Which raises
      the design question step 1 must answer**: hash the hoisted bus, or not.
      While it is still ticked to completion inline it is always idle at an
      instruction boundary, so hashing it adds only zeros and cannot move a
      golden; from increment (3) the CPU can stop *mid-cycle*, and then the bus
      is real state a resumed run must agree on. Add it to the hash at (3), not
      before, and say so in that commit; (2) give `ap_m68030_step` a resumable state so it
      can return mid-access; (3) add `ap_m68030_cycle()` beside it and let the
      machine call that, with `step` kept as a loop over it; (4) switch the
      board's run loop. Only (4) can change interleaving, and only (4) needs
      the goldens re-blessed if it does — which the item says it must not.
  - [ ] **What the tick loop item deferred here**, so that the two are read
        together. Phase 3's loop advances each device to an absolute instant
        once per instruction, every device carrying its own remainder, and
        ticks the bus per clock. The two schedules agree on everything measured
        so far — remainders are carried, and the 68030 samples interrupts at
        instruction boundaries anyway — and they do **not** agree in general,
        wherever a device's output would feed back into an instruction still
        executing. That case is what a cycle-steppable CPU makes reachable, and
        it is the reason to want one beyond speed.
- [x] **Where the time goes, measured** — and the first thing the measurement
      found was that the profile was measuring the instrument. A stepped boot
      read one instruction word back per step to fill a trace column; with the
      MMU on that is a full walk of the tree, and at 350 M instructions it was
      **266,700,639 probe walks against the machine's own 56,688** — 99.98% of
      every table walk in the run. `perf` put `ap_board_read` at **28%** of the
      whole profile, four times the next entry.
      Made conditional on something consuming the word. Same binary, same
      bound, same state hash `67A14B3BB6041410`: **541 s → 339 s, a 1.60×
      speedup**, so the read-back was 37% of a plain boot's wall clock.
      *Verification: A/B on one binary via `--boot-trace-last 1`, which forces
      the old path; `ctest` 129/129 and every golden unchanged. The real profile
      and what it now says is in `PROJECT_STATUS.md`.*

- [x] Squeeze the reference core first. Every named candidate tried and
      measured against the 350 M boot state hash: LTO already on; cached
      re-derived values **refuted** (caching the 8259's resolver is 14%
      *slower*); cached arbitration **1.12x**; idle-skip guards in the timer,
      calendar and serial **marginal**; `flatten` **neutral** and not kept.
      One refutation turned out to be a real interrupt-controller defect — the
      master's cascade input was refreshed only by accident, through a
      per-instruction re-drive — now fixed with a regression test.
      Session net **1.83x**, 541 s → 296 s, hash `67A14B3BB6041410` throughout,
      of which the largest single win was **not** on this list: a frontend
      trace read-back walking the MMU tree once per instruction, worth 1.60x.
      *Verification: `ctest` 129/129 and every golden unchanged at each step;
      speed measured on release builds only. Detail, including what this item
      cannot reach and why that is the exact-skip item's job, in
      `PROJECT_STATUS.md`.*
- [x] **Main memory answered first in the board's region lookup** — a
      reordering rather than a decision, since no placement and neither graphics
      window overlaps either map's memory range, which is now asserted by
      `board_suite` instead of assumed.
      **And the identity-harness defect it surfaced**: the state hash covers a
      fitted display and the power-on epoch, and the report mentioned neither,
      so two runs differing only in `--screen` produce reports identical but for
      the number — which reads as a broken change. Both are printed beside it
      now, and `tools/identity-boot.sh` carries the canonical invocation. The
      reference is re-baselined to `0D8379A03105C0F7`; `67A14B3BB6041410` is
      retired, its configuration never having been recorded. Earlier A/Bs stand.
      *Verification: bit-identical to the pre-change binary at seven bounds to
      350 M across three separately built binaries; 273/275 s → 253.5/255.4 s;
      `ctest` 129/129, `board_suite` 36 → 37. Detail in `PROJECT_STATUS.md`.*
- [ ] Exact-skip scheduling: `next_event()` and `skip(n)` per subsystem, CPU
      half and devices half of the tick split so a span-breaking I/O write still
      runs its devices half canonically. *Verification: entire probe suite and
      long boot hashes byte-identical to the reference core.*
      **Awaiting:** the CPU half, and the two device-side increments above are
      done — **45.3 s → 30.3 s, 1.49x**, every step verified by an unchanged
      `A354786119A3931D` and boot reports identical line for line.
      **A re-profile has moved the target.** With the device work reduced, the
      instruction pipeline is now the largest share — `ap_m68030_step` 10.5%,
      `fill_to_decoded` 9.8%, `ap_m68030_decode` 4.4% — against
      `ap_board_write` 8.4%, `ap_board_sample_interrupts` 8.0%,
      `ap_sio_advance` 6.8% and `ap_board_read` 5.1%.
      **Two things are measured and should not be re-attempted blindly.** The
      sampler's residual 8.0% is the *firmware's* device polling, not the
      invalidation rule: narrowing that rule to device-only accesses changed
      30.398 s to 30.324 s, inside the noise, because a serial status read
      genuinely could have moved a line. And `ap_sio_advance` holds nothing
      structurally wasteful — its loopback tail is a field read and a `set_input`
      that already no-ops on an unchanged level, so its cost is per-call
      overhead at twenty million calls.
      **So what is left is the item's own title.** `skip(n)`: running the CPU
      across a span without ticking devices, which needs a bound on *observable
      state* rather than on an interrupt line — a stronger claim, because the
      serial part's output-port square waves are readable at any instant.
      **That blocker is now GONE, and it was a stored cursor rather than a
      hardware fact.** `ap_mc68681_t` carried a `now` refreshed on *every*
      advance, whether or not its counter moved, so that a program reading a
      free-running clock pin got a level. But §4.2.11.6's clock is a **pure
      function** of the instant and the programmed rate, so the instant belongs
      to `ap_mc68681_output_pin`'s caller and the part need remember no time at
      all — the same argument that took the PTM's `now` away. The field is
      deleted, `ap_sio_advance`'s unconditional two-store prologue with it, and
      the signature now *forces* a caller to name its instant, so the property
      is structural rather than only tested.
      *Verification: `ctest` 138/138, `sio_suite` 29 → 30 — advance once, then
      read the pin at instants no advance ever saw and watch the wave still
      move — and identity boot `A354786119A3931D` unchanged.*
      **But the aggregate bound it unblocks has a measured ceiling of 38%, and
      that is worth knowing before building it.** §3.9's memory refresh runs the
      serial part's counter off X1 for the life of the machine, and X1 is
      3.6 MHz against a 25 MHz CPU — **6.944 CPU clocks per pulse against a mean
      instruction of 4.278**, so 0.616 pulses fall per instruction and only
      **38.4%** of instructions have no pulse due. The serial part is therefore
      the binding term in any whole-board `next_event()`, and the most a
      device-side skip can remove is 38.4% of `ap_board_advance`'s calls. Not
      nothing, and not the order-of-magnitude the item's title suggests: this
      board has a device whose observable state changes faster than the
      processor executes. Detail in `PROJECT_STATUS.md`.
      **The remaining work was built, and it is REFUTED by measurement.**
      `next_event()` per device, the aggregate minimum on the board, the same
      three invalidation sites and a gate in `ap_machine_run` -- all of it, and
      it makes the reference core **11.8% slower**. Three interleaved pairs,
      base `31.22/31.04/31.01` s against `34.74/34.77/34.81`, with the
      interrupt half of the bound taken from the cache
      `ap_board_sample_interrupts` already fills; recomputing it was 11% slower
      again. The reason is structural: the bound is recomputed after *every*
      advance and skips at most 38.4% of them, so it is paid eight times for
      every three it saves. Not landed -- a 12% slowdown for no gain is
      weakening the reference core, which is the one thing this phase may not
      do. A version that could win would need an **incremental** bound, each
      part publishing when its next event moves rather than the board asking,
      and that is a different design from this item's.
      **And it found a defect that outlives it.** The item calls this increment
      "provably identity-preserving"; as specified it is not. Four parts -- the
      disk, tape, keyboard and graphics -- date deadlines from a stored `now`
      that only the advance refreshes (`ap_omti.c:352`,
      `completion_at = omti->now + duration`), so a skipped advance made the
      next disk command complete early: hash `03EE415450926A89` became
      `58105715891CEA4E`, 99 clocks apart, bisected to between 100 M and 200 M
      instructions. This is the third instance of a pattern already cured twice
      here, in the PTM and the DUART. **Carrying the four cursors makes it
      byte-identical**, and the prerequisite is now named for the per-cycle
      processor item, which cannot stop advancing the board every instruction
      until those four are dealt with. Detail in `FINDINGS.md` C152.
      **The carry itself is NOT in the tree** -- checked 2026-08-18 rather than
      assumed: it was part of the reverted patch and went out with it, so the
      board-level carry helper that patch added is nowhere in `src/`, and
      `ap_omti.c:352` still reads
      `omti->completion_at = omti->now + duration`. Not a live defect --
      `ap_board_advance_one` refreshes each device's cursor on every access that
      reaches it, so the stored `now` *is* the caller's instant today -- and
      exactly the latent hazard the next schedule has to clear first.
      **And it cannot be done one small part at a time**, which was tried and
      abandoned on 2026-08-19. The graphics cursor looked like pure residue --
      `advance` writes it, `ap_graphics_beam` reads it, and the part's own
      comment says the raster is "a function of the instant, not an
      accumulation". It is not: `graphics_status` reads the beam from the
      **register-read** path, and a register read has no instant of its own.
      Every one of the four is that shape. So the instant has to reach
      `ap_board_read`/`ap_board_write`, which is 120 call sites, 114 of them in
      tests -- a single mechanical change to make with the schedule it is for,
      not a series of small ones.
  - [x] **The timer advance is skipped until a pulse is due**, the PTM keeping
    no `now` of its own so nothing can be left stale. *Verification: interleaved
    A/B, faster in all three pairs, 29.65 s against 30.20 s; hash and reports
    unchanged; `ctest` 137/137.*
    **And the serial part's equivalent was tried and removed**: provably safe,
    but no measurable direction at n=3, so it is not worth a call per
    instruction. Its blocker turned out to be hypothetical all the same --
    `duart->now` is read only through `ap_mc68681_output_pin`, which no
    production code calls. Detail in `PROJECT_STATUS.md`.
  - [x] **The bus tick is batched when `n` ticks are provably one** -- no DMA
    able to ask and an idle arbiter, both guards being the ones the per-tick
    path itself tests. *Verification: 39.9 s → **30.4 s**, 1.311x, state hash
    unchanged and the two boot reports identical line for line; `ctest`
    137/137. Detail in `PROJECT_STATUS.md`.*
  - [x] **The interrupt sample is skipped when no source can have changed** --
    `next_event()` for interrupt sources, an aggregate bound, and
    `interrupt_valid_until` discarded by the three sites that reach a device.
    *Verification: 45.3 s → **39.9 s**, 1.136x, state hash `A354786119A3931D`
    unchanged; `board_suite` 47 → 50, `ctest` 137/137. Detail, and why the
    serial part nearly made it worthless, in `PROJECT_STATUS.md`.*
- [ ] Extend exact-skip across nodes: run node cores in parallel only within
      provably inert windows between ring events. *Verification: whole-ring
      state hash identical to the single-threaded reference.*
      **That blocker is half cleared, 2026-08-19.** It read "`--ring-two-node`
      runs two machines that execute their boot PROMs, but **neither runs
      Domain/OS**", and both nodes now do: two whole machines with distinct node
      IDs on one `ap_ring_sched` segment, each reaching
      `Domain/OS kernel(7), revision 10.4` from its own installed volume, no
      copies and no relabelling. So a multi-node workload whose wall clock this
      could improve **exists**, and there is a whole-ring state to hash beyond
      firmware.
      What is *not* cleared is the prerequisite above it: this item extends the
      exact-skip machinery across nodes, and that machinery's remaining half is
      refuted by measurement (11.8% slower as specified, needing an incremental
      bound instead). So the order stands -- exact-skip first, with a design
      that wins -- but the reason has changed from "there is nothing to
      optimise" to "the thing being extended is not finished".
      **And one part of the design is already refuted by measurement.** The
      window between ring events is not the binding term: §3.9's memory refresh
      runs each node's serial counter off X1 for the life of the machine, at
      6.944 CPU clocks per pulse against a mean instruction of 4.278 (the
      exact-skip item above), so a *node* is provably inert for well under one
      instruction at a time whatever the ring is doing. Parallelism across nodes
      therefore cannot come from inert windows in the device sense; it would have
      to come from the ring's own latency being long enough that two nodes cannot
      observe each other inside it, which is a different argument and is not the
      one this item is written on.

## Phase 9 — Content testing

- [x] `docs/references/TEST_SHELF.md`: Domain/OS releases and applications
      organised by the subsystem each stresses. Every row is marked `observed`
      or `expected` — a shelf that mixes what has been run with what has been
      reasoned is a list of things someone once assumed. Nine subsystems are
      already reached by the reference boot alone and its report counts each;
      six need content we either cannot reach yet or do not hold, and the
      releases and applications we lack are named rather than implied.
      *Verification: every cited path is in the repository and `doc_claims`
      checks that.*
- [x] Boot every Domain/OS release obtainable — **SR10.2, SR10.3, SR10.4**.
      *Verification: each boots on this core from its own disk, with its
      state hash — SR10.2 `E5147CD96AB052D9` at 1.5 G instructions, reaching
      `SPM Initialized`.* The scope is the set that exists: there is no SR9.7
      or SR10.1 install media online, and SR10.4.1 is an upgrade over SR10.4
      rather than a release to install from bare metal.
      Detail — the media census, the calendar gate, why a restored volume is
      not a bootable one, the oracle's tape underrun abort, and the four-link
      chain SR10.2 needed — in `PROJECT_STATUS.md`.
- [ ] Boot every firmware revision we hold, including both `3000_BOOT` revisions
      and both ring board generations.
      **Done and recorded**: six boot PROMs across five models, each with its
      state hash and what it did. It found one defect of ours — the frontend
      fitted 16 MB to every model, twice what a DN3000 takes, which left the
      boot PROM's sizing strap unset and failed its memory test with
      `E0060882`. Memory size now comes from the model table, `--ram` selects
      it, and both DN3000 revisions pass into Memory Module 2. Detail and the
      display-redirects-the-console trap in `PROJECT_STATUS.md`.
      **Awaiting**, each named there. The **DN2500 now loads its own 128 K PROM
      and runs**, on a `DS2500_MAP` holding only measured placements, and stops
      at a named gap: its core block is **not** the Series 4000's — thirty-two
      four-byte registers where that has one aliased across 256 bytes — so the
      range was left undecoded rather than answering another machine's register.
      *Superseded 2026-08-19 by the entry below*: the block is now decoded as
      `AP_BOARD_REGION_S2500_CONTROL`, **storage and `PROVISIONAL`**, because
      the firmware's own reset code requires `0202D4` to read back what it
      writes and spins for ever otherwise. That is the same restraint, not a
      reversal of it -- the range answers *itself* rather than another machine's
      register -- and it took the machine from 18 instructions to 60 M.
      Reference, web and oracle are all exhausted for a Series 2500, so
      finishing it is an undocumented-hardware project of its own rather than a
      tail of this item.
      **Re-verified rather than carried forward on trust** (2026-08-18):
      bitsavers' whole Apollo index holds **no** Series 2500 document of any
      kind, and its only register-level hardware reference for any model is
      `008778-03`, the Series 3000/4000 manual this project already has. A web
      search for a Series 2500 core-board or service manual returns
      configuration guides and parts lists and no hardware reference. **What
      would unblock it is a Series 2500 hardware manual or a machine to
      **Three DS5500 registers the addendum documents and this core does not**
      (`FINDINGS.md` C236), found by reading `019411-A00` as **page images**
      after a text-layer grep had declared it silent. §4.2.1.14 **DS5500 Cache
      Status Register**, 8-bit read-only, `HSI Present <3>` cleared when a
      graphics device is in the HSI connector and `MEM Time <0>` for an access
      to non-existent memory; §4.2.1.18 **DS5500 Memory Present Register**,
      8-bit read-only, `MEM Present <7-0>` cleared when boards are present with
      each consecutive **pair** of bits identifying a slot, P25/P24/P23/P22
      right to left; and Table 4-6's added line `PC ON/OFF -- Physical Cache
      (DS4500 Only)`. The first two are the shape the memory strap needed on
      other models -- read-only registers a boot PROM consults to size what is
      fitted -- so they belong with this item even though a 68040 core is what
      makes them reachable.

      **A third tail, found by audit on 2026-08-19 and sized rather than
      guessed** (`FINDINGS.md` C226, C227). Two whole parts are built, tested
      and **unreachable from any machine** -- `src/core/cpu/m68040/` (3,181
      lines, 13 modules) and `src/core/cpu/m68851/` (2,039 lines, 9 suites) have
      **zero** call sites anywhere in `src/` outside their own directories. And the field that would
      select between them is consumed only by a hash-scope *name* and a line in
      the frontend's report, so `.mmu = AP_MMU_M68851` on the DN3000 rows is a
      declaration the machine does not honour: `ap_machine` builds an
      `ap_m68030_cpu_t` unconditionally, which is the same root cause as the
      DN5500 stopping at its second instruction.
      *Not a live defect* -- the DN3000 diffs pass, so its firmware does not
      depend on the difference within the window measured -- but it is exactly
      what `model/`'s own rule forbids, "all machine variance lives here", and
      **and the inference drawn from that on the day was wrong.** It read "so
      this item's remaining work is *one* change: teach `ap_machine` to build
      the CPU and MMU its model row names, the 68040 and 68851 halves being
      already written and tested behind it". Checked an hour later: **no 68040
      CPU type or step function exists anywhere in the tree, and no 68020 core
      either** -- only the 68030 has an execution core at all. What those directories hold
      are the *parts a core would use* -- MMU descriptors, ATCs, caches, an
      FPU, timing tables -- not a core, so `ap_machine` cannot be taught to
      build one that does not exist. The DN5500 tail stays what it was: build a
      68040 execution core, against the 68030's 14,034 lines of the same.
      *The audit's facts stand -- 22 suites reachable only from their own tests,
      and a model table whose `.mmu` only prints. The reading that this made the
      work small did not, and it is the same shape as C220: a plausible
      conclusion from real evidence, wrong because one thing was not checked.*
      **And the same shape appears a third time in the same table** (C228).
      Counting readers for every field of `ap_model_t` outside `ap_model.c`
      puts `cpu` at 110, `fpu` at 30, `ram_base` at 10 -- and **`has_ring` at
      zero**. It is declared, set on eleven rows, and referenced outside the
      table only by `model_suite`, which asserts the rows are what the table
      already says; the card is fitted by `--ring` instead. So a row claiming a
      card the machine never consults sits beside a row claiming an MMU the
      machine never builds.
      *Three sub-items, each small and none started here*: `ap_machine` builds
      the CPU its row names; `.mmu` selects the MMU its own enum documents; and
      `has_ring` either gates the default fit or is deleted. **The middle one is
      not behaviour-neutral** -- an MMU with a different descriptor format is a
      translation change -- and the third is not either, since gating the fit
      would put a card in machines the reference boot has none in. Both need the
      identity harness on the other side, which is why they are named rather
      than done in passing.

      **DN2500, 2026-08-19: from 18 instructions to 60 M, and the next blocker
      is named.** The tail recorded here -- "the Series 2500 PROM is 131072
      bytes against `AP_BOARD_PROM_SIZE`" -- was already closed: `DS2500_MAP`
      carries `prom_size 0x020000`. What actually stopped it was its **second
      instruction**, and its own reset code says so:

          0001F040  move.b #$1F,$00020800    ; the posted code, mapped
          0001F048  move.b #$FF,$000202D0    ; <- bus error, nothing decoded
          0001F050  move.b #$40,$000202CC
          0001F058  move.b #$2, $00020800
          0001F060  move.b #$1, $000202D4
          0001F068  move.b $000202D4,d0 / andi.b #$0F,d0 / cmpi.b #$1,d0 / beq
          0001F078  ... else spin here for ever

      So `0202D4`'s low nibble must read back what was written. **It is not the
      DN3500's core-register page at this family's displacement**: that PROM
      references none of `0202CC`, `0202D0` or `0202D4`, searched exhaustively
      over the image. Series 2500-only, no document on disk, and a fresh web
      search finds none -- `[CFG]`'s Product Summary is configuration, not
      registers -- and the oracle has no 2500 driver.
      Modelled as `AP_BOARD_REGION_S2500_CONTROL`: **storage, `PROVISIONAL`**,
      the same restraint the ring's unmapped slots are kept under. `020000` and
      `020100` *are* the DN3500's CPU status and control at the same
      displacement, which is the reading three other blocks in this map already
      make (`010400`->`020400`, `010800`->`020800`, `010C00`->`020C00`).
      *Verification: `board_suite` 56 -> 57; the machine runs **60 M
      instructions with zero bus errors**, PC `0000BB06`, against 18
      instructions and a fault before.*
      **The next blocker, measured rather than guessed**: it prints nothing and
      sits with `a1`/`a2` at `00020200` and `a3` at `00020280`, so the block is a
      real device with register sets `$80` apart and the firmware is polling a
      status bit. **Storage cannot satisfy a bit that must change**, which is
      exactly the limit a storage model has. Closing it needs a Series 2500
      register document or a 2500 driver to instrument, and neither exists
      today -- so this is `PROVISIONAL` with its cost to close named, not a
      module claimed finished.
      The DN4500's memory strap is
      **solved**: the firmware decodes it with a fourteen-arm `cmp.b` chain,
      identical in the DN3500's PROM, and the oracle's four bank layouts fall
      out of it unchanged. Its self-test failure was our unstrapped port reading
      `00`, which the firmware reads as twenty megabytes rather than as no
      answer. Still open: the DN5500, now precisely diagnosed — it stops at its
      second instruction, `cinva`, because the 68040 is modelled and nothing
      executes on it. **Re-verified 2026-08-18 down to the opcode**: `4E71`
      (`nop`) at `00060C`, `F4D8` (`cinva bc`) at `00060E`, and the F-line is
      taken there — `stopped on vector 11 taken from PC 0000060E, after 1
      instruction(s)`. What follows is worth knowing so "stops" is not
      misread: the run does *not* halt there. The 68030 core takes the F-line
      correctly, the PROM's handler runs ~135 more instructions, and the
      machine ends on a bus error writing its exception frame at `FFFFFFFC`
      with `a7` zero. So the core's exception path is not what is missing;
      the instruction is. **Both ring generations now boot**, and Domain/OS's own
      self-test suite passes with the card fitted -- `Apollo Token Ring test
      passed.`, `above driver type loaded.`, `Self tests passed.` -- with
      reproducible hashes for each. It found one defect of ours that no
      self-test could reach: unit 1 answered as a second card, because the
      decoded unit was discarded. Detail in `PROJECT_STATUS.md`.
      *Verification: `frontend_flags` 13 → 16; DN3500 30 M hash unchanged.*
- [ ] Real multi-node Domain workloads: distributed single-level store across
      nodes, `lcnode`, remote file access. *Verification: content finds what
      unit tests did not; each finding lands with a test.*
      **Blocked on a second *installed* volume, and this objection still
      stands after C180.** Every workload here is a thing two booted nodes do.
      Patching a copy's label -- which is how the Phase 6 item's two nodes are
      now made -- is **not** the route for *this* item: the objects on the copy
      were created by the first node and carry its ID, so distributed
      single-level store and remote file access would be reading two volumes
      whose UIDs collide, and the findings would be findings about a fiction.
      The boundary is exact: ring **membership** is a function of the node a
      machine presents, and C180's relabelling settles that; anything reading
      **object UIDs across the ring** is not, and this item is all of that.
      **The second install now works and this item is startable** (C199): node
      B's volume boots. What had stopped it was this frontend never giving a
      machine its disk's node on the `--disk` path, so a volume recording node
      `22222` ran on a machine calling itself `12345` and Domain/OS declined to
      run as a node its disk contradicts. Given its own node the same volume
      reaches `Starting standard daemons:.`, hash `5671D8D76ACDC046`.

## Deferred tails

Nothing is deferred silently. Current list:

- Apollo PRISM / DN10000 — **out of scope**, permanently: different
  architecture, no ROM dumps, no oracle.
- HP Series 400 (68030/68040 machines that also ran Domain/OS) — out of scope;
  a different machine family with its own MAME driver.
- DN3xx / DN4xx 68010 era, where the ring was standard equipment — out of scope
  for now: no runnable oracle and an Apollo custom MMU documented on paper only.
  Revisit once the ring works, since these are its native machines.
- Process-separated multi-node transport — designed for, not built. Would be an
  explicitly non-deterministic mode, documented as such, owning no goldens.
- `.ecm` and other formats we will not support — document the refusal when the
  media layer lands.
- ~~**DN3550** (Series 3550)~~ — **done**, Phase 5 having closed. `dn3550` and
  `dsp3550` are in the table from `[CFG]` pp. D-77/78 and D-96/97: the DN3500's
  68030 and 68882 at 25 MHz, 8/16 MB to 32 MB, and the 19-inch 1280×1024
  monochrome panel that is the one thing distinguishing it. Detail in
  `PROJECT_STATUS.md`.
- **DN4000** (and DN3000-to-DN2500 / DN3500-to-DN4500 upgrade paths) —
  `[CFG]` documents DN4000 as the prior-generation 68020 sibling of DN3000. Not
  in the agreed scope and not in MAME; revisit only if the 68020 path proves it
  cheap.
- ~~**Display variants as first-class configurations**~~ — **decided**, Phase 5
  having closed, and the decision is the one already built: a display is a
  **run-time option** (`--screen c4p|c8p|19i|15i`) over a model row that names
  the machine's *base* panel, not a separate model per panel. MAME's
  `dn3500`/`dn3500_19i` split is rejected: the panel is a purchase option in
  `[CFG]`'s own option lists (Opt. DM0, Opt. FM2), and a table row per
  combination would multiply nine models by four panels to describe one field.
  The DN3550 is a model rather than a DN3500 variant for the opposite reason —
  `[CFG]` sells it as its own machine with its own ordering number.
