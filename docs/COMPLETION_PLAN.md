# Completion plan

Phased road to done. Each item names **its verification** — an item without one
cannot be ticked. Tails discovered while implementing something go into this
file the moment they are found, not when someone remembers.

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
- [ ] Ring controller device: register interface, dual-ported RAM buffer,
      transmit and receive logic, bypass relays. *Verification: the ring ROM's
      own self-test passes under emulation — the firmware is the test.*
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
  - [ ] **The transmit/receive handshake, which is what actually blocks the
        self-test.** **Prerequisite done**: the harness stepped the *processor*
        rather than the machine, so no time passed during the firmware's run --
        `FINDINGS.md` C109's defect, fixed there and left here. It now reports
        `elapsed 122,231,950,714,368 base unit(s)` where it produced zero, and
        the fourteen passing subtests are unchanged (`RING.md` 69b, 69c). An
        operation that completes after a while could not have completed on the
        old harness at all.
        **And a constant duration is ruled out, by trying one** (`RING.md` 70,
        70a): 8 us derived from `[MAC]`'s 12-byte minimum at 12 Mbit/s finishes
        *before* subtest 22 polls, so the firmware reads a bit already restored
        and fails. Choosing a longer constant means trying values until our own
        test passes -- the parameter search `CLAUDE.md` forbids -- and no source
        gives an independent figure. So the completion **must** come from
        `ap_ring_station`, where the time is the medium's: 69a's design is
        required, not preferred. The timed scaffolding was reverted rather than
        left dormant.
        **And the firmware names the source** (`RING.md` 71-71b): `$538`-`$544`
        loads the two **8254 counters** with `$1FF` and `$3FF` and *then*
        issues the `$6` command. Finding 41a had already established these are
        packet counters clocked by **ring traffic**; nobody had connected that
        to the completion. So it is a **count to be reached**, not a duration
        to be looked up, and both halves are modelled -- what is missing is the
        wire that clocks the 8254s from ring traffic, which 41a closed as a
        question about frequency while leaving the connection unmade.
        **Corrected the same day it was written** (`RING.md` 71c): the counters
        are **read back** four instructions after subtest 26 -- `$5B2`-`$5D4`
        walks five of them through subtests 31 and 32 -- so they *measure* the
        operation rather than gate it, and the load before the command puts
        them in a known state for that measurement.
        What that leaves is better than what it replaced: the completion still
        comes from the medium (70a), and subtests 31/32 supply a
        **quantitative check on what the operation must produce** -- a
        firmware-owned constraint to build the station-driven model against.
        The 8254-from-traffic wire is still unmade and still needed: for 31 and
        32 to pass, not for 26.
        **And the loopback is a DMA loop, not a wire loop** (`RING.md` 79-79d):
        `002398-01` Rev 1 -- on disk since the start, and every `[EH]` citation
        so far was Rev 4 -- documents a DIAGNOSTIC COMMAND register whose bit
        `8000` is "dma test (loop xmit DMA to rcv DMA)". So the self-test's
        loopback needs no medium and no station, which is a much smaller build
        than 77b assumed. Rev 1 also lists explicit "Network Trans/Rec
        Interrupt ACK" registers, independently documenting the acknowledge
        mechanism finding 74 recovered from the AT firmware alone.
        **And the `$6` command is now characterised** (`RING.md` 72-72b): all
        four of its sites in `[ROM3500]` share one five-step preamble --
        `$976`, `#$8` to `+404`, two counts, `$944` to load the 8254s, then
        `$6` -- with `[ROM4500]` matching, and **only the counter pair varies**
        (`$1FF`/`$3FF` three times, `$5`/`$5` once). So it is a *counted*
        operation whose arguments are two ring-traffic counts and whose result
        is read back through the same counters. That gives the station-driven
        model its shape with **no constant anywhere** -- the extents are the
        firmware's -- and the `$5`/`$5` site is an independent check a model
        fitted to the large counts would fail.
        **Twenty of the firmware's own subtests now pass** (`RING.md` 74-75b):
        the timing question dissolved when 74 found that the polling helpers
        are not passive -- `$9FA` and `$A28` each write an acknowledge to the
        *first* window after polling, which 56b had recorded and 69 was written
        without. The bits return when their condition is acknowledged, gated on
        a command being outstanding. Subtests 22-26 and 31 pass; the stop is now
        **SUBTEST 32** at `00059800`, the first 8254 -- the firmware asking for
        counters that have *counted*, which is the traffic wire 41a and 71d
        named -- **and 76 now has its specification exactly**: `$976` starts
        every counter at `$FFFF`, and subtest 32 requires `+800` to read
        `$FC03` and `+802`/`+804` to read `$FC00`, i.e. **1020, 1023 and 1023
        counts**, checked five times over. The header counter trailing the
        other two by exactly three is a structural fact to reproduce, not a
        number to approximate. Detail in `RING.md`.
        **Fourteen of the firmware's own subtests now pass**
        (`RING.md` 60-68), and the fifteenth is a *timing* question rather than
        a register one: subtest 26 requires `+400`'s bits 3-1 set where 22 and
        24 required two of them clear, with no intervening write, so they go
        clear while an operation is in flight and return when it completes (69).
        `ap_ring_sched` and `ap_ring_station` already model the ring with
        duration; driving the controller's status from them, rather than from
        the register write, is the remaining work -- and the self-test is now
        the test for it. Finding 45 shows `+400` has five polled bits (15, 13, 11,
        2, 1), each with its own timeout and expected polarity, and finding 48
        two byte-wide command registers taking `$1`, `$2`, `$6`, `$8`. **None
        of their meanings is established**, and they are what sequences the
        loopback test finding 50 describes — transmit a frame at buffer word 0,
        receive it back from word 16. Modelled as storage until a source
        settles them, which means the ring ROM's self-test gets past its memory
        test and no further.
        **All three ROM images are now read, and none of them answers this.**
        `[ROM3000]` and `[ROM4500]` poll the same four masks, test the same
        `$7FFF` extent, and use the port the same way (findings 51, 51a, 51c) —
        `[ROM4500]` differs from `[ROM3500]` in 1,413 bytes inside the
        self-test, so that is a real revision agreeing, not a reprint. The
        self-test hands its expected value, actual value and register address
        back to the caller as *data*, and the whole string table is five
        messages, so no per-bit text exists to recover.
        **The filesystem walk that blocks this is DOCUMENTED** (`RING.md`
        84-84d): `[AEGIS]` chapter 4 is the on-disk format -- VTOC header,
        VTOC block, VTOC entries, and §4.4.1 "Locating an Object in the VTOC"
        with the VTOCX laid out as bits 31:4 block and 3:0 index, plus a worked
        example any implementation can check itself against. The header sits in
        the **logical volume label**, one step from what `image/ap_volume.*`
        already parses. This manual has been on disk since the start and was
        never cited for this. It unblocks `ring8a.drvr`, the SELF_TEST image's
        configuration checksum (83a), and the Ethernet and graphics
        equivalents.
        **The only route left is Domain/OS's own ring driver -- and that route
        is now open** (`RING.md` findings 53-53e). The installed disk carries
        the driver's link map with every entry point named, including
        `RING_$POLL_STICKY_BPHERR`, which names a **latched bi-phase error**:
        the first name attached to any of `+400`'s polled bits from outside the
        ROM. Nineteen hardware conditions are named in order by
        `domain_ring.pas`, six with prose definitions.
        **The next step is concrete and bounded**: the files are named --
        `ring8a.drvr` and `ring8b.drvr` -- and `RING_PROC` is **`0x3370`
        bytes**, with every routine at a known offset inside it
        (`RING_$SENDP` `+0xABC`, `RING8_$INT` `+0x758`,
        `RING_$POLL_STICKY_BPHERR` `+0x19A4`). What stands between here and
        `+400`'s bit meanings is **walking the AEGIS filesystem** to extract 13
        KB: neither the load addresses nor the directory records give a byte
        offset. That is a filesystem problem, not a research one, and it would
        pay for itself well beyond the ring. Two further routes stay closed: finding 50a and
        51c.
  - [ ] The DMA path and the interrupt.
        **Blocked on which line, and it is a real three-way disagreement**
        (`RING.md` 82-82b): the controller has no IRQ accessor and no wiring at
        all -- the dangling shape the 3c505 had -- and `[S3K]` Table 2-3's
        "IRQ3 = Network Board", `FINDINGS.md` C11's measured cascade on IR3,
        and finding 53d's vector 163 (master IR3 through the measured `A0`
        base) all name IR3, which cannot be both the cascade and the ring.
        A wrong line is worse than none: it delivers interrupts a real machine
        would not. What settles it is a `writetrace.lua` sweep of
        `011000`-`0111FF` with a ring ROM fitted, watching which mask bit the
        PROM clears -- the method C11 already used on the ICWs, and cheaper
        than more reading. The device half *is* documented: Rev 1 gives the
        board's interrupt-pending and enable bits and its clear rule (79c).
        **Run, and it does not isolate the line** (82c-82e): across three
        configurations the slave controller ends **fully masked** every time,
        so there is no bit to identify. `--ring-rom` diverts the boot into the
        console poll and never reaches Domain/OS; `--ring` alone boots normally
        and still unmasks nothing. Finding 53e's rule explains it -- the driver
        installs into vector 163 *only when the card answers* -- so the line
        will be named by making Domain/OS's driver accept this controller, not
        by another trace or another manual.
        **The vector number is now measured:
        163** (`RING.md` 53d) -- `VBR` is `3C400800` and `RING_VEC` sits at
        `3C400A8C`, so the slot is arithmetic on two measured addresses rather
        than an inference, and `RING_8025_VEC1`/`VEC2` are 172 and 171 the same
        way. The control is that on a machine with no ring card the slot holds
        the *generic* handler while the claimed vectors resolve to the timer and
        both DUART channels and match the boot's own interrupt counts (53e).
        **And the driver's code turns out to be readable offline** (53f): both
        `RING8_$INT` and its deferred half are resident in an ordinary state
        dump, because Domain/OS loads the ring driver whether or not a
        controller answers -- so this source costs a `grep`, not an instrumented
        run on hardware nobody has. Its prologue polls **bit 1 of the word at
        device `+1400`** and reads `+1404` (53g).
        **The ROM cross-read was tried and is inconclusive** (53h): the
        firmware's `+400`/`+402`/`+404` and the driver's `+1400`/`+1404` do not
        correspond under any simple geometry -- the two AT windows are `$8000`
        apart, and `+1400` into window 0 lands inside *unit 1's* range. Recorded
        as inconclusive rather than reconciled, because a manufactured match
        would be a wrong register map with two sources apparently agreeing.
        **Read, and it withdraws the register reading** (53i): `3C4D9000` is
        `RING_$CCB`, `RING_$CTL` is its `+10` -- which is the ISR's own
        `LEA $10(A0),A2` -- and the block's `+1C`/`+20` and `+28`/`+2C` are two
        **empty list heads pointing at themselves**. So `$20(A0)` is a queue
        pointer and `+1400` an offset into a queued structure, not a device
        window. 53h declined to reconcile the cross-read; this is why it could
        not be, and the caution was right.
        **Awaiting:** the mask and transfer shape from the **ROMs**, which
        address the controller directly (53j) -- and there are **three** of
        them, not four: the `3000` and `5500` images are byte-identical, the
        same firmware shipped for both models under one part number (54). The
        two `10666` revisions differ from it by ~54% of bytes and from each
        other by 41%, so they are genuinely independent readings of the same
        registers rather than one image twice. The driver remains the source for
        names and for the vector, and a poor one for register semantics --
        everything it touches is an indirection away from the hardware, and the
        indirections are empty on every machine this project can dump.
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
      `lcnode` on each node lists the other; console output diffed against
      itself across runs for determinism.*
