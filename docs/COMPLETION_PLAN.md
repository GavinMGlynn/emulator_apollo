# Completion plan

Phased road to done. Each item names **its verification** — an item without one
cannot be ticked. Tails discovered while implementing something go into this
file the moment they are found, not when someone remembers.

`[x]` done · `[~]` in progress · `[ ]` not started

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
        - Needs the `ext/mame` SC-499 edit of `FINDINGS.md` C56.
        - Told in full in `FINDINGS.md` C47-C58 and summarised in
          `PROJECT_STATUS.md`; the tooling it required is the next item.
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
        problem. *Verification: `probe_encoder`, 20 checks, each asserting the
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
        wrap or a zero; a run takes a limit. Rationale in `PROJECT_STATUS.md`.
        *Verification: `machine_suite`, 10 tests — the whole probe cycle; reset
        leaving supervisor state with interrupts masked at 7; an out-of-range
        access faulting rather than wrapping, including the straddling case; a
        runaway program stopping at its limit; a run reporting *why* it ended;
        an operator write not leaving a stale cache line; two machines on two
        different RAM buffers hashing alike at every step; the machine hash
        covering the RAM; the two caches being distinct; and a `PMOVE` reaching
        the registers the machine actually translates through.*
  - [x] **The probes themselves** (`src/core/probe/ap_probe.c`), eight small
        programs covering one thing each: a register write, a store and reload,
        a `DBcc` loop, `BSR`/`RTS`, a `TRAP` taken and returned from, the
        multiplies and divides, `MOVEM` out and back, and a `PMOVE`. A probe
        reports rather than judges, every probe ends with `STOP`, and the runner
        blanks RAM and plants a returning handler on every vector between
        probes. Rationale in `PROJECT_STATUS.md`.
        *Verification: `tests/goldens/probes.txt`, checked under every build
        preset and confirmed identical between `-O0` and `-O3`; plus
        `probe_suite`, 7 tests for what a golden cannot express — a golden will
        happily pin a probe that never terminates, faults, or differs run to
        run. Every probe terminating below its limit, none touching memory the
        machine lacks, the same answer twice on different buffers, a result
        independent of what ran before, no two probes being the same probe, and
        every probe carrying a name and a purpose.*
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
      platform. *Verification: goldens bit-identical on all four CI targets and
      both build types.* `cmake/Goldens.cmake` registers each golden as an
      ordinary CTest entry labelled `golden`, so every build preset checks it
      and no bespoke CI step is needed. Locally verified four ways: a perturbed
      golden fails with a named unified diff, restoring it passes, the release
      (`-O3`) build matches the same golden as the `-O0` build, and
      `goldens-update` regenerates byte-identically.
  - One block is pinned today, `model_table.txt` from `--list-models`. It
    supersedes the old `headless_lists_models` smoke test, which only checked
    the exit status that `regress.py` already checks.
  - Missing Python is a **fatal configure**, not a skipped test: a `ctest` run
    reporting green while pinning nothing would defeat the portability claim.
    `-DAPOLLO_GOLDENS=OFF` is the explicit opt-out, and says what it costs.
  - Tail: goldens are regenerated by the `goldens-update` build target rather
    than by hand, so a golden cannot be edited into agreement with a change.
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
        *Verification: `hash_suite`, 11 tests, including the **published**
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