- [ ] Node insertion and removal mid-run, with stripping and token loss, in
      `src/core/ring/ap_ring_station.*`.
      **UNTICKED BY AUDIT** (`RING.md` 85-85e): this claimed "the transmit
      sequence of `[MAC]` §2.1 ... implemented" and **three of its eight steps
      are absent** -- step 3's "begins to transmit its packet", step 6's "sends
      out a new free token to follow the frame", and step 7's "until it
      finishes receiving its own frame". Nothing outside `ap_ring_framer`'s own
      tests ever calls it, so no frame is ever put on the medium; §2.2.2.2's
      destination matching does not exist and the acknowledge fields are never
      modified in flight. What *is* implemented and correct is the token,
      claim, strip and forward behaviour below the frame.
      **Transmit half now built** (`RING.md` 86-86b): `queue_frame` assembles
      through the framer into a caller-lent buffer and drives it a bit per bit
      time while stripping (step 3), originates a free token the instant the
      last bit goes out (step 6), and ends stripping when its own frame start
      has come back and its own length has passed (step 7). The new test caught
      two defects on the way, the worse being that stripping **overwrote the
      free token step 6 emits**, so the ring was never released.
      **Receive decision now built too** (`RING.md` 87-87c): a station has an
      address and destuffs the passing stream far enough to take §2.2.2.2's
      decision, checking broadcast first because "receivers ignore the
      destination address field" when it is set. The capture waits for the
      *separator*, not the frame start -- §2.2.2.1's frame start sequence has a
      null separator between them, and starting early puts eight zero bits into
      the destination address.
      **Still absent**: 85c alone -- the acknowledge fields are not modified in
      flight. The station now knows whether it is addressed, which is the
      precondition every one of those bits attaches to, so what is left is
      rewriting bytes in the forwarded stream.
      The stripping timeout and the token-loss recovery are implemented;
      removing a
      node mid-run is measured to lose an in-flight token, and a waiting
      station recovers by forcing a claimed token. Findings 34-37 in `RING.md`.
      *Verification: five more `ring_station_suite` tests, 12 in total. Detail
      in `PROJECT_STATUS.md`.*
  - One value stays `PROVISIONAL` and is marked in the header: §2.2.1.1 says a
    node forces a token "after a specified timeout" and never specifies it.
    The stripping timeout stands in, as the only documented figure of the right
    order; patent 4,716,575 is where a real one would come from.