- [~] 68030 integer core, strictly cycle-stepped: one `tick()` per machine
      cycle, no batching, no event queues. *Verification: probes against the
      oracle; `MC68030 User's Manual 3ed` for the paper timing figures, each
      cited.*
  - [x] **Bus cycle state machine** (`src/core/cpu/m68030/ap_m68030_bus.c`),
        built first because it is the bottom of the timing stack: emergent
        timing means every clock an instruction takes is a clock some real bus
        cycle took. Models the documented S0–S5 half-clock states, both
        termination paths, and wait-state insertion at S3. One `tick()` is one
        *clock* and runs that clock's two states in order, so the project's
        cycle rule holds while the manual's granularity is preserved rather
        than collapsed into a translation layer.
        *Verification: `bus_suite`, 17 tests, each citing its manual section —
        the three-clock asynchronous minimum (7.3.1), the two-clock STERM
        minimum and three-clock STERM-plus-one-wait (7.3.4 p. 7-48), one clock
        per wait state, ECS asserted for only its half-clock, DBEN trailing AS
        by a clock, all strobes negated at S5, and a never-answered cycle that
        must not quietly complete. Also asserts that every CPU clock in this
        machine has an even period in base units, so a half-clock is exactly
        representable — the state model would be silently lossy otherwise.*
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
  - [x] **Instruction pipe and cache holding register**
        (`src/core/cpu/m68030/ap_m68030_pipe.c`), `[030]` §11.2.2 p. 11-2. The
        three-word pipe (B → C → D, decoded at D), the per-stage abnormal-
        termination status bit, and the 32-bit holding register that makes
        alignment matter: an aligned prefetch loads the whole long word, so
        "the instruction word for the next sequential prefetch can then be
        accessed directly from the cache holding register, and no external bus
        cycle or instruction cache access is required".
        Detail in `PROJECT_STATUS.md`.
        *Verification: `pipe_suite`, 14 tests. The headline one counts bus
        cycles for four sequential words and gets 2 when long-word aligned
        against 3 when starting on an odd word — the difference counted rather
        than averaged. Others cover big-endian word selection, the three-stage
        decode latency, order preservation, the status bit travelling with its
        word (a bus error must fault where the word is *used*, not where it was
        fetched), a clean fill clearing it, and a holding-register miss leaving
        the stage empty rather than loading a stale word.*
  - [x] **Programming model** (`src/core/cpu/m68030/ap_m68030_regs.c`), `[030]`
        §1.3 with the `M68000 Family Programmer's Reference Manual 1992`
        §1.3.2, whose Figure 1-8 survives intact where the 68030 manual's does
        not — so the SR layout, the trace-mode table and the stack-selection
        table are all transcribed rather than derived.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `regs_suite`, 10 tests — the S/M table, the `x` being
        load-bearing (S=0 M=1 is still the USP), A7 reads and writes reaching
        the active stack while leaving the other two untouched, changing
        privilege reaching a different stack without copying anything between
        them, all four trace encodings including `11` reported as UNDEFINED
        rather than folded into another mode, the reserved bits never reading
        back, and a CCR write being unable to reach S and escalate privilege.*
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
        sixteen conditions `Bcc`, `Scc`, `DBcc` and `TRAPcc` share,
        `M68000 Family PRM` Table 3-19.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `cond_suite`, 9 tests. The headline one exhausts the
        whole space — all sixteen conditions against all thirty-two CCR states —
        asserting every pair complementary, which is what turns the
        transcription from trusted into verified. Also: X taking part in no
        condition (checked over the same whole space), `HI`'s four input
        combinations individually since it is the entry whose bars were lost,
        the signed comparisons where a sign error hides, `GT` differing from
        `GE` only by Z, and T/F unavailable to `Bcc` because those encodings are
        `BRA` and `BSR`.*
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
        MOVEQ, the one family that had no decoder. Given any 16-bit word: which
        family claims it, and what that family made of it. Family `0100`'s three
        subtrees are tried in the order their encodings nest — the `$4E` group
        first (a fixed top byte), then LEA/CHK and `$48`/`$4C` (bit 8 set), then
        the single-operand group (bit 8 clear) — and that ordering is stated in
        the header rather than left to be rediscovered.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `decode_suite`, 7 tests, including the property no family
        suite can check — a sweep of the **entire** 16-bit space asserting every
        word classifies, and that the wholesale families (`1010`, `1111`,
        `0110`) claim all 4096 of their words.*
        Measured coverage across all 65536 encodings: arith 29.0%, move 16.3%,
        immediate 5.3%, quick/branch/shift/coproc/lineA 6.2% each, moveq 3.1%,
        misc 2.9%, single 1.9%, control 0.3% — **89.9% claimed, 10.1%
        illegal**. Neither number should move much without a reason; a
        dispatcher that claimed everything would be as wrong as one claiming
        nothing.
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
  - [x] **Writes through the same path.** The asymmetry with reads *is* the
        content: a read can be answered from the cache alone, and a write never
        can, because the data cache is writethrough — "the data is written both
        to the cache and to external memory" — so an external cycle always
        happens and the MMU is therefore always consulted. That is also what
        makes write protection work at all: if a write could be answered from
        the cache, a write-protected page already resident would be writable.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `access_suite`, 5 further tests (12 total) — a write to a
        fully warm address still consulting the MMU where the read before it did
        not; the first write paying a search while a second *read* to the same
        page pays nothing; the price paid once rather than per write; a write
        hit updating the cached value so a later read sees it; and a transparent
        write skipping the tables.*
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
  - [~] **The instruction step** (`src/core/cpu/m68030/ap_m68030_step.c`):
        fetch through the pipe and instruction cache, decode, execute, advance
        the PC, account the clocks. **A program runs.**
        Executing today: **everything in families `0000` through `1111`**
        except `BKPT`, `CAS`, `CAS2`, `CMP2`, `CHK2` and the coprocessor
        instructions other than the MMU's. Every addressing mode the part has
        resolves, full-format indexed and memory indirect included. Exceptions
        are taken *and returned from*, including bus error, address error and
        the line 1010 and 1111 emulator traps.

        (This paragraph read "`NOP`, `MOVEQ`, and the 8-bit forms of `BRA` and
        `Bcc`" until it was corrected. That was true when the item was written
        and had been false for a long time — the tables below were updated
        commit by commit and the prose describing them was not, which is the
        same rot found in `PROJECT_STATUS.md`'s "no CPU" line.)
        **Unimplemented is a distinct outcome from illegal**, and that is what
        lets this ship incomplete. Silently doing nothing would make a program
        appear to run while producing wrong results *and* a wrong clock count;
        reporting illegal would be a lie about the hardware and would send a
        probe down an exception path the real machine never takes. So an
        unimplemented instruction stops the step, says so, and leaves the PC
        where it was — "how far did this program get" is then a real measure.
        `-Wswitch-enum` keeps it honest: every decoded kind is listed
        explicitly, so adding a family to the decoder forces a decision here
        rather than letting a `default` make it silently.
        *Verification: `step_suite`, **178 tests** — including `MOVEQ` sign-extending `$FF` to
        −1 and setting exactly the documented condition codes (with X asserted
        to *survive*), a four-instruction program running to its end, `BRA`
        landing on its target and the instruction there being the expected one,
        a conditional branch reading the flags the previous instruction set —
        the first interaction between two instructions — an unimplemented
        instruction reported rather than skipped, an illegal encoding distinct
        from it, and a second pass over the same code costing **zero** clocks
        because the instruction cache answers.*

        **What stops it being `[x]`**, and why each is a *reason* rather than an
        omission:
        - `BKPT` **now executes**, running its breakpoint acknowledge cycle in
          **CPU space** per §7.4.2. The breakpoint number rides on **A2-A4**,
          not on the low address lines: putting it elsewhere acknowledges a
          different breakpoint, and external hardware answers with the wrong
          instruction word rather than faulting.
          On a DN3500 nothing decodes CPU space, so the cycle takes a bus error
          and "the processor takes an illegal instruction exception" -- the
          machine's behaviour, not an error path, which is why declining was the
          wrong report.
          *Verification: `step_suite`, 2 further tests (191 total) -- the
          illegal instruction taken when nothing answers, and the acknowledge
          cycle's address and function code checked directly, since a wrong
          breakpoint number is a legal address that no fault would reveal. The
          harness records the **first** CPU-space cycle, because the vector
          fetch that follows would otherwise be the one pinned.*
        - `CAS` **now executes**, and the bus asserts `RMC` across the pair.
          The signal lives on the access context rather than on a bus object,
          because each access creates its own cycle and the signal spans two of
          them. §7.3.6's "the burst mode is never used during read-modify-write
          cycles" is enforced at the request rather than at the acceptance, so
          `CBREQ` is never raised inside one.
          *Verification: `bus_suite`, 2 further tests (25 total) -- `RMC`
          surviving the cycle boundary inside an operation, and a burst refused
          with `CBACK` offered anyway so the refusal is not cosmetic.
          `step_suite`, 4 further tests (189 total) -- the swap on a match; the
          **write that still happens on a mismatch**, going the other way into
          the compare register, which a model that skipped the store would turn
          into a retry loop that spins forever; and the lock released on both
          outcomes, since `RMC` held past the instruction would refuse every
          later bus grant.*
        - `CAS2` still declines: two independent memory operands under one
          locked sequence is a two-address atomic this operand path cannot
          express, and running it as two `CAS` operations would be a different
          instruction with the same mnemonic.
        - `CMP2` and `CHK2` decode and have no semantics yet. **Not blocked** —
          simply not done, and the smallest remaining piece of this item.
        - The non-MMU coprocessor instructions take the line 1111 emulator trap,
          which is **correct** on a machine with no coprocessor fitted rather
          than a gap. If an FPU is ever modelled they become real work.

        **All four are now done or reasoned.** `CMP2`/`CHK2` execute, `CAS`
        executes with `RMC` asserted across its pair, `BKPT` executes its
        acknowledge cycle, and the non-MMU coprocessor instructions take the
        line 1111 trap, which is correct on a machine with no coprocessor
        fitted. `CAS2` alone declines, and for a stated reason rather than a
        pending one: two independent memory operands under one locked sequence
        is a two-address atomic this operand path cannot express.
  - [x] **Operand access** (`src/core/cpu/m68030/ap_m68030_operand.c`), the
        layer between address calculation and memory. It exists mostly to hold
        two register rules that are opposites of each other and are easy to
        swap, neither of which faults when wrong:
        **a data register write is partial** — a byte or word operation leaves
        the rest of the register alone — while **an address register write never
        is**: "the source operand is sign-extended to a long operand and the
        operation is performed on the address register using all 32 bits". So
        `MOVE.W #$FFFF,D0` leaves D0's upper half untouched and
        `MOVEA.W #$FFFF,A0` sets A0 to `$FFFFFFFF`. Applying the data rule to an
        address register leaves a stale upper half that later long operations
        silently use; applying the address rule to a data register destroys live
        data.
        *Verification: `operand_suite`, 8 tests, including the two rules applied
        to the **same** operand value side by side — the comparison that catches
        using one where the other belongs — plus an unfinished address and an
        immediate each reported as a fault rather than read as a zero that would
        look like a real operand.*
  - [x] **MOVE and MOVEA semantics**, in the addressing modes reachable without
        an extension word — register direct, `(An)`, `(An)+`, `-(An)`. The
        extension-word modes are excluded for a concrete reason rather than an
        arbitrary one: the step does not yet fetch extension words, and MOVE is
        the instruction that makes that hard, since its destination's extension
        words sit after its source's. Those modes report unimplemented;
        guessing a displacement of zero would run and be wrong.
        *Verification: `step_suite`, 7 further tests (17 total) — a word MOVE
        leaving the destination register's upper half intact against `MOVEA.W`
        sign-extending all 32 bits, the same operand size in both, which is the
        operand layer's two rules observed through running code; `MOVE` setting
        exactly the documented condition codes against `MOVEA` touching none;
        and a store-then-reload round trip through memory with the postincrement
        and predecrement side effects applied.*
  - [x] **Defect found by that round trip: the write path never wrote
        through.** `ap_m68030_access_write` translated the address and updated
        the data cache but never issued the external write cycle — so it
        documented writethrough and behaved like writeback. Every existing test
        passed, because none of them observed memory. Fixed by adding the store
        callback the cache fill already had, with a regression test that writes
        twice to a *cached* line and asserts memory saw both, at the physical
        address rather than the logical one.
  - [x] **Extension word fetching**, which unblocks every addressing mode at
        once rather than one family at a time. Extension words come from the
        *same prefetch path* as the instruction word — advancing the pipe is
        what makes stage C's word the decoded one — rather than from a separate
        read, so they cost what the manual says they cost and share the holding
        register's savings.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `step_suite`, 6 further tests (22 total) — a long
        immediate as two words high-half-first; a byte immediate taking the
        *low* half of a whole word, Table 2-3 seen in running code; a negative
        displacement source; an absolute long destination; `(xxx).W`
        sign-extending so `$8000` addresses the top of memory; and both operands
        taking their words **in order**, checked by giving them different
        displacements so a swapped read produces the wrong address.*
  - [x] **The MMU instructions**, which are how every MMU register and the ATC
        actually get driven: `PMOVE`, `PFLUSH`, `PFLUSHA`, `PLOAD` and `PTEST`.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `step_suite`, 15 further tests (139 total) — `PFLUSHA`
        across differing function codes; the mask selecting by agreement; a
        flush by address leaving the neighbour; a function code from a data
        register that is not the register's number; `PLOAD` adding where
        `PFLUSH` removes; `PTEST` level 0 resident and not; level-0-with-A
        refused; a table-search `PTEST` leaving the ATC empty and returning the
        descriptor's address; a TC round trip; one P-REGISTER field reaching two
        registers under two prefixes; an invalid root pointer faulting with the
        address already landed; an inconsistent TC landing with `E` cleared; the
        ATC surviving `PMOVEFD` and not `PMOVE`; an MMU instruction in user
        state taking a privilege violation rather than F-line; and a
        register-direct operand refused.*
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
        `CMPM`/`EXG`).
        `MULU`/`MULS` (word × word → long, the whole register), `DIVU`/`DIVS`
        (32/16, remainder in the upper word, overflow setting `V` and leaving
        the operands unchanged), `ADDX`/`SUBX` in both the register and the
        `-(An),-(An)` forms, `ABCD`/`SBCD` in both, `CMPM` and all three `EXG`
        exchanges. The extended forms share the documented "cleared if nonzero;
        unchanged otherwise" `Z`, which is what lets one `Z` describe a whole
        multi-precision value rather than just its last word.
        Division by zero now raises vector 5 through the exception machinery
        rather than declining, so the only thing left under this item is
        `ABCD`/`SBCD`'s **`N` and `V`, which every manual documents as
        undefined** -- and which real silicon sets definitely. `N` is bit 7 of
        the result and `V` is the binary overflow between the *unadjusted* and
        corrected results, from an exhaustive hardware sweep cross-checked
        against Motorola's patent US4325121. `CHK`'s undocumented `Z`, `V` and
        `C` came from the same body of work. Both are `PROVISIONAL` only
        because the sweep was on a 68000.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `alu_suite`, 3 further tests (20 total) -- the addition
        and subtraction forms shown to use different operands, and a sweep of
        the whole byte space establishing both `V` states are reachable, which
        a hardcoded `false` satisfied for years. `step_suite`, 1 further test --
        `CHK`'s `Z` taken from the register and not the bound, which is the
        plausible wrong reading.*
        *Verification: `step_suite`, 16 further tests (80 total); then probes
        against the oracle for the timing, which is data-dependent for both the
        multiplies and the divides and so is not yet modelled.*
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
        *Verification: `operand_suite`, 2 further tests (13 total) — a
        straddling long splitting into two cycles at the right addresses with
        the right halves, and an aligned byte staying one cycle of its own size;
        plus the `RTE` round trip in `step_suite`, which is the real output.*
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
        *Verification: `bounds_suite`, 9 tests — the two size encodings side by
        side on the same bit pattern; each half's unassigned value being the
        other's valid one; the static bit operations declined and a byte `CAS`
        one size field higher accepted; the extension word separating `CMP2`
        from `CHK2`; the checked register possibly being an address register;
        `CAS2` behind the immediate encoding with two extension words and no
        byte form; and each half taking the addressing mode category its operand
        needs — control for the bounds pair it only reads, memory alterable for
        the operand `CAS` swaps.*
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
        `microcode + measured operand bus + prefetch cost`, where the microcode
        is `CC − 2(r + w)` from the published `(r/p/w)` and the bus half is what
        this core measures -- so a wait state or a cache hit still moves the
        answer. `FINDINGS.md` C9's row, `ADD.B D0,(A0)`, comes to 6 warm and 7
        cold averaged over both alignments: the manual's composed figure and the
        oracle's measurement. What remains under it are two *readings* rather
        than gaps, both named below. Named here because it was missing from this plan entirely,
        which is worse than being open: the step accumulates bus and cache time
        only, so a register-to-register `ADD` costs **zero** clocks today and
        every figure the core reports is a lower bound. The core's headline
        claim is emergent timing, and this is the part of it not yet built.
        **Not to be closed by transcribing §11.6's NCC column** — that one is a
        mean of the odd- and even-aligned cases, rounded up, so a core adding it
        would reproduce an average the hardware never exhibits.
        **But the `CC` column is a different quantity, and it is the one we
        need.** `docs/references/M68030_TIMING.md` now records the distinction:
        §11.6's legend gives each entry as `total(reads/instruction-bus/writes)`,
        and a register-to-register form reads `CC 2(0/0/0)` — no operand reads,
        no writes, and no instruction bus cycles, because the instruction is in
        the cache. There is nothing in that number but microcode, and §11.3.1
        defines CC without any averaging. So the route here is the project's
        other permitted one: **a documented value with a cited page**, not a
        measurement.
        The same legend independently confirms C7, saying the published
        prefetch count "is always greater than or equal to the actual number of
        bus cycles (one bus cycle per two instruction prefetches)" — the actual
        rule being exactly our alternation.
        The route is the project's own rule: measure against the oracle, or
        take a documented value with a cited page and mark it `PROVISIONAL`.
        **The route now exists on both sides** — see `FINDINGS.md` C5. Our
        machine side-loads and steps (`ap_machine`, `ap_probe`); the oracle
        does too, with `-debug -debugger none` making `cpu.debug:step()`
        available headlessly, and Lua reading and writing memory and registers.
        Neither needs firmware, so C4's PROM problem no longer gates this.
        Both of C5's prerequisites are now **settled** — `FINDINGS.md` C6 and
        `tools/mame-oracle/steptime.lua`. Time advances in whole cycles: every
        `NOP` step is exactly 80,000,000,000 attoseconds against a 40 ns clock,
        and the script prints the raw attoseconds beside the derived count and
        flags any remainder `FRACTIONAL` rather than rounding it away. The
        side-load guard reads the probe back and refuses if it disagrees, which
        is what stops a harness aimed at the PROM's range measuring the PROM
        while reporting the probe.
        First four measurements, oracle-side: `NOP` 2, `MOVEQ` 2,
        `ADD.L D0,D1` 2, `LSR.L #1,D0` 4 clocks.
        **The first comparison is done, and it settles the shape of this item**
        — `FINDINGS.md` C7. Our bus time *alternates* 0/2 per instruction where
        the oracle is a flat constant, and the manual says ours is right:
        §11.3.3 computes the no-cache case "assuming ... one external bus cycle
        per two instruction prefetches", which is the cache holding register
        serving two instruction words per fetch, and then says the published
        figure is "the **average** of the odd-word-aligned case and the
        even-word-aligned case". The oracle's flat number is what a cycle-table
        model produces; ours exhibits the alignment dependence the manual calls
        real. Classified `oracle-wrong`, ours kept.
        **So the target for this item is not a per-instruction constant.** It is
        a microcode time added to a bus time that keeps alternating, and the
        check is that our average over both alignments matches `[030]` §11.6 —
        not that our per-instruction figure matches MAME's. That the same 0/2
        appears for `LSR.L #1,D0` as for `NOP`, where the oracle charges 4
        against 2, is precisely the execution time this item is about.
        Both halves of the comparison are now runnable and pinned:
        `apollo-headless --time-instructions` with a golden, and
        `tools/mame-oracle/steptime.lua` for the oracle.
    - [x] **Wiring the figures in is a *scheduling* problem, not an addition**,
          and the scheduling model now exists: `ap_m68030_schedule`, which is
          `max(microcode, bus)` and is applied to every transcribed form.
          The three transcribed instructions in `--time-instructions` went from
          alternating 0/2 to a **steady 2**, which is what the manual predicts:
          `MOVEQ` is `CC 2` and `NCC 2`, the same figure cached or not, because
          its two-clock fetch hides entirely under its two clocks of microcode.
          The untranscribed ones still alternate, visibly bus-only.
          **A test had to be restated rather than repaired.** One asserted that
          a second pass over cached instructions costs *zero*, which was right
          while the clock was bus time alone. It now costs the published `CC`,
          and — because `CC` equals `NCC` for `MOVEQ` — costs the *same* as the
          uncached pass. So what the cache buys there is bus cycles, not clocks,
          and the test says that instead: no further line fills, and four
          `MOVEQ`s costing 8.
          It remains a **two-resource approximation** of §11.2's eight, kept
          because it reproduces both published columns for every transcribed row
          and because the alternative is inventing structure the manual does not
          publish. The remaining work is the two-sided check across more rows:
          cold-cache totals equalling `NCC`, warm-cache totals `CC`. A row where
          they stop agreeing is where this runs out, and is a measurement worth
          having.
          *Verification: `overlap_suite`, 4 further tests (12 total) — a bus
          cycle wholly hidden costing nothing, bus time beyond the microcode
          being what costs, a 44-clock divide swallowing its fetch, and an
          untranscribed instruction remaining its bus time. Both goldens moved
          and were regenerated, byte-identical between `-O0` and `-O3`.*
    - [x] **The original addition, implemented and backed out.**
          Detail in `PROJECT_STATUS.md`.
          *Verification: per transcribed row, cold-cache total equals `NCC` and
          warm-cache total equals `CC`, both from `[030]` §11.6.*
    - [x] **The published figures, transcribed**
          (`src/core/cpu/m68030/ap_m68030_timing_table.c`). Nineteen rows from
          §11.6.8 and §11.6.9 whose instruction-cache case reads `n(0/0/0)` —
          no operand reads, no writes, and no instruction bus cycles because the
          instruction is in the cache, so `n` is pure microcode. The `NCC`
          column is not transcribed at all, and neither are the rows with a
          non-zero read or write count: their `CC` includes operand bus cycles
          this core produces itself, so adding them whole would count twice.
          Detail in `PROJECT_STATUS.md`.
          *Verification: `timing_table_suite`, 7 tests — a transcription cannot
          be checked by re-reading it, so these check it against structure:
          every row satisfying the head ≤ CC rule a dropped or doubled digit
          usually breaks; the word-address-forms-cost-4 pattern that showed
          §11.3.4's example to be mislabelled, pinned so the table cannot drift
          back towards it; the seven register operations agreeing with each
          other, so one mistyped row stands out; the divides marked and **only**
          the divides, since a marker applied too widely makes every figure look
          provisional; what is not transcribed reported absent rather than
          approximated by a neighbouring row; and the figures composing through
          Equation (11-1), which is what they were transcribed for.*
    - [x] **The composition rule, built without the numbers**
          (`src/core/cpu/m68030/ap_m68030_overlap.c`). Equation (11-1) and
          §11.2's eight resources are arithmetic and vocabulary rather than
          measurement, so they land now and the figures have somewhere to
          arrive.
          Detail in `PROJECT_STATUS.md`.
          *Verification: `overlap_suite`, 8 tests — §11.3.4's **worked example**
          checked verbatim at 6 clocks, which is an external number rather than
          one of ours; the directional pairing; an instruction fully absorbed
          costing nothing; the first instruction overlapping with nothing;
          overlap being pairwise rather than cumulative across three
          instructions; and a head or tail longer than its instruction reported
          inconsistent, since such an entry was mis-transcribed.*
        *Verification: self-timing probes against the oracle, per instruction
        and per addressing mode, with `[030]` §11.6 as an independent check and
        every discrepancy classified before anything is changed.*
  - [x] **Effective address times, §11.6.1–§11.6.5**, and composing them
        through Equation (11-2). `FINDINGS.md` C9 is the reason this is now a
        named item rather than a later refinement: without it the footnoted rows
        report a component as a total, and the oracle already shows the size of
        the gap — 7 clocks against our 4 for `ADD.B D0,(A0)`.
        This is where `head` and `tail` finally earn their place. Equation
        (11-2) overlaps the effective address's tail against the operation's
        head — `CCea + [CCop - min(Hop,Tea)] + ...` — which is why both columns
        were transcribed from the start even though Equation (11-1) does not use
        them per-instruction.
        **First question, settled: the step declines.** A footnoted row is left
        at bus time alone, which `--time-instructions` shows as an *alternating*
        figure — visibly a lower bound, exactly as an instruction with no
        published figure at all reads. Reporting the component would have given
        a steady number that looks like a measurement and is short by a whole
        memory access, which every other convention in this core rules out.
        A test now asserts the decline; the one it replaced compared our total
        against `CC` and `NCC` and *passed*, because both sides were the same
        component — the blind spot C9 records, made concrete.
        **Second question, now recorded:** `max(microcode, bus)` cannot be
        extended to these rows. `docs/references/M68030_TIMING.md` works it
        through — the warm case needs a smaller answer against a smaller bus and
        the cold a larger one against a larger, which `max` cannot give from one
        microcode figure because it is monotonic in both. The refinement is one
        question per bus cycle, *is the microcode waiting on this?* — a prefetch
        is not, an operand read feeding the current operation is — giving
        `max(microcode, hideable) + blocking`. That is a two-bucket change
        rather than a rewrite, but it is not to be made before the effective
        address tables exist, so that both sides of the composition are
        published numbers rather than one published and one inferred.
    - [x] **A methodological correction the tables forced.** §11.6's assumption
          list includes "The data cache is not enabled", and the sampling
          helpers had it on. Comparing a figure measured with it on against one
          computed with it off is not like-for-like, and the difference is an
          operand read per repeat — invisible for the register forms, which is
          why a dozen rows passed the two-sided check before this surfaced. A
          published table's *assumptions* need transcribing as carefully as its
          numbers.
    - [x] **The tables themselves**
          (`src/core/cpu/m68030/ap_m68030_ea_timing.c`): §11.6.1's fetch and
          §11.6.3's calculate rows for the modes without a full-format
          extension word.
          Detail in `PROJECT_STATUS.md`.
          *Verification: `ea_timing_suite`, 8 tests — a register operand costing
          nothing either way and its head marked inapplicable; fetching never
          costing less than calculating, for every mode, which a swapped
          transcription would break everywhere at once; the two tables differing
          by exactly the operand read; the relative heads distinguished from the
          plain ones; an immediate costing by the words it occupies, with byte
          and word alike per Table 2-3; the immediate absent from the calculate
          table; and the long absolute being the one fetch row whose two columns
          differ.*
    - [x] **Composing them.** Half of the verification this item names is now
          met: **the second worked example of §11.3.4 comes to 40 clocks**,
          which is Motorola's arithmetic on Motorola's figures and the only
          published number that exercises Equation (11-2) rather than (11-1).
          Detail in `PROJECT_STATUS.md` and
          `docs/references/M68030_TIMING.md`.
          *Verification: `ea_timing_suite`, 4 further tests (12 total) — the
          40-clock example with the components fed in **as the example prints
          them** rather than from our tables, since feeding the transcription in
          would move both sides of the comparison together; a register operand
          contributing no component at all, checked on a case where a zero-cost
          one would over-count; the "2+op head" notation resolving against its
          operation; and four of our transcribed rows agreeing with the same
          example, which is a check against a different page from the one they
          were read off. `overlap_suite`, 3 further tests (15 total) — the
          no-cache case composing by addition against §11.3.3's own "2 + 7 = 9"
          and "9 + 7 = 16", and the two columns shown not to be the same
          function.*
    - [x] **Equation (11-2) is Equation (11-1) over *components*.** It reads as
          a second rule and is not one: every term is a component's cache case
          less the lesser of its own head and the previous component's tail, and
          (11-2) only adds that an instruction contributes *two* components. One
          accumulator therefore serves both, which is why
          `ap_m68030_overlap_add_component` now sits beneath
          `ap_m68030_overlap_add`.
    - [x] **The no-cache case composes by plain addition**, per §11.3.3, and
          `ap_m68030_no_cache_total` is that second rule. Kept a separate
          function deliberately: running `NCC` figures through head and tail
          would subtract an overlap the published number already excludes.
    - [x] **A per-instruction comparison against a published `NCC` is the wrong
          comparison, permanently.** §11.3.3 works an instruction that costs
          "eight clocks for even alignment and 10 clocks for odd alignment, an
          average of nine" while the *pair* it belongs to costs "16 clocks for
          both even and odd alignment". So the alignment difference moves
          between adjacent instructions rather than adding to the stream, 9 is a
          figure the hardware never exhibits, and the right unit of comparison
          is a sequence. This core already exhibits that alternation
          (`FINDINGS.md` C7).
    - [x] **Composed into the step, and `FINDINGS.md` C9 closes.**
          `total = microcode + measured operand bus + prefetch cost`, where the
          microcode is `CC − 2(r + w)` from the published `(r/p/w)` and the bus
          half is what this core measures -- so a wait state or a cache hit
          still moves the answer, which is the difference between this and a
          cycle-table model.
          Detail in `PROJECT_STATUS.md` and
          `docs/references/M68030_TIMING.md`.
          *Verification: `ADD.B D0,(A0)` at **6 warm and 7 cold averaged over
          both alignments** -- the manual's composed figure and the oracle's
          measurement -- with both 6 and 8 asserted to occur, so the average is
          not four equal numbers. `machine_suite`'s decline test became this
          one.*
    - [x] Three things the wiring forced, each found by a number moving that
          should not have: the `*` and `**` footnotes name **different tables**
          and can no longer share a flag; the exposure rule was being applied to
          rows it was derived to exclude, so applicability is now data on the
          row rather than a list in a test; and a **pipe refill is not the row's
          own prefetch**, so it is charged where it happens rather than replaced
          by a published figure derived for something else.
          *Verification: `timing_table_suite`, 16 tests, including the
          classification checked against each instruction's length and whether
          it changes flow rather than against the figures it is used with.*
    - [x] **§11.6.1's full-format rows, transcribed and selectable by the
          extension word** -- sixteen entries covering every combination of base
          and outer displacement, which is the whole space a full-format word
          can encode. The reading that decides between the table's two
          contradictory groups is `PROVISIONAL` with its measurement named.
          Detail in `PROJECT_STATUS.md` and
          `docs/references/M68030_TIMING.md`.
          *Verification: `ea_timing_suite`, 7 further tests (19 total) -- every
          combination resolving to a consistent row; the reading isolated in a
          test of its own rather than buried in a sweep, with a long base
          displacement as the control that a transcription making *every*
          displacement free would fail; an indirection costing a second read at
          every base displacement; the three indirection kinds sharing their
          figures, which the table's own note about Xn says from the other side;
          and the worked example's `fea ([B])` now reachable by lookup, so that
          test's hand-supplied inputs and this table agree from two different
          pages.*
    - [x] **§11.6.3's full-format rows too, and they confirm the reading on a
          second table.** Every group A row equals its group B row with the base
          displacement dropped in the calculate table exactly as in the fetch
          one, so the pattern holds over sixteen rows across two independently
          typeset tables with no counterexample. The head column corroborates it
          in a way the fetch table cannot: there the group A rows carry a plain
          head where `(B)` carries "6+op head", so the groups differ in *kind*
          and not only in value.
          *Verification: `ea_timing_suite`, 3 further tests (22 total) -- the
          two tables' patterns asserted together rather than separately;
          calculating reading one fewer than fetching at every row, which is
          §11.6.3's "fetch time ... only for the first level of indirection";
          and the head kinds told apart.*
    - [x] Four more `p` counts corrected in the calculate table, the same defect
          the fetch table had and found the same way -- by reading the page
          rather than a text extraction of it.
    - [x] **§11.6.2, Fetch Immediate Effective Address, transcribed and wired**
          -- so the `**` rows are priced rather than declined. It is keyed by the
          immediate's size *and* the destination mode together, because one
          entry covers both halves, which is exactly why such a row could never
          have been priced off §11.6.1.
          *Verification: `ea_timing_suite`, 4 further tests (26 total) --
          including that the two size columns are **not** a scaling of each
          other, `(An)` differing by one clock and `(An)+` by two, so a model
          scaling one column by operand size would be wrong in both directions;
          a long immediate never costing less than a word one, which a swapped
          column would break everywhere at once; the absent `An` destination row
          reported absent, the table agreeing with the opcode map rather than
          having a gap; and the `%` relative heads distinguished from the plain
          ones.*
    - [x] **Every row is now priced; none declines.** The change-of-flow rows
          fell to §11.3.3's page: the target's alignment decides *where* the
          refill reads, not how many bus cycles it takes -- a three-deep pipe
          wants two either way -- so nothing is averaged and the published
          difference is the exposure. The three-word rows fell to the same
          arithmetic as a single word, the two alignments differing by one
          fetch, so the larger case is twice the published average and the
          smaller is free.
          Detail in `docs/references/M68030_TIMING.md`.
          *Verification: `timing_table_suite` asserts **zero** rows classified
          unknown, so a row added without a class decision fails rather than
          being priced by whichever rule sits first.*
    - [ ] Open, and both are readings rather than gaps: the `PROVISIONAL`
          reading that selects between §11.6.1's and §11.6.3's two row groups,
          whose measurement is named in `PROJECT_STATUS.md`; and the one-clock
          bound that §11.3.3's "rounded up" leaves on a published difference of
          1, which the pair cannot separate.
  - [x] **The termination *kind* now comes from a device.** `machine_fill` and
        `machine_store` ask the board and answer `BERR` when nothing decodes the
        address, `STERM` when something does — so a bus error is a device
        declining to answer rather than a test asserting one. That is what made
        the boot PROM's 129 self-test faults, the AT bus empty-slot reads and
        the display probe all behave as the hardware does.
  - [ ] Still open: the termination's **arrival clock**. `STERM` is answered at
        a fixed two-clock minimum regardless of which device replied, so a slow
        device cannot yet lengthen a cycle. Until it can, contention is emergent
        in *who* holds the bus but not in *how long* they hold it, and no
        measured timing figure can come from a device's own speed.
        - Belongs with Phase 3's single arbitration point, as the original item
          said. Splitting it here because half of it is done and a wholly-open
          item hides that.
- [~] Exceptions, traps, interrupt priority, bus/address error stack frames.
      *Verification: probes that deliberately fault, diffed against oracle.*
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
        runs a breakpoint acknowledge cycle this step does not issue — and is
        now the suite's unimplemented-instruction placeholder. The single-operand
        escapes also gained their sizes in the decoder. Semantics that would
        otherwise be silently wrong are in `PROJECT_STATUS.md`.
        *Verification: `step_suite`, 14 further tests (111 total) — `LEA` against
        `MOVEA` on the same operand; the predecrement mask reversal seen in
        memory; a save/restore round trip; a word `MOVEM` reaching the whole
        register; `CHK` inside, above and below its bound with the negative case
        distinguished by `N`; `TAS` reporting a free semaphore and taking it;
        `MOVE from SR` trapping in user state while `MOVE from CCR` does not;
        `MOVE to CCR` unable to reach the system byte; `ILLEGAL` taking vector 4
        with its own address stacked; `NBCD` in both complements; and the three
        `EXT` forms reaching different distances from the same source byte.*
  - [x] **The `$4E` control group executes in full**: `JSR`, `JMP`, `BSR`,
        `RTS`, `RTR`, `RTD`, `RTE`, `LINK`, `UNLK`, `TRAP`, `TRAPV`, both
        directions of `MOVE USP` and `MOVEC`, `STOP` and `RESET`, with the
        privileged ones raising a privilege violation in user state. `MOVEC`'s
        sparse register codes, `STOP`'s ordering and `RESET`'s
        nothing-happens-inside semantics are in `PROJECT_STATUS.md`.
  - [x] **The wider branch displacements**: `BRA`/`Bcc`/`BSR` at 16 and 32 bits,
        all three sizes sharing one base, "the instruction address plus two".
        Detail in `PROJECT_STATUS.md`.
        *Verification: `step_suite`, 8 further tests (119 total) — a `MOVEC`
        round trip through `VBR`, `$800` leaving `$002` alone, an undefined code
        raising vector 4, `MOVEC` privileged and not taking effect when it
        traps, `STOP` masking interrupts and then not fetching at all, `RESET`
        leaving the registers alone and continuing, a word branch landing where
        a byte one would, an untaken word branch skipping its displacement, and
        a long `BSR` pushing the address after **both** displacement words.*
        `RTE` is the counterpart of taking an exception, and the throwaway frame
        makes it a *loop* rather than a special case: "the processor reads the
        status register value from the frame, increments the active stack
        pointer by eight, updates the status register ... and then begins RTE
        processing again", on whichever stack the restored S and M bits now
        select, and the frame it finds "may be any format (even another
        throwaway frame)". An undefined format is a format error, vector 14.
        `RTR` restores **only** the condition codes — "The supervisor portion of
        the status register is unaffected" — because `RTR` is unprivileged and
        restoring the system byte would make it an instruction any user program
        could use to enter supervisor state.
        **`BSR`'s condition field is `F`**, the encoding that means *never* for
        a `Bcc`. Testing the condition without excluding it pushes a return
        address and then falls through, so every subroutine call leaks a stack
        word and returns to the wrong place. Found while wiring `BSR` to the
        stack this item provided.
        `MOVE An,USP` writes the USP *directly* rather than through A7: it only
        executes in supervisor state, where A7 names the ISP or MSP, so going
        through A7 would move the wrong stack and leave the one being set up
        untouched.
        *Verification: `step_suite`, 8 further tests (97 total) — a `BSR`/`RTS`
        round trip proving the return address is the instruction after the call;
        `JMP` going to the address rather than to its contents, which for a jump
        table is one indirection too many and lands somewhere plausible;
        `LINK`/`UNLK` as exact inverses; a `TRAP` taken and returned from with
        the privilege level restored and the user stack untouched; `RTR`
        declining to restore a stacked S bit; a privileged instruction in user
        state stacking **its own** address; an undefined frame format becoming a
        format error; and `TRAPV` in both directions, since a model that always
        trapped would pass a test that only set V.*
  - [~] **Taking an exception**: stacking the frame, fetching the vector through
        the VBR, and loading the PC (`ap_m68030_take_exception` in
        `ap_m68030_step.c`). Needed an instruction unit and a memory system, so
        it landed with them rather than here — the same split as the caches,
        where structure and cost were separable.
        §8.1's four steps, and their **order** is the whole of the difficulty:
        the status register is copied *before* S is set, and it is the copy that
        is stacked. Stacking the modified one survives casual testing — the
        handler runs, RTE returns — but returns to a user program with S still
        set. Nothing faults; the privilege boundary is simply gone. The frame
        goes on the *active* supervisor stack, read after S is set, so a
        user-state exception builds on the ISP and leaves the USP alone.
        Which frame comes from Table 8-6, transcribed into
        `ap_m68030_frame_for_vector`: the six-word frame's extra long word is
        "the address of the instruction that caused the exception", distinct
        from the stacked PC, which points at the *next* one. A six-word
        exception given a four-word frame leaves RTE reading a vector offset out
        of an address.
        Wired in so far: **divide by zero** (the tail of the instruction
        semantics item above, now closed) and **TRAP #N**, whose four-bit field
        is an index into Table 8-1's trap range and not a vector number.
        **Interrupts land here too**, and they differ from every other exception
        in three ways. The status register copy is taken **before** the priority
        mask is raised — stacking the raised mask instead leaves the interrupted
        code running at the handler's priority for ever after, never receiving
        another interrupt at its own level, and nothing faults. The vector comes
        off the bus through an acknowledge cycle, with `AVEC` selecting an
        autovector and a bus error during the cycle meaning *spurious* rather
        than a fault — which is what keeps a machine with a misbehaving device
        running. And "If the M bit of the status register is set, the processor
        clears the M bit and creates a throwaway exception stack frame on top of
        the interrupt stack": one interrupt, **two frames on two different
        stacks**, and clearing M first is what moves A7 between them, so the
        order is not an implementation detail.
        Level 7 is recognised on the *transition* to 7, not on the level, so
        holding the line there does not re-interrupt — a model that read the
        level alone would never let the handler make progress. An interrupt also
        ends a `STOP`, which is what `STOP` was waiting for.
        **Reset now performs §8.1.1's ten steps** and is no longer declined.
        Four of them are the ones a plausible implementation drops, none of
        which faults when missed: "setting the supervisor bit **and clearing the
        master bit**"; the vector base register zeroed; the caches' freeze and
        **write-allocate** bits cleared along with their enables; and
        translation disabled in the TC *and* in both transparent registers.
        Two explicit negatives are as load-bearing as the steps -- reset "does
        **not** flush the address translation cache (ATC), nor does it save the
        value of either the program counter or the status register" -- so there
        is no frame, and an ATC entry must survive. A model that flushed it
        would be tidier and wrong.
        *Verification: `step_suite`, 2 further tests (193 total) -- the ten
        steps checked as a whole from a processor deliberately left in the
        state each one has to undo, with an ATC entry asserted to survive; and
        nothing stacked, checked by counting stores rather than by inspecting
        the stack pointer alone.*
        The bus and address error frames (`$A`/`$B`) build and return; the
        coprocessor mid-instruction frame stays declined, and correctly so --
        only a coprocessor generates it, and this machine has none until the
        68882 lands in Phase 2b. `CHK`, `TRAPV`,
        `TRAP`, `TRAPV`, `CHK`, the privilege violations, the illegal
        instruction word, the MMU configuration errors, the interrupts and
        **trace** are all wired in — every exception this model can build a
        frame for.
        Trace's rule is an ordering one: "The state of these bits when an
        instruction begins execution determines whether the instruction
        generates a trace exception after the instruction completes." So the
        mode is captured *before* the instruction runs, and an instruction that
        turns tracing off still traces — which is what lets a debugger step
        through the line that disables it. The stacked status register is the
        one that instruction left behind, so tracing is already off in the
        frame; the trace happened anyway.
        The change-of-flow mode counts **status register manipulations** as
        changes of flow, for a hardware reason rather than a logical one:
        "the processor must re-prefetch instruction words to fill the pipe again
        any time an instruction that can modify the status register is
        executed". A model tracing only actual branches would silently skip
        every `MOVE to SR` a debugger asked to see.
        An illegal or unimplemented instruction is **not** traced, "since it is
        not executed" — the distinction an emulation routine depends on, since
        it must raise the trace itself and would otherwise trace twice. And "if
        an instruction forces an exception as part of its normal execution, the
        forced exception processing occurs before the trace exception is
        processed", so a traced `TRAP` stacks *both*, trace on top, and the
        trace handler runs first and returns into the trap's.
        **Table 8-6's bracketed column is a second fact, separate from the frame
        size**: the stacked PC is the *next* instruction for an interrupt, a
        `TRAP` and everything in the six-word row, but the *faulting*
        instruction for illegal instruction, A-line, F-line, privilege violation
        ("First word of instruction causing Privilege Violation") and format
        error ("RTE or cpRESTORE instruction"). Defaulting to "next" has a
        privilege-violation handler emulate the instruction and return past it,
        and a format-error handler return past the `RTE` it was called to
        diagnose. Both run; neither faults.
        *Verification: `step_suite`, 10 further tests (89 total) — the stacked
        SR being the pre-change copy, tracing turned off, the frame on the
        supervisor stack with the USP untouched, the vector fetched through the
        VBR at offset rather than number, the six-word frame carrying two
        different addresses, the unbuildable frames declined with the stack
        pointer unmoved, and the handler's first instruction actually executing
        next; `exception_suite`, 2 further tests (16 total) sweeping Table 8-6's
        rows including every TRAP and every autovector. Then probes that
        deliberately fault, diffed against the oracle, which is what this item
        always asked for.