- [x] 3c505 802.3 controller, so Domain networking can also be checked against
      MAME the way MAME does it. *Verification: **the card's own firmware
      self-test passes** — `802.3 Network Controller-AT test passed.` — and the
      host-interface oracle diff agrees at the instruction level: our 22 writes
      to `058006` come from PCs `080382`/`080392`/`0803C2`/`0803C8`, which are
      `entry_05`'s own instructions and are the PCs `ETHERNET.md` finding 10a
      measured on the oracle before this model existed. MAME independently
      configures I/O `300`, IRQ 10, DRQ 6 and drives its DMA request as
      `HRDY && DMAE` (17, 17a, 18, 18a).*
      **One named approximation, `PROVISIONAL` in `ap_3c505.h`**: the adapter's
      power-on flag handshake is host-side, where MAME runs the card's real
      80186. Cost to close is an 80186 alone — both firmware dumps are already
      on disk (18c). Detail in `PROJECT_STATUS.md`.
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
- [ ] DN4500 Matrox graphics. *Verification: PNG inspection; no oracle, so
      documented as paper-verified.*
      **Opened, and the board's own ROM is the specification.**
      `4500_Matrox_013748_04.bin` is a valid Apollo option ROM that
      `tools/ring-rom/disasm.py` reads — and it is the **only** image on disk
      whose `magic1` is `C000A0B7`, the class the boot PROM's early scan accepts
      on magic alone (`RING.md` 70, which refutes 59b's claim that no such ROM
      was held). Fitted with the new `--option-rom` flag the scan takes it and
      the machine executes it, stopping at `+5A8`.
      **Two register addresses recovered from the firmware**: it writes the word
      sequence `A534`, `1744`, `1345` to **`$D40000`** and then polls
      **`$DA0006` bit 3** for clear, with a 15.7 M-iteration timeout — the shape
      the ring and 3c505 items both started from. Nothing decodes either
      address here, so the poll spins out.
      **Caveat, stated because it would otherwise be assumed away**: that run
      used the DN3500 PROM and map, since `identity-boot.sh` does. The
      addresses are facts about the *board*; whether they decode the same on a
      DN4500 needs `4500_BOOT_13167_02_MD7R.0.32.bin` and that model's map —
      and that PROM does **not** carry either address as an absolute operand,
      checked.
      **Both documentary routes are now exhausted, and named so nobody repeats
      them.** `[S3K]` chapter 10 is the graphics chapter and covers the
      **DN3000 and DN4000** controllers only — 4-plane colour, two 1280x1024
      monochrome, 8-plane colour — as PCB dimensions, cables and supply
      voltages, with no register map and no Matrox board. Its Table 2-6
      graphics ranges (`0A0000`, `0C0000`, `0E0000`, `FA0000`) contain neither
      address. The web has no register-level material for Apollo part `013748`
      either; what exists is sales listings and unrelated modern Matrox parts.
      **So the ROM is the only source left, which is where the ring and the
      3c505 both ended up — and it has now been read.**
      `docs/references/GRAPHICS.md` holds ten findings, each citing a ROM
      address: the option-ROM header and its four entry points, **three
      register blocks** extracted mechanically (`$D40000` a bidirectional data
      port, `$D80004`/`+5`/`+8` a longword path with a ready bit at bit 7,
      `$DA0000` a block whose `+6`/`+7` are command-over-status with bits 3, 4
      and 5 polled), and the stop at `+5A8` explained by the firmware's own
      15.7 M-iteration wait for `$DA0006` bit 3 to clear.
      **The board takes a downloaded program, and it is now measured rather
      than inferred**: 4,716 bytes from ROM `+B22`, written word by word to the
      fixed port `$DA0000`, ending exactly on the header's `length` field — two
      independent numbers meeting. The `ID: GAO Boot Microcode` ASCII is that
      image's own header, not a console message. The two `move.l (a3)+` loops
      that first looked like the download are a **16-longword CRTC parameter
      table** carrying `00000400` = 1024 (GRAPHICS.md 4a/4b, correcting 4).
      **Which makes the item smaller than it looked**: nothing has to *execute*
      the microcode, only accept it. What is needed is the three ports, the
      status bits the firmware polls, and a frame buffer — not a coprocessor.
      **The device now exists** — `src/core/device/ap_matrox.*`, fitted by
      `--matrox`, decoding the three blocks and answering `$DA0006` as zero,
      which is what GRAPHICS.md 11's two verdict conditions require and asserts
      nothing further. The microcode download completes and is *measured* to:
      `a1` ends at `00081D8E`, the ROM base plus exactly its `length` (12).
      **What it has not bought is a boot** (12a): with the ROM fitted the
      machine prints nothing at all and never reaches its self tests.
      Two hypotheses tested and **both refuted** — it is not the DN3500/DN4500
      mismatch (12d: the DN4500's own PROM and map fail identically, with the
      no-card control booting normally), and it is not the 64 KB image
      answering all four scan slots (12f: the third slot is read zero times).
      A third reading — a runaway through PROM data — was also **wrong and is
      retracted** (12g): those `4801`s are the body of a **delay loop**, closed
      by `subq.l`/`bgt` two instructions later, and the machine reaches them
      606 K instructions before the option ROM ever runs.
      **What it actually is** (12j-12l): `$5ED6`-`$5F00` is the firmware's
      **error display** — two delays and two writes to `$00010100`, the
      diagnostic LED register, with no test and no exit. That register and this
      very loop are already documented in `ap_boardreg.h` and `FINDINGS.md`
      C109, and the boot report already prints `posted codes`, so three turns
      of disassembly ended at a line of output that was there all along.
      The codes, with controls: a healthy DN4500 posts `… 9F 8F FE`
      (complemented: … 50, 60, **70**); with the option ROM it posts `… 9F ED`
      and loops — **whether or not the Matrox device is fitted**. So the device
      is not what fails (12m).
      **Decoded and fixed** (13-13b): the post routine's `ror.b #4` / `not.b`
      turns the displayed `ED` back into code **`21`**, whose check is at
      `[ROM4500]` `$6962` and tests the machine's **own** display controller at
      `0005E801` — not the Matrox board. Every run had `display none`, so that
      compare met an empty decode. **With `--screen c8p` the boot goes six
      checks further** (`… ED DD 9D 8D 7D 6D 5D FC`) and control reaches the
      Matrox ROM's own code.
      **And the board no longer stops the boot** (13c, 14): `[ROMMX]`
      `$2EC`-`$310` waits for `$DA0006` **bit 5 set** — the opposite polarity
      to bits 3 and 6, which is why answering the register zero satisfied one
      routine and stalled this one. With it satisfied the register reads `$20`
      (the three measured conditions and nothing else), six further checks
      pass, and the machine ends in `FINDINGS.md` C109's **console-selection
      poll** — idle and waiting for a keystroke, not failed.
      Stated precisely (14a): the no-card control posts three codes further,
      so the two are **not** identical and fitting the board still costs
      something. What is established is that it no longer *stops* the boot.
      **The verification itself now runs, and it fails usefully** (15): with a
      screen and no card, `--screenshot` shows the DN4500's entire boot —
      `SELF TESTS IN PROGRESS.`, every test line, `COULD NOT LOAD
      /SAU7/SELF_TEST.` and a `>` prompt. With the card it is **entirely
      black**. The counters say why (15a): the built-in controller drops from
      9.9 M plane writes to 1.6 M while the Matrox blocks go to 1.5 M reads —
      the console has moved to the card, which is what a graphics option ROM is
      for. So question A is settled by counting rather than reading (15b):
      16,618 writes is four orders short of a 1024x800 8-plane frame, so the
      **pixel path is elsewhere and undecoded**.
      **Found** (16): 30.7 M reads and 50,744 writes land in the undecoded AT
      window, and the first write is `000C63AF` — inside `[S3K]` Table 2-6's
      `0C0000-0DFFFF`, "ALTERNATE MONO GRAPHICS MEMORY SPACE". The board's own
      ROM corroborates independently, `$2E0` being `movea.l #$c63b2,a3` (16a).
      The **range** is settled by measurement; the **layout is not** (16b) —
      planes, pitch, and whether `0C0000` is the origin or a window.
      16's citation was corrected for citing the **DS3000's 16-MB** table on a
      32-bit machine (16c) — and then **the right table restored the name**
      (17): `019411-A00` Table 2-5, the DS5500 256-MB allocation and the only
      32-bit Apollo map on disk, gives `0C0000-0DFFFF` the *same* name, on the
      map class this core uses for DN3500/4500/5500. So the label carries
      across address-space sizes and the retraction was one step too far.
      The card therefore presents as the **alternate monochrome** controller
      (17a) — a real distinction, since that table puts alternate *colour* at
      `0E0000` and this machine's own 8-plane display at `0A0000`, and the card
      writes to neither.
      **RETRACTED by rendering it** (18): decoding `0C0000`-`0DFFFF` for real
      gives it **6 writes** in a whole boot while **50,738** still land in the
      undecoded window. Finding 16 had taken the report's `first write` as the
      location of fifty thousand — one sample generalised to a population, this
      project's oldest error, with the instrument that catches it (`first
      seen`, sixteen distinct addresses) in the same report.
      **Measured instead** (18a): the writes are at `0093D000`-`0093DD29`, with
      108,035 further distinct addresses the tracker could not hold — AT bus
      memory, not a documented graphics range. A 3.3 KB span under 50,738
      writes is a **window or a port**, not a linear frame, and that is the
      next thing to establish. 17/17a's "alternate mono controller" reading
      rested on the retracted address and is unsupported until something else
      carries it; finding 15a's counting — that the console moved to this card
      — is untouched.
      The frame buffer and `--matrox-screenshot` land anyway: they decode a
      documented graphics range, cost the reference boot nothing, and are the
      instrument that produced the retraction.
      **And the extent settles what the traffic is** (18c): a new lowest/highest
      tracker gives write span `0004D402..0093DD3F` and read span
      `0004D400..00FFF003` — across AT I/O *and* memory — with ~110,000
      accesses over **108,051 distinct addresses**, about one per address. That
      is a **scan**, not a frame being drawn.
      **So 15a's attribution is withdrawn too** (18d): the built-in
      controller's drop from 9.9 M plane writes to 1.6 M is real, but "the
      console moved to the card" was inference and the card is not where the
      pixels went. Where they went is **open**. What still stands from this
      line is the register map, the firmware's assertions, and that the screen
      is black — the located frame buffer, its geometry and its identification
      do not.
      *Verification so far: `matrox_suite`, 6 tests, each replaying a ROM
      address; the reference boot returns `A354786119A3931D` unchanged.*
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
      **Awaiting:** the CPU half — skipping instruction steps across a span
      with no events, which is what `skip(n)` names. The devices half is done.
  - [x] **The 8259's priority resolver returns early when nothing is asking** —
    one line, provably equivalent, no new state to invalidate.
    *Verification: 281 s → 273 s and 275 s, state hash `67A14B3BB6041410`,
    exception census identical; `ctest` 129/129. Detail in
    `PROJECT_STATUS.md`.*
  - [x] **Devices half done**: the bus tick's DMA poll is skipped when nothing
    can ask, via a flag armed at an auditable set of sites.
    *Verification: 296 s → 284 s and 281 s (**1.04x**), state hash
    `67A14B3BB6041410`, DMA counters bit-identical; `ctest` 129/129. Groundwork,
    result and the invalidation rule in `PROJECT_STATUS.md`.*
- [ ] Extend exact-skip across nodes: run node cores in parallel only within
      provably inert windows between ring events. *Verification: whole-ring
      state hash identical to the single-threaded reference.*

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
- [ ] Boot every Domain/OS release obtainable (SR9.7, SR10.1–10.4).
      *Verification: each boot recorded with its state hash; failures explained,
      not hidden.*
- [ ] Boot every firmware revision we hold, including both `3000_BOOT` revisions
      and both ring board generations.
      **Done and recorded**: six boot PROMs across five models, each with its
      state hash and what it did. It found one defect of ours — the frontend
      fitted 16 MB to every model, twice what a DN3000 takes, which left the
      boot PROM's sizing strap unset and failed its memory test with
      `E0060882`. Memory size now comes from the model table, `--ram` selects
      it, and both DN3000 revisions pass into Memory Module 2. Detail and the
      display-redirects-the-console trap in `PROJECT_STATUS.md`.
      **Awaiting**, each named there: the DN2500's map, now **half-recovered
      from its own firmware** — the PROM region is 128 K (proved by the image's
      own self-checksum bounds and its `0001F040` reset PC) and the core device
      block is the Series 4000's shifted up by `0x10000`, confirmed against
      six addresses; what is missing is the AT cards at ISA `140` and `148`,
      which are not shifted Series 4000 addresses. The DN4500's memory strap is
      **solved**: the firmware decodes it with a fourteen-arm `cmp.b` chain,
      identical in the DN3500's PROM, and the oracle's four bank layouts fall
      out of it unchanged. Its self-test failure was our unstrapped port reading
      `00`, which the firmware reads as twenty megabytes rather than as no
      answer. Still open: the DN5500, now precisely diagnosed — it stops at its
      second instruction, `cinva`, because the 68040 is modelled and nothing
      executes on it; and both ring generations, which need the ring controller
      device.
      *Verification: `frontend_flags` 13 → 16; DN3500 30 M hash unchanged.*
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