- [~] 68030 on-chip MMU: translation tables, ATC, transparent translation,
      `MMUSR`. *Verification: probe walks and faults; oracle diff.*
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
  - [~] **`PROVISIONAL`: the ATC replacement algorithm**, now narrower than it
        was. `[030]` §9.4 names it and its ingredients — "a pseudo least
        recently used algorithm ... a validity bit and an internal history bit"
        — but never states the rule.
        **The sibling manual closes one half.** `MC68851 PMMU User's Manual`
        §5.2.1.3, describing the compatible ATC, says the second bit is "a
        history bit to indicate that the entry has been recently **used**" —
        which the 68030's own text never says. Our implementation set it only on
        *insert*, so "recently used" meant "recently loaded": an entry
        translated a thousand times but never reloaded was evicted as though
        untouched, the opposite of what a least-recently-used policy is for.
        A translating hit now marks the entry, through
        `ap_m68030_atc_mark_used` rather than inside the lookup — because a
        lookup is also how `PTEST` probes, and "The PTEST instruction does not
        alter the ATC". Putting it at the call site is what keeps a diagnostic
        instruction from perturbing the state it exists to report.
        **The sibling manual also documents the *order*.** §5.2.1.3 states the
        algorithm outright -- "locate an invalid entry and use it. If no invalid
        entries are found, use a psuedo least-recently-used (LRU) algorithm to
        select an entry ... and replace that entry" -- so the two steps this
        core performs are transcribed rather than inferred. The 68851's L bit
        has no counterpart here, the 68030 being unable to lock an entry, so
        that clause drops out rather than being modelled as always false.
        **What remains PROVISIONAL** is only which entry is chosen among those
        whose history bit is clear. That is genuinely unstated in both manuals,
        and is a tie-break rather than an algorithm.
        *Verification: `atc_suite`, 3 further tests (20 total) — a hit marking
        through the explicit call and *not* through the lookup alone; marking a
        miss touching nothing; and a repeatedly hit entry surviving a sweep that
        evicts idle ones, which is the property the bit exists for and which was
        absent while only inserts marked. Remaining: measure eviction order
        against the oracle for the last undocumented half.*
  - [x] **MMU status register (`MMUSR`)**
        (`src/core/cpu/m68030/ap_m68030_mmusr.c`), `[030]` §9.7.4 pp. 9-59 f.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `mmusr_suite`, 16 tests — every field packing to its own
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
        `ap_m68030_desc`'s rules, and fill the ATC. This is the piece that joins
        the four MMU modules together, and the first one whose *timing* is
        interesting, since it is where the bus cycles are. The manual states ATC
        translation "is always completely overlapped by other operations; thus,
        no performance penalty is associated with ATC searches" — so an ATC hit
        must cost nothing in our timing, and a *miss* is where the table walk's
        bus cycles appear.
    - [x] **The table search** (`src/core/cpu/m68030/ap_m68030_walk.c`),
          `[030]` §9.2 and §9.5. Splits the logical address with
          `ap_m68030_tc`, walks the tree, applies `ap_m68030_desc`'s rules to
          each descriptor, and reports `descriptor_fetches` — the quantity a
          timing probe measures, and the reason a three-level tree costs more
          than an early-terminating one.
          *Verification: `walk_suite`, 13 tests — one fetch per level, an
          invalid descriptor stopping the search where it is found, early
          termination both shortening the search and taking the unconsumed
          index bits as offset, an indirect descriptor costing its extra fetch
          and being rejected unless it points at a page descriptor, WP/S/CI
          accumulating down the tree, a limit violation aborting *before* the
          fetch it would have indexed (0 fetches, not 1), a bus error ending
          the search, and the 8-byte stride of a long-format table.*
    - [x] **Descriptor writeback: the U and M bits.** "During a table search,
          the U bit in each descriptor that is encountered is checked and set if
          it is not already set", and M is set for a write access under
          `ap_m68030_desc`'s rule. Each update is a bus *write*, so this is
          timing rather than bookkeeping: `[030]` §11 p. 11-56 counts a table
          search in reads and writes separately and states that "an RMC cycle to
          set the U bit is counted as one read and one write". The update is
          expressed as "set U / set M at this address" rather than as a long
          word, for the same reason the fetch returns a decoded descriptor —
          the status bit positions are deferred, not guessed.
          *Verification: `walk_suite`, 13 further tests — one write per
          descriptor with U clear and none when it is already set, a write
          access costing exactly one more than a read of the same tree, an
          already-modified page costing nothing, WP and a supervisor violation
          each suppressing M, the read half of a read-modify-write still
          setting it, an invalid descriptor getting no write at all, the M bit
          landing on an indirect descriptor's target rather than on the
          indirect itself, and a bus error on the write half setting B.*
    - [x] Corroborated against the `MC68851 PMMU User's Manual 3ed` §5.1.5.3.11,
          whose update table is explicit where `[030]` is prose — U clear with M
          unchanged is an RMW, U and M together are a *single* operation, U
          already set with M clear on a write is one write, and both already set
          is no write at all. Our one-write-per-descriptor cost model matches it
          row for row. Worth recording because it is independent of the 68030
          manual and pins the case that is easiest to get wrong: setting two
          bits in one descriptor must not cost two cycles.
    - [~] **One open reading, and every source is now exhausted: when a
          supervisor violation suppresses the U update.** `[030]` says the U bit
          is set "except *after* a supervisor violation is detected" without
          saying whether a descriptor whose *own* S bit causes the violation
          still gets its own U set. We fold that descriptor's S in first, so it
          does not.
          - The **68851 manual** repeats the sentence about a pointer being
            fetched for an address denied at *another* level and drops the
            supervisor clause entirely, so it does not arbitrate.
          - The **oracle has no opinion**: `FINDINGS.md` C2 found
            `update_descriptor()` never consults supervisor state at all, so it
            cannot distinguish the two readings -- it implements neither.
          - The **web** has nothing: the question does not appear in the
            circulating 68030 MMU write-ups, which cover descriptor formats and
            `PTEST` and stop short of the history-bit gating.
          - So this needs **real hardware or a Motorola erratum**, and is marked
            `[~]` rather than `[ ]` because the reading is made, documented and
            consistent with the manual's other sentence -- what is outstanding
            is confirmation, not a decision.
    - [x] **Fill the ATC from a completed search**
          (`ap_m68030_walk_fill_atc`), so a miss populates the entry a hit then
          serves for free. This is the join between `ap_m68030_walk` and
          `ap_m68030_atc`, and the point at which the "hit costs nothing, miss
          costs the search" claim becomes measurable end to end.
          Detail in `PROJECT_STATUS.md`.
          *Verification: `walk_suite`, 7 further tests — a filled entry turning
          the next access into a hit at the right physical address, WP and CI
          reaching the entry, each of the three fault kinds cached as a faulting
          entry (including a supervisor violation, where the search itself
          succeeded and only the access was illegal), and the end-to-end form of
          the §9.4 timing rule: a read fills M clear, so a later write is a hit
          that still forces a search, and refilling after that search makes the
          write free.*
    - [x] **Descriptor status bit positions: derived, and labelled as derived.**
          Detail in `PROJECT_STATUS.md`.
          *Verification: `walk_suite`, 7 further tests — short table and page
          descriptors decoding their different address widths and status sets, a
          table descriptor not decoding CI or M (page-descriptor fields whose bit
          positions fall inside its ADDRESS), an indirect descriptor having no
          status bits at all and so costing no history write, long-format LIMIT
          as fifteen bits under the L/U flag, and a three-level tree built from
          real memory words walking to the same physical address as the
          hand-built one.*
    - [x] **Confirmed against the oracle** (`FINDINGS.md` campaign C1). Every
          derived position matches `m68kmmu.h`'s field constants field for
          field — DT, WP, U, M, CI, S, and both address masks — which is a sixth
          source independent of the five that produced the derivation. Two
          behaviours derived from prose are confirmed as code besides: U and M
          are written in a *single* store, and no history bit is written to an
          invalid descriptor.
    - [x] **Divergence found and classified `oracle-wrong`** (C2): `[030]`
          §9.5.1.1 gates the U update on "except after a supervisor violation is
          detected" and the M update on "or a supervisor violation", and the
          oracle implements neither — `update_descriptor()` never consults
          supervisor state. We keep ours, since the manual states the condition
          twice for two separate bits. It changes history bits only on a search
          that already faults, but that is exactly a supervisor-only tree probed
          from user state.
    - Cross-reference: the supervisor-violation U-bit reading above is the
      same question, recorded once there rather than twice.
- [x] 68030 on-chip instruction and data caches, and their effect on bus timing.
      *Verification: hit-vs-miss measured through the bus state machine — a hit
      costs 0 clocks, a burst line fill 5, a non-bursting miss 2. A probe
      against the oracle remains worthwhile and is listed under the probe suite,
      but the claim no longer rests on one.*
  - [x] **Cache structure and policy**
        (`src/core/cpu/m68030/ap_m68030_cache.c`), `[030]` §6. Both caches are
        "256-byte direct-mapped ... organized as 16 lines. Each line consists of
        four entries", with the tag holding **a valid bit per entry**, not per
        line — "each entry is independently replaceable". That one sentence
        shapes the module: per-line validity would make a burst fill and a
        single-entry fill indistinguishable, and those cost very different
        numbers of bus cycles. It would also make `CEI`/`CED` unimplementable.
        Detail in `PROJECT_STATUS.md`.
        *Verification: `cache_suite`, 19 tests — the A7-A4/A3-A2 split, the
        function code distinguishing otherwise identical addresses, a
        single-entry fill leaving its three neighbours invalid against a burst
        validating all four, a tag change invalidating the rest of the line,
        `CEI` clearing exactly one entry, and each `CACR` field packing to its
        own documented bit.*
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
        together, with each missing on its own leaving an ordinary cycle (and a
        DSACK port getting its three-clock cycle, since burst runs "only from
        32-bit ports that terminate bus cycles with STERM"); CBREQ negated after
        the third long word, "indicating that the MC68030 only requests one more";
        a clock without STERM being a wait state that does not advance the burst;
        and a bus error ending the fill short.*
  - [x] **What a miss costs, end to end** (`ap_m68030_cache_read`). The join the
        item was really about: a hit costs **no external bus cycle at all** —
        "Whenever a read access occurs and the required instruction word or data
        operand is resident in the appropriate on-chip cache (no external bus
        cycle is required), the MMU is completely ignored" — and a miss costs
        whatever the bus charges. Same split as the MMU: `ap_m68030_atc` holds
        the cache and `ap_m68030_walk` spends the time; `ap_m68030_cache` holds
        the lines and this spends it.
        *Verification: `cache_suite`, 6 further tests — a miss costing 5 clocks
        followed by a hit costing 0 and not asking memory again, one burst fill
        serving all four long words of the line, a device without CBACK costing
        2 clocks and filling one entry, a disabled cache paying for every access
        (which is what `MD`'s `IC` toggle exposes on real hardware), a frozen
        cache fetching but keeping nothing, and a bus error caching nothing so a
        fault cannot become a cached value.*
  - Superseded note, kept because the reasoning was the useful part: §7.3.7:
        burst runs only "from 32-bit ports that terminate bus cycles with STERM
        and respond to CBREQ by asserting CBACK", after which the processor
        "continues to accept data on every clock during which STERM is
        asserted" — so a line is 2 clocks for the first long word and 1 for each
        of the next three, against 8 for four separate synchronous reads. That
        ratio is the whole point of modelling the caches for timing.
        *Verification: a burst line fill costing 5 clocks against 8 for four
        single reads, counted through the bus state machine rather than
        asserted.*
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
      numbers. *Verification: two rows in `PROJECT_STATUS.md`'s PROVISIONAL
      table -- the four divides that **are** transcribed, at §11.6.8's published
      maxima with its own footnote quoted, and the `+` rows that are **not**
      (the multiplies, `CMP2`, `CHK2`, `CHK` with the exception taken, `CAS2`),
      which are absent from the table and therefore unpriced rather than priced
      wrongly. Plus `timing_table_suite`, which asserts that exactly four rows
      carry the marker -- a marker applied too widely would make every figure
      look provisional and none of them actionable.*

## Phase 2b — The rest of the CPU family

Split out of Phase 2. Each is a subsystem in its own right rather than a tail of
the 68030, and none is on the DN3500's critical path: the DN3500 is a 68030 with
a 68882, and the 68882 is the only one of these it has.

- [~] 68882 FPU. *Verification: probe suite over each operation and rounding
      mode; note the oracle's admitted FPU gaps as a divergence class.*
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
  - [ ] The extended, single and double formats and the conversions between
        them; the arithmetic; the transcendentals; and the coprocessor
        interface that reaches all of it.
- [ ] 68020 subset: no on-chip MMU or cache differences, external 68851.
      *Verification: `dn3000` boots under both; oracle diff.*
- [ ] 68851 external PMMU as its own subsystem. *Verification: `MC68851 PMMU
      User's Manual 3ed` cited per figure; oracle diff.*
- [ ] 68040 for DN5500: different pipeline, caches, and MMU descriptor format;
      integrated FPU. *Verification: `MC68040 User's Manual 1993` cited;
      `dn5500` oracle diff, expecting to exceed the oracle's FPU coverage.*

## Phase 3 — Core board

- [ ] Memory bus with one shared arbitration point, so contention is emergent.
      *Verification: probes measuring contention between CPU and DMA.*
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
  - [ ] Remaining: the two routes `008778-03` §2.4.7 gives an I/O adapter for
        *reaching* the arbiter — a DMA channel in cascade mode plus MASTER.L,
        and the Series 4000 Master Request Register. Both need transfers, and
        the register is absent from the oracle (C10).
        - The register's **absence from the oracle stands**, but its use no
          longer has to be guessed: the boot PROM executes `CLR.B $00011600` on
          every pass through its reset path, so the firmware clears the master
          request register early and repeatedly. Same evidence as the Phase 3
          core-register item, cross-referenced here because both were blocked on
          the same unknown.
        - What that does not give is the read-back value or the effect of a
          *set* — only that the firmware clears it at reset, which is what a
          bus-mastering request register would want at reset and is therefore
          consistent without confirming anything about the arbitration path.
  - [ ] The synchroniser is `PROVISIONAL` at two clocks, the published maximum
        rather than a measurement. Needs grant latency measured against the
        oracle across request phases, which needs a second master first.
- [ ] Address translation map (`0x017000`), CPU status/control, cache control,
      task alias, master request, latch-page-on-parity-error registers.
      *Verification: `008778-03` cited per register; oracle diff.*
  - [x] The address translation map itself: the translation for both DMA
        widths, the entry format, and the register file. `atmap_suite`,
        15 tests. The source that settles it is `019411-A00` §4.2.1.4, an
        addendum that *replaces* pages 4-10 and 4-11 of the handbook and is the
        only one naming the DS3500 — the base manual describes the map as a
        Series 4000 feature and would have left our reference machine without
        one.
  - [ ] Open on the map, and both are gaps in the manuals rather than in the
        code: what the region decodes to beyond the entries (`017000`-`0177FF`
        is 2 KB and 128 entries of 16 bits fill 256 bytes of it), and whether a
        byte address within it selects entry `(address - base) / 2`, which is
        assumed because it is the only reading with no gaps. Both are pinned by
        tests so they cannot be closed by accident.
        - **The oracle answers both, and disagrees with us on one.**
          `apollo_address_translation_map_r` is
          `address_translation_map[offset & 0x3ff]` on a 16-bit handler, so the
          offset is in *words*: **1024 entries spanning the whole 2 KB**, and a
          byte address selecting entry `(address - base) / 2`.
        - The second half confirms our assumption. The first **contradicts** it:
          we model 128 entries filling 256 bytes, the oracle models 1024 filling
          all 2 KB.
        - **Resolved, and our model is right.** `019411-A00` §4.2.1.4 gives
          *both* numbers, for different transfer widths: during **8-bit** DMA
          "address bits `<15:10>` provide an index ... they select one of the
          **64** entries", and during **16-bit** DMA "address bits `<16:10>` ...
          select one of the **128** entries". The map has 128 entries; 8-bit
          transfers reach only the first 64.
        - So our 128 is correct, and the sub-item above already implements "the
          translation for both DMA widths" — the two facts were in the plan the
          whole time, one line apart, describing each other.
        - The oracle's `& 0x3ff` is **over-permissive**: 1024 entries where the
          hardware has 128. Classified as hardware-truer on our side, with the
          manual as the citation. It is the kind of mask an emulator writes to
          avoid an out-of-bounds index rather than to model a decode.
        - The remaining half of the original question — what the rest of the
          2 KB region decodes to — is untouched by this. 128 entries of 16 bits
          fill 256 bytes; §4.2.1.4 says nothing about the other 1792.
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
  - [ ] **Blocked, not deferred:** task alias (`010300`) and master request
        (`011600`) are absent from the oracle — they match, exactly, the
        all-ones signature that two known-unmapped control addresses produce.
        Table 2-8 lists both, so the hardware has them and the oracle does not.
        Needs the architecture handbook, or a boot-PROM disassembly showing what
        the firmware writes there. Implementing all-ones would bake an oracle
        gap in as though it were a measurement.
        - **Partly answered, from evidence collected for something else.** The
          boot PROM executes `CLR.B $00011600` — a byte write of zero to the
          **master request register** — on every pass through its reset path.
          That is the disassembly this item asked for, and it arrived while
          chasing a stack exhaustion whose cause was the register being
          unreachable through the map (`FINDINGS.md` C34 era).
        - So the register is written, early, with zero, repeatedly. That does
          not give its read-back value and does not settle task alias
          (`010300`) at all, both of which still need the handbook or the
          oracle. But it removes the "we have no idea what touches this" part
          of the blockage for one of the two.
        - Worth noting how it was found: not by looking for it. The trace that
          named `CLR.B $00011600` was watching A7 for an unrelated defect, and
          the answer to a blocked item was in it. That is an argument for
          re-reading traces already taken before running new ones.
- [ ] Two 8259 interrupt controllers and the Apollo interrupt vector scheme.
      *Verification: probe-driven interrupt ordering vs oracle.*
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
- [ ] Two AT DMA controllers. *Verification: transfer probes; device request
      lines gate DMA at block granularity, not per word.*
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
  - [ ] Transfers. Blocked on the shared arbitration point below, and cleanly
        so: every register above is programmable and observable without a byte
        moving, which is exactly what firmware does to a controller it has not
        yet used. Rotating priority is stored and decoded but its rotation is
        not kept, because nothing can rotate it until a transfer completes.
  - [x] Both controllers wired into the board at `010C00` (stride 1) and
        `010D00` (stride 2). `dma_suite`, 6 tests. The AT convention that the
        first controller cascades onto channel 0 of the second is deliberately
        **not** asserted — the equivalent assumption about the interrupt
        controllers was wrong here (C11) — and will be measured once transfers
        exist to measure.
  - [ ] Then the shared arbitration point (Phase 3's first item), which has been
        waiting on a second bus master to exist.
- [ ] Interval timer and calendar. *Verification: self-timing probes; the
      14-day calendar interval hazard noted in the MAME driver is reproduced or
      explained.*
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
  - [~] Period and pulse-width measurement stay declined, now for a hardware
        reason rather than a transcription one: both time a signal applied to a
        timer's gate pin, and on this board the three gates have nothing
        connected. They are decoded and reported, so a caller learns which mode
        it asked for.
        - Marked `[~]` rather than `[ ]`. An unticked box reads as work
          outstanding, and this is not: it is a mode the DN3500 cannot exercise,
          declined deliberately and reported honestly. It becomes real work only
          if a model appears whose board wires a gate — which is a Phase 7
          question, not a Phase 3 omission.
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
  - [ ] Whether to recompute the time base to admit the six fast rates. The
        cost is now measured, not guessed: x64, dropping the representable span
        from 88.6 years to 505 days. Worth doing if anything is ever seen using
        them; the part's own 4.194304 MHz crystal is a separate matter and can
        never be a clock domain at all, since it would leave 3.95 days.
  - [~] The square-wave output pin (nothing on this board is wired to it) and
        the daylight-savings shifts of `DSE` (stored but inert).
        - Also `[~]`, and for the same reason as the timer's measurement modes:
          nothing on this board can observe either, so implementing them would
          add behaviour no test could distinguish from its absence.
- [ ] SIO serial lines, keyboard and mouse. *Verification: console byte stream
      identical to the oracle's.*
  - **The oracle's half of that comparison now exists.** `docs/references/MD.md`
    holds its console stream byte-exact — sign-on `0D 0A 4D 44 37 0D 0A`, prompt
    `0D 0A 0D 0A 3E`, and `A`'s address lines — captured through
    `tools/mame-oracle/mdcapture.lua`. That was written for Phase 1's MD item and
    is the verification this item asks for, arriving from the other direction.
  - Our half does not exist yet: the PROM never transmits on our core, because
    it never completes the channel B autobaud that precedes the console. So the
    remaining work for this item is the framing above, and then the comparison
    is a diff rather than a new measurement.
  - Worth stating because the two items were filed in different phases and each
    describes half of one job. Phase 1 wanted the format so a parser could be
    written; Phase 3 wants the stream so the DUART can be checked. The same
    capture serves both.
  - [x] Placement measured: both ports at `010400` and `010500`, **stride 2**,
        sixteen registers over thirty-two bytes. `FINDINGS.md` C14. The DUART
        manual is already in `docs/references/motorola/`.
  - [x] The 2681/68681's programming model: all sixteen register addresses,
        both channels with their FIFOs and mode-register pointer, the
        counter/timer with its two address-triggered commands, the interrupt
        registers, and the input and output ports. `mc68681_suite`, 15 tests.
  - [~] Serial framing — baud rates, start/stop bits, parity, and the automatic
        echo and loopback modes.
        - **Started, with the piece the console negotiation needs.**
          `ap_mc68681_receive_at` takes the rate the *sender* used and compares
          its receiver nibble against the channel's own `CSR`. A mismatch sets
          `SR[6]`, framing error, and the byte still enters the FIFO — the part
          does not discard it, and discarding would look identical to nothing
          being sent. `mc68681_suite`, 4 tests.
        - That failure is the point. The boot PROM autobauds by cycling channel
          B's clock select and waiting for a byte that decodes cleanly, so a
          model where every byte arrives intact whatever the rate would let the
          negotiation succeed at the first rate tried.
        - A disabled receiver reports no framing error: it never sampled the
          character, so it cannot have mis-sampled a stop bit. Without that, the
          flag would appear on a port nothing is listening to.
        - The rate now flows end to end: `ap_sio_receive_at` on the board, and
          `--boot-input-rate` on the frontend, defaulting to `0x77` because that
          is what the firmware configures both ports to at reset (measured off
          the oracle, `FINDINGS.md` C39). The rate-less `ap_sio_receive` remains
          for callers that mean "assume the wire agrees", which the header now
          calls a claim rather than a default.
        - **No behavioural difference is observable yet**, and this is recorded
          rather than glossed: feeding `\r` at `0x77` and at `0xBB` both leave
          the PROM at `00002542`. The framing error is set, but the firmware
          does not read the status bit at this point in its boot, so nothing
          downstream changes. The mechanism is right and the demonstration is
          not there — those are different claims.
        - **The mode registers' framing fields are decoded**: `MR1[1:0]`
          character length, `MR1[2]` parity enable, `MR1[4:3]` parity type, and
          `MR2[3:0]` stop-bit length. Names and bit positions, the same shape as
          the SSW and the display controller's mode fields, because that part is
          settleable before any of it shapes a character on a wire.
          `mc68681_suite`, 3 tests.
        - Character length is a **count**, `5 + field`, not a table index. Both
          readings give the same four answers, and only the count says why `11`
          is eight rather than nine.
        - Parity is enabled when bit 2 is **clear**. That inversion has its own
          named function and its own test, because getting it backwards yields a
          link that works until the first character with an odd number of set
          bits — one that passes a test written with `0x00` and fails in
          service.
        - The stop-bit field is **sixteen encodings** from 0.5 to 2 stop bits,
          not a one-or-two flag. Only the two common lengths are named and the
          rest survive as their own code, because a driver that programmed 1.5
          stop bits meant it.
        - **Applied to a character**: a received byte now arrives with only as
          many bits as the link carries, so a seven-bit port never sees an
          eighth. That is the absence of a signal rather than truncation of a
          value — the bit was not transmitted — which is why a seven-bit console
          shows `A` for both `41` and `C1` and reports no error doing it.
        - It immediately caught an assumption in an existing test. `MR1` resets
          to `00`, which is a **five-bit** link, so an unprogrammed port
          delivers `41` as `01`. A rate test had been asserting `41` came back
          intact while quietly depending on framing not being modelled; it now
          programs the eight bits it always meant.
        - **The channel modes are decoded and local loopback works.**
          `MR2[7:6]` gives normal, auto-echo, local loopback and remote
          loopback. In local loopback a transmitted character returns on the
          same channel, **framed by that channel's own settings** — a self-test
          that bypassed framing would be checking the FIFO rather than the link.
        - And it must *not* also reach the pin. A model that both looped back
          and transmitted would let a self-test pass while the outside world saw
          traffic it should never have seen, so there is a test that nothing is
          transmitted outward — and a control that normal mode still does, since
          a model that never transmitted would satisfy the first two.
          `mc68681_suite`, 4 tests.
        - **All four channel modes now act.** Auto-echo retransmits *and*
          delivers — a terminal sees its own typing echoed by the part rather
          than by software. Remote loopback retransmits and does **not**
          deliver: the channel is a mirror for someone else's test, and a local
          program must not see traffic never addressed to it. Delivering in both
          would make remote loopback indistinguishable from auto-echo, which is
          the one thing separating them. `mc68681_suite`, 3 more tests with a
          normal-mode control that does neither.
        - **Parity is checked.** `ap_mc68681_receive_framed` takes the sender's
          `MR1` as well as its rate and sets `SR[5]` when the two disagree.
          Compared as **enable and type together**: two ports both using parity
          but differing on odd against even get a wrong bit on roughly half of
          all characters, which is a link that works *intermittently* — worse
          than one that never works, and invisible to a test that sends one
          character.
        - A receiver not using parity reports none, whatever the sender did. It
          cannot find a bit it is not looking for, and without that rule a
          no-parity console would report errors against any sender that used
          parity — including ports the DN3500's own firmware configures.
        - Kept separate from `ap_mc68681_receive_at` rather than replacing it,
          because the two state different things: `receive_at` means "the sender
          agrees about framing, check the rate", and a caller forced to pass the
          receiver's own `MR1` to say "the same" would be asserting a fact it
          does not have.

        - `ap_sio_receive_framed` carries rate *and* parity through the board,
          so a modelled device can state its own configuration and let the DUART
          decide whether the link works. `sio_suite`, 2 tests through the
          registers a program actually writes, with a control that a correctly
          configured sender produces neither error.

    That completes the item's original list — baud rates, start and stop bits,
    parity, and the automatic echo and loopback modes — except that stop bits
    are decoded and reported rather than timed, which needs the tick loop before
    it can mean anything.

    **This unblocks the keyboard item below**, which was waiting on "the framing
    above, or a device on the other end". The framing exists now; what the
    keyboard still needs is its scan codes.
    - The PROM's table is captured as data in `FINDINGS.md` C46 — 41 bytes at
      `000021D2`, found from the `CMP.B (d8,PC,Xn)` loop that searches it. Its
      high bytes fall into triples on a fixed spacing (`CB DB FB`, `C8 D8 F8`,
      …), interleaved with ASCII runs, and several characters repeat.
    - **Settled** (`FINDINGS.md` C46): three separate keys. A scan code is
      `port × 32 + bit` across `keyboard1..4`, bit 7 marks a release, and the
      table's `4B 5B 7B` are `Numpad 1`, `F10` and one more — unrelated keys
      whose codes happen to differ only in bits 4 and 5. The triple spacing is
      matrix layout, not modifier encoding.
    - So a keyboard module reports **the key that moved**, make and break, and
      does not fold shift or control into the code. That is the whole interface,
      and it is now specified rather than inferred.
    - **Written**: `device/ap_kbd.c`. A press sends the key's index, a release
      the index with bit 7 set, and the matrix stops at `0x80` *because* bit 7
      is the flag — a key above it would have a make code indistinguishable from
      another key's break code, so the bound is refused rather than masked.
      `kbd_suite`, 5 tests.
    - A repeated press, or a release of a key never pressed, sends **nothing**.
      A real matrix scan cannot report a transition that did not happen, and
      letting one through would desynchronise the firmware's own shift state —
      which it tracks from these transitions and nothing else.
    - No timer and no auto-repeat. The real part scans on a timer and repeats
      held keys; neither is modelled, because nothing in this core advances time
      and a repeat interval would be a number with no clock behind it. What is
      modelled is the transition, which is what a caller can generate honestly.
    - **Wired**: `ap_board_key_press` and `_release` deliver the scan code to
      serial 1 channel A. `board_suite`, 2 tests — one that a press and its
      release arrive as `4B` and `CB`, one that a repeated press puts *nothing*
      on the port rather than being filtered later.
    - The framing immediately produced a real constraint. `MR1` resets to a
      **five-bit** link, so on an unconfigured port `4B` arrives as `0B` and a
      release code — bit 7 set — cannot arrive at all. **The keyboard needs
      eight bits**, and the firmware must configure them before it can read a
      key. That is a fact about the machine, found by the framing work rather
      than assumed by it.
    - The link's *rate* is assumed rather than measured: the code goes out at
      the port's own clock select, which models a correct link and makes the
      rate check vacuous on this path. The real keyboard's line rate is unknown
      — the firmware configures channel *B* in every trace we hold and leaves
      channel A at reset — so a figure here would be invented. Recorded in the
      board header.
    - `--boot-key N` presses and releases a matrix index, self-timed the way
      scripted input is: it acts once the port can take the code, because a
      fixed step number would be a guess about how long the firmware takes to
      enable its receiver and would silently do nothing if it took longer.
    - **It changes nothing yet, and the counters say why.** The PROM still stops
      at `000007AE` with 38 serial writes — configuration only. The firmware has
      not enabled channel A's *receiver* at that point, so a scan code arriving
      there is dropped by the DUART exactly as the hardware would drop it.
    - That is the same state the console poll is in, seen from the other side:
      the machine is waiting, and what it is waiting for is not a keystroke on a
      port it has not opened. Recorded rather than treated as a defect in the
      keyboard, which is now complete on its own terms.
        - **The values are no longer unknown.** `FINDINGS.md` C39 and C42 read
          them off the running oracle: `sio1 ACR E0`, `sio1 CSRB 77`,
          `sio2 ACR 80`, `sio2 CSRA 77` at reset, and the firmware then
          **autobauds** channel B by cycling `CSRB` between `77` and `BB` on
          every keyboard event until a character decodes.
        - That is the first serial configuration this project has measured
          rather than inferred, and it makes the item concrete: the clock-select
          codes to decode are `77` and `BB`, and the mode to implement is one
          where a *wrong* rate yields a character that does not decode — because
          the firmware's console depends on exactly that failing.
        - It also fixes the order. Framing must come before the keyboard, not
          with it: the keyboard is on channel A and the console negotiation
          happens on channel B, so a keyboard that delivered bytes without
          framing would let the autobaud succeed at any rate.
  - [x] Both ports wired into the board at `010400` and `010500`, stride 2,
        sharing IRQ1. §3.9's memory-refresh period is pinned at exactly 99000
        base units — a figure whose *frequency* is not an integer, so it is the
        second case (after the interval timer's prescaled rate) that a core
        counting in hertz could not represent at all. `sio_suite`, 6 tests.
  - [ ] Drive the refresh from the DUART's timer, and the keyboard from
        SIO line 0. Both need the framing above, or a device on the other end.
        - **"SIO line 0" is now precise: serial 1, channel A.** Confirmed twice
          over — MAME's `dn3500` wires `m_keyboard->tx_cb()` to
          `apollo_sio::rx_a_w` (`FINDINGS.md` C42 era), and the boot PROM's own
          poll loop reads serial 1's status register A and looks the received
          byte up in a **scan-code translation table at `000021D2`**, which this
          project has read.
        - So the keyboard's side of the wire is specified without needing the
          oracle again: it sends Apollo scan codes, not ASCII, on channel A, and
          the PROM's table is the map that decodes them. Feeding ASCII there is
          what made a carriage return fail to match while `0D` sat in the table.
        - The refresh half is unchanged and still needs the DUART's timer, which
          needs the tick loop.
- [ ] Node ID PROM (`0x011200`), including node ID taken from the logical volume
      label. *Verification: `lcnode`-visible node ID matches the configured
      value.*
  - [x] The PROM itself: layout measured, identifier big-endian in registers
        0-3, checksum in register 14 confirmed arithmetically. Stride 2 with the
        odd byte reading zero — *not* the serial ports' arrangement at the same
        stride, which reads every value twice. `nodeid_suite`, 6 tests.
  - [ ] Taking the identifier from the logical volume label, which needs media
        and a volume-label reader. The module takes it from its caller, so this
        is a source above it rather than a change to it.
  - [x] **Answered: only the identifier.** The oracle's `apollo_ni::call_load`
        computes `data[2] + data[4] + data[6]` and compares it against
        `data[30]`. Three bytes summed, not sixteen, and a **sum** rather than
        an exclusive-or.
        Detail in `PROJECT_STATUS.md`.
## Phase 4 — Storage, then a first boot

- [ ] **OMTI 8621 ESDI/floppy controller** — one controller for both, and the
      DN3500's.
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
  - [ ] The commands that move data, and §6's floppy set. They want a drive and
        a disk image behind them, and `media/` has no `.awd`.
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
- [ ] **Archive SC-499 cartridge tape.**
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
  - [ ] If an FPA is ever modelled, `F8000000-FFFFFFFF` is its space — and the
        commented-out handler is a hint that returning `FFFFFFFF` there was
        tried and not kept. Find out why before repeating it.

### The PROM now needs time to pass

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
  - [ ] The rest of those 803 writes: `CR2`/`CR3`'s remaining fields, the
        blitter's five defined modes and the colour lookup table. Verify on a
        decoded PNG, not on register round-trips.
  - [~] **The display controller is the next module**, and now for a reason
        rather than as the next thing on a list. It stops being a probe target:
        the four regions already recorded (`05D800`/`05E800` registers,
        `0FA0000`/`000A0000` graphics memory) become the machine's output.
        - Kept as the *rationale* and marked `[~]`, because it is not a separate
          piece of work. The work is the item directly above — `CR2`/`CR3`, the
          blitter's five modes, the lookup table — and the Phase 5 line that
          says the same thing. Three unticked boxes for one job inflated the
          count and split the evidence between them.
        - The Phase 5 line now carries the specification (the 803 writes a
          fitted `c8p` provokes); this line carries why it matters; the item
          above carries which registers. Cross-referenced rather than merged, so
          none of the three loses what it uniquely says.
  - [ ] The keyboard is the matching input module: serial 1 channel A takes scan
        codes, and the PROM's table at `000021D2` is the map it decodes them
        with. `--boot-input-channel A` already reaches it; what is missing is
        the scan codes themselves.
  - [~] Superseded reading, kept because it was right about the device and wrong
        about the consequence: **SIO1 is the keyboard, not a terminal.** If
        so, feeding it ASCII is the wrong thing entirely — `\n` and `\r` both
        fail to match, and `\r` is *in* the table — and the DN3500's console is
        the graphics display plus keyboard rather than a serial terminal. With
        only the display's ID register modelled, the PROM would have nowhere to
        print, which fits it never transmitting.
        - Confirm against the oracle before acting: what does MAME attach to
          each `apollo_sio`, and does its DN3500 drive a keyboard there?
        - If it holds, the console module is the **display**, not serial, and
          the graphics controller stops being a probe target and becomes the
          output device. That is a large module and the wrong one to start on a
          reading this fresh.
        - Checked: `ap_mc68681_write` does drop a transmit-buffer write when
          `tx_enabled` is clear, and the command register's enable and disable
          bits are handled correctly. So the mechanism exists; whether the
          firmware trips it is still unestablished, and the per-register counts
          are still the way to find out.
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
  - [ ] The tick loop is still owed and remains the project's central design
        item. It was **not** what the `000007AE` stop needed, and building it
        there would have been the wrong move for a plausible reason — a poll
        loop looks like a timing problem.
        - **It is now demonstrably required, by five named things rather than a
          hunch.** Each was found by building something else and hitting the
          same wall:
          1. **Stop-bit timing.** `MR2[3:0]` is decoded and reported; timing it
             needs a clock.
          2. **The DUART's counter/timer**, which the memory refresh is driven
             from — `§3.9`'s period is already pinned at 99000 base units.
          3. **The MC146818's periodic interrupt**, whose six fastest rates are
             a `PROVISIONAL` figure waiting on a time-base decision.
          4. **The bus's arrival clock.** Every device answers at a fixed
             two-clock `STERM`, so a slow device cannot lengthen a cycle and no
             timing figure can come from a device's own speed.
          5. **Keyboard auto-repeat**, deliberately unmodelled because a repeat
             interval would be a number with no clock behind it.
        - That list is the difference between "owed eventually" and "next". Each
          entry names a module already written that is incomplete *only* because
          nothing advances.
        - **Started: the machine keeps time.** `ap_machine_now` is absolute time
          since reset in `AP_TIME_BASE_HZ` units, advanced from each step's CPU
          clocks through `ap_clock_duration`. That conversion is the **only**
          place a CPU cycle becomes a time, which is what keeps the rest honest
          about its units. `machine_suite`, 3 tests.
        - `ap_machine_set_cpu_hz` **refuses** a rate the base cannot represent
          rather than rounding it. Rounding would put a machine a fraction of a
          cycle out per tick and hide it in a unit nobody reads directly — which
          is exactly why the base is derived from every clock in the machine
          instead of chosen.
        - A machine whose clock was never set produces **no time at all**, and
          that is tested. A default rate would be a figure nobody chose
          appearing in every measurement, which is the failure this project's
          `PROVISIONAL` discipline exists to prevent.
        - This is the clock, not the loop. Nothing else advances inside it yet;
          the five things above still wait. What exists is something true for
          them to advance against, rather than a number invented alongside the
          first subsystem that needed one.
        - This is the project's central design item, deferred until something
          needed it, and the firmware now does: *"one `tick()` per machine
          cycle, every subsystem advancing inside it, no batching, no event
          queues, no special cases."*
        - Time is counted in `AP_TIME_BASE_HZ` units, never CPU cycles, and
          `src/core/time/` already exists for it. The step returns clocks; the
          conversion to base units is the first thing to get right, and
          `ap_clock_init()` already refuses a frequency the base cannot
          represent.
        - Identify which device `A0` points at before building anything — the
          poll names it, and `--boot-trace` reports only A6 and A7 today.
          Extending the trace has been the cheapest move available at every
          step of this investigation.
  - [x] That run was a machine waiting correctly rather than a runaway — the PC
        stayed on one instruction and the fault count was static, unlike the
        vector-table runaway. Confirmed since: it was waiting for console input
        on a channel nothing was feeding.
  - [x] Every `ap_board_t` counter now records the **first address** as well as
        the count — read-only writes and both AT bus empty-slot directions,
        matching the unmapped pair. Applied before an investigation needed it
        rather than during one, which is the point of C33's rule.
        Detail in `PROJECT_STATUS.md`.
  - [ ] The rest of the display controllers: the blitter and the colour lookup
        table. Verify on a decoded PNG rather than on register round-trips — a
        controller that passes register tests and draws nothing is the standard
        way this goes wrong.
        - The **graphics memories are done** and no longer belong in this line:
          `0A0000-0BFFFF` colour and `FA0000-FDFFFF` monochrome decode, store
          into caller-owned buffers, and are matched *before* the AT bus windows
          they sit inside — which had been reporting the machine's own frame
          buffer as an empty expansion slot. `graphics_suite`.
        - Also done and not listed here when this was written: the device ID
          registers, `CR0`'s mode and shift fields, and `CR1`'s bits named per
          controller family. What remains is the drawing.
        - The `--screen` option fits one, and with `c8p` the firmware makes
          **803 writes** to the controller. Those writes are the specification
          for what is left: they are what the blitter and lookup table have to
          answer.
  - [x] The **special status word** and the bus fault frame layout,
        `cpu/m68030/ap_m68030_ssw.c` — Figure 8-9's bit positions, the SIZ1/SIZ0
        encoding that counts bytes *remaining* (so a long word is zero), the
        FC2-FC0 address space, and Table 8-6's field offsets for both fault
        frames. `ssw_suite`, 11 tests.
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
  - [~] The long frame's INTERNAL REGISTER fields are stacked as zero — a
        deliberate approximation, since this model has no microsequencer state
        to save. Cost to close: an `RTE` resuming a fault *mid-instruction*
        cannot work from a zeroed frame, so the rerun must reconstruct the
        access from the SSW and fault address instead of from internal state.
        - **Landed, and now recorded as the convention requires**: marked
          `PROVISIONAL` in `ap_m68030_step.c` and entered in
          `PROJECT_STATUS.md`'s `PROVISIONAL` table, alongside the second
          approximation it produced — `RTE` re-executing the faulted
          instruction from the start rather than resuming mid-instruction.
        - `[~]` rather than `[ ]`: the approximation is *made*, deliberately and
          documented in all three places. What remains open is closing it, which
          is the "cost to close" column of the table entry rather than an
          unstarted task. The item said "record it as `PROVISIONAL` when it
          lands", and this is that.
  - [x] Closed: the store callback returns `bool` now, so a write can fault, and
        `step_suite` covers a faulted write taking the short frame. It was
        recorded as unreachable rather than quietly left untested, which is what
        made it findable when the store path changed.
  - [x] The `.ct` image reader, `image/ap_ct.c`: block addressing, the
        whole-block size check, and boot-record parsing. `ct_suite`, 8 tests.
  - [x] The QIC-02 command set transcribed as far as the scan allows
        (`FINDINGS.md` C25) — ten of twelve codes legible; ERASE and SELECT Q11
        FORMAT are left blank rather than guessed, though both are constrained
        to the `2x` group.
  - [x] The drive and tape motion, `device/ap_qic.c`: the readable half of the
        command set over a `.ct` image, with writing refused and the two lost
        opcodes claimed by nothing. `qic_suite`, 12 tests.
  - [x] The drive joined to the SC-499's registers: a driver reaches it through
        `050000`, commands go via the request bit, and data comes back a byte at
        a time. `tape_suite`, 11 tests.
  - [ ] The per-byte QIC-02 handshake. §1.13.2's Figure 1-7 is now
        **transcribed** (`FINDINGS.md` C26): a five-edge REQUEST/READY exchange
        whose *ordering* is fully determined. Its timings are all **bounds, not
        values** — `T4->T5 < 500 ms` for command execution, a 20-to-100 µs
        window for the close — so implementing them means picking documented
        figures and marking them `PROVISIONAL`, as the 68030's input
        synchroniser is. The ordering alone is enough for a polling driver.
  - [x] Figure 1-6, the read data transfer, transcribed (C26): ACKNOWLEDGE and
        TRANSFER pace each byte while READY frames the block.
  - [x] Figure 1-8 transcribed and the READY/EXCEPTION defect fixed: exception
        clears ready in one call, and a command clears the exception and
        restores ready, which is Figure 1-8's own order.
  - [x] Figure 1-10, the status byte transfer, transcribed (C26): a repeating
        per-byte REQUEST/READY exchange with DIRECTION reversed, echoing a
        status *block* rather than one byte.
  - [ ] The status block's **length and contents**, which Figure 1-10 does not
        give — it shows the protocol, not the payload. `READ STATUS` currently
        succeeds and returns no bytes, which is honest about the gap but is not
        a status block. Find the field definitions before implementing; the
        conventional QIC-02 length is not a source.
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
  - [ ] Close them by measurement against a running drive. Only a driver
        watching for the edges themselves can observe the difference.
  - [ ] Figure 1-5, the write data transfer — only needed if a write-back path
        is ever added, which `ap_qic` currently refuses outright.
  - [ ] Note C25: the
        controller identifies the cartridge type from BOT-to-load-point
        *distance*, which a raw block image has no geometry to supply, so the
        drive must be told its cartridge type rather than deriving it.
        The image format is known
        (`FINDINGS.md` C24): a **raw 512-byte-block image**, 104,841 blocks for
        the boot cartridge, no wrapper to parse. Its first block is a boot
        record carrying four big-endian words, the ASCII `SYSBOOT REV` and
        `M68K`, and 68000 code.
  - [x] Wired into the board at `050000`: four registers at stride 1, the upper
        four addresses of each eight floating to `FF`, aliased through the
        range, on IRQ5. `tape_suite`, 6 tests, including the measured reset dump
        reproduced over two aliasing periods.
  - [ ] **Open:** the guide says a reset sets DONE and the measured part does
        not. Its scan lost the status register's bit numbers, so "DONE" may not
        be the bit this core calls DONE. A status read after a real transfer
        would settle which bit moves (`FINDINGS.md` C19).

      Placement from `008778-03` Table 2-9:
      `050000`-`050F80`, AT `218`-`21F`, eight registers, confirmed by an
      eight-byte aliasing period in the oracle — and `050000` **is** the
      controller, confirmed by removing the card: with `isa2` emptied it reads
      `FF` throughout, with the card present it reads `00 40 FF ...`. Note that
      the DN3500's *default* configuration already carries the tape in `isa2`
      beside the OMTI in `isa1`, which is what invalidated the first attempt at
      this comparison (`FINDINGS.md` C16). No programming model exists in
      `008778-03`, whose Chapter 8 is physical only.
      The register model is now **transcribed** from the controller's own manual
      (`FINDINGS.md` C18): `BASE+0` data/command, `BASE+1` control on write and
      status on read, `BASE+2` start DMA and `BASE+3` reset DMA, both
      write-triggered by any value. Four addresses are used, not the two the read
      sweep found — the other two are write-only, which is why they read `FF`.
      Ready is corroborated at status bit 6 by the sweep's reset value of `40`,
      which the manual's own OCR could not supply.
      The register *span* was measured first: only `050000` and `050001` read
      back, the other six addresses read `FF`. The **bit map was not**
      and cannot be got the C10 way — a bit sweep writes commands to a command
      register, the controller's state moves under the probe, and the sweep
      reported it could not restore what it found. But **the controller's own
      manual has been found**: the *Archive SC-499 Tape Controller Information
      Guide* is in `docs/references/archive/` and carries the QIC-02 command
      descriptions. It confirms the measured span independently — data/command
      register at base+0, status at base+1 — so the bit map is transcription
      rather than measurement, and a protocol probe is needed only to check it
      (`FINDINGS.md` C17).
      Promoted ahead of the disk, because it
      is the only bootable medium that exists: `media/` holds the Domain/OS
      SR10.3.5 distribution as `.ct` cartridge images including
      `CRTG_STD_SFW_BOOT_1`, and no Winchester image at all. The first boot
      therefore runs from tape and installs onto a blank disk, which reverses
      the order this phase assumed.
- [ ] Winchester and floppy media handling (`.awd` for the disk, as the oracle
      names it). Placement: `04D000`-`04D007`, AT `1A0`-`1A7`, eight registers,
      confirmed by aliasing period. **Note for any oracle comparison:** the
      DN3500 has the OMTI in `isa1` by default, so `-isa1 ctape` *removes the
      disk controller* — they must go in different slots, and the failure is
      silent (C16). *Verification: first register read matches the oracle exactly
      for the no-media state.*
- [ ] QIC-II cartridge tape, `.ct` images. *Verification: reads the bitsavers
      SR10.4 install tapes.*
- [ ] `.awd` / `.afd` image formats, so oracle and emulator share media.
      *Verification: byte-identical reads of the same image under both.*
- [ ] **Integration check, not a milestone:** DN3500 boots Domain/OS SR10.x to a
      login prompt, console byte-identical to the oracle. *Verification: console
      diff plus a boot state hash.*

## Phase 5 — Display

- [ ] Mono 1024×800 graphics controller and display timing. *Verification:
      framebuffer decoded to PNG and inspected; oracle frame diff.*
- [ ] Colour and 8-plane controllers; 1280×1024 mono. *Verification: as above
      per controller.*
- [ ] Headless frontend flags that earn their keep: run N cycles, dump state,
      `--dump-mem ADDR[:LEN]`, screenshots (raw framebuffer *and* scanned-out
      picture), periodic screenshots every N frames, scripted key/mouse input at
      given cycles, TTY capture, media load/persist. *Verification: each flag
      exercised in CTest.*
- [ ] SDL3 interactive frontend, implemented rather than stubbed: scanout to a
      letterboxed texture, keyboard/mouse mapping, `--frames` bounded mode
      smoke-tested under dummy SDL drivers. *Verification: bounded-mode CTest
      under `SDL_VIDEODRIVER=dummy`.*

## Phase 6 — The Apollo Token Ring

The novel work. No runnable oracle exists, so this phase is paper-oracle
discipline throughout.

- [ ] Disassemble `{3000,3500,4500,5500}_RING_*.bin` and recover the controller
      register map and dual-ported RAM layout. *Verification: every register
      recorded in `docs/references/RING.md` with the ROM address that proves it;
      cross-checked against both board generations.*
  - [x] `tools/ring-rom/disasm.py` resolves the option-ROM header, entry-point
        table and string table, and confines code to the checksummed image.
        *Verification: runs clean over all four ring ROMs and the 3C505 ROM;
        sum32 reports VALID for each.*
  - Tails found while building it, both recorded in `RING.md`: the DN3000 and
    DN5500 dumps are byte-identical (finding 5a), so this is three images to
    read and not four; and every option ROM carries an unexplained 2-byte
    trailer just past `length` (finding 7a, open question H). H is not a
    blocker — it is outside the image and nothing yet shows the machine reads
    it — but the boot PROM's option-ROM scan will settle it.
- [ ] MAC layer from `010005-00`: free and claimed tokens, frame start and
      separator characters, null separators, packet header, packet data, FCS,
      end-of-frame. *Verification: each format cites its manual section;
      encode/decode round-trip tests.*
- [ ] Physical layer: data stream, PLL behaviour, elastic-store buffer, passive
      bypass. *Verification: `010005-00` ch. 3 and patent 4,716,575 cited per
      behaviour.*
- [ ] `ring_medium` interface — attach, detach, advance bit clock, symbol in and
      out — narrow enough that a process-separated transport can be added later
      without touching node cores. *Verification: unit tests over the interface
      with synthetic nodes only.*
- [ ] Ring controller device: register interface, dual-ported RAM buffer,
      transmit and receive logic, bypass relays. *Verification: the ring ROM's
      own self-test passes under emulation — the firmware is the test.*
- [ ] Multi-node scheduler: N nodes on one cycle-locked ring, each advancing
      only on its own cycle boundaries. *Verification: whole-ring state hash
      reproducible across runs and across build types.*
- [ ] Cross-node probes: token round-trip time, ring latency per node inserted,
      behaviour under contention. *Verification: goldens over the ring result
      block, locked into CTest.*
- [ ] Two nodes see each other over the ring under Domain/OS. *Verification:
      `lcnode` on each node lists the other; console output diffed against
      itself across runs for determinism.*
- [ ] Node insertion and removal mid-run, including token loss and
      reconfiguration. *Verification: probes over the documented recovery
      behaviour.*
- [ ] 3c505 802.3 controller, so Domain networking can also be checked against
      MAME the way MAME does it. *Verification: oracle diff — this is the one
      networking path with a runnable reference.*

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
- [~] Close DN2500 `ram_base`, or record it as a documented gap with its cost to
      close. *Verification: an address-space table for Series 2500, or the boot
      PROM's own memory sizing code.*
  - **Corrected from `01000000` to `04000000`**, from the boot PROM's own reset
    vector — the second of the two routes this item names, and it needed no new
    material. `2500_BOOT_16182_8` begins with SSP `040007D0` where
    `3500_BOOT_12191_7` begins with `01000180` and its RAM is at `01000000`. A
    reset stack pointer must land in usable memory.
  - So the previous value was not merely unverified, it was **wrong**: it
    assumed the DN2500 matched the other 68030 models, and the PROM that
    disproves it has been in `roms/` all along.
  - Still `[~]` and still `PROVISIONAL`. The reset SSP proves memory exists at
    that address; it does not give where the region begins or ends. The extent
    needs a Series 2500 allocation table, and the oracle cannot help — it has no
    2500 driver.
  - `tests/goldens/model_table.txt` regenerated; the golden caught the change,
    which is what it is for.
  - **The same check was then run against every boot PROM we hold, and the rest
    agree.** `3000_BOOT_8475_4` and `_7` both start with SSP `00100180` against
    a table entry of `0x100000`; `3500`, `4500` and `5500` all start with
    `01000180` against `0x1000000`. Five models corroborated, one corrected.
  - That the only wrong entry was the only *assumed* one is worth stating. The
    others were taken from address-space tables and the reset vectors agree with
    them independently, which is a check on both. The DN2500's was a guess
    filling a gap, and a guess is what the check caught.
  - **A second DN2500 discrepancy fell out of the same listing.** Its PROM is
    **131072 bytes**, and `AP_BOARD_PROM_SIZE` is `0x010000` — 64 KB. Every
    other image fits (DN3000 is 32 KB, the rest exactly 64 KB), so
    `ap_board_load_prom` would refuse the Series 2500's outright.
  - Not a defect in the board: `ap_board` is the **DN3500** and its 64 KB region
    is `008778-03` Table 2-8's, correctly. It is a Phase 7 item — the board's
    PROM extent is model variance and belongs in the model table with
    `ram_base`, not as a constant in a DN3500 header. Recorded here because it
    was found here; the work belongs with the model range.
- [ ] DN4500 Matrox graphics. *Verification: PNG inspection; no oracle, so
      documented as paper-verified.*
- [ ] DSP variants confirmed as true subsets. *Verification: `dsp3500` boots
      headless; oracle diff.*

## Phase 8 — Verified fast mode

Only after the reference core is proven, and only under an identity harness.

- [ ] Squeeze the reference core first: LTO, `flatten` on the run loops,
      idle-skip guards naming each subsystem's no-op states, cached arbitration
      results, cached per-cycle re-derived values. *Verification: probe goldens
      and boot state hashes byte-identical; speed-up measured on release
      builds only.*
- [ ] Exact-skip scheduling: `next_event()` and `skip(n)` per subsystem, CPU
      half and devices half of the tick split so a span-breaking I/O write still
      runs its devices half canonically. *Verification: entire probe suite and
      long boot hashes byte-identical to the reference core.*
- [ ] Extend exact-skip across nodes: run node cores in parallel only within
      provably inert windows between ring events. *Verification: whole-ring
      state hash identical to the single-threaded reference.*

## Phase 9 — Content testing

- [ ] `docs/references/TEST_SHELF.md`: Domain/OS releases and applications
      organised by the subsystem each stresses.
- [ ] Boot every Domain/OS release obtainable (SR9.7, SR10.1–10.4).
      *Verification: each boot recorded with its state hash; failures explained,
      not hidden.*
- [ ] Boot every firmware revision we hold, including both `3000_BOOT` revisions
      and both ring board generations.
- [ ] Real multi-node Domain workloads: distributed single-level store across
      nodes, `lcnode`, remote file access. *Verification: content finds what
      unit tests did not; each finding lands with a test.*

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
- **DN3550** (Series 3550) — discovered in `[CFG]`: a 68030 @ 25 MHz machine
  with the DN3500's CPU and a wider graphics menu (19" colour 1280×1024, 19"
  colour 1024×800, 19" mono 1280×1024, 15" colour 1024×800). Not added to the
  table yet because its only known difference from DN3500 is display options,
  which nothing models until Phase 5. Add it there.
- **DN4000** (and DN3000-to-DN2500 / DN3500-to-DN4500 upgrade paths) —
  `[CFG]` documents DN4000 as the prior-generation 68020 sibling of DN3000. Not
  in the agreed scope and not in MAME; revisit only if the 68020 path proves it
  cheap.
- **Display variants as first-class configurations** — the table currently names
  each model's base monochrome panel only. MAME models variants as separate
  machines (`dn3500`, `dn3500_19i`, colour). Decide the representation in
  Phase 5, not before.
