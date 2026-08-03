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
        `ap_m68030_desc`'s rules, and fill the ATC — the piece that joins the
        four MMU modules together, and the first whose *timing* is a table
        search rather than an instruction.
        *Verification: `walk_suite`, 40 tests. Detail in `PROJECT_STATUS.md`.*
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
- [ ] Two 8259 interrupt controllers and the Apollo interrupt vector scheme.
      *Verification: probe-driven interrupt ordering vs oracle.*
      **Awaiting:** a *second* synchronously-raisable interrupt source. The
      claim here a moment ago — that the probe had become runnable — was wrong
      in the half that mattered: ordering needs two lines standing at once, and
      this board has exactly one a program can raise with no time passing. The
      timer and calendar need the tick loop; the tape's `DONE` needs the DMA
      channel assignments the DMA item is itself waiting on; the disk has no
      accessor. Enumerated in `PROJECT_STATUS.md`, since it is a negative.
      The ordering a *program* sees is now verified through the machine, which
      is what could be done without the oracle.
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
- [ ] Two AT DMA controllers. *Verification: transfer probes; device request
      lines gate DMA at block granularity, not per word.*
      **Awaiting:** a *device* driving a request line. Transfers run, block
      granularity is measured, and the request path is the part's own — what is
      missing is which peripheral sits on which channel, which this board has
      not been measured for and `board/ap_dma.h` deliberately refuses to assume.
      Until then the peripheral side of a transfer reads all ones and is
      counted, and verify transfers carry the measurements. Closes with the disk
      and tape controllers, whose own items own the answer.
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
        `010D00` (stride 2). `dma_suite`, 6 tests. The AT convention that the
        first controller cascades onto channel 0 of the second is deliberately
        **not** asserted — the equivalent assumption about the interrupt
        controllers was wrong here (C11) — and will be measured once transfers
        exist to measure.
  - [x] And the shared arbitration point it was pointing at — Phase 3's first
        item, which had been waiting for a second bus master to exist. Each was
        the other's blocker and neither was blocked: the arbiter needed a master
        to arbitrate for, the transfers needed a bus to arbitrate over, and both
        landed the moment either did.
- [ ] Interval timer and calendar. *Verification: self-timing probes; a long
      calendar interval carried correctly at every boundary.*
      **The 14-day hazard this line used to cite does not exist.** It said
      "noted in the MAME driver"; the pinned `ext/mame` has nothing about days,
      weeks or intervals anywhere in its Apollo driver, and no commit in its
      history touches "14 days" under its `mame` sources at all. The claim dates from
      this project's first scaffolding commit, written before the driver had
      been read, and would have sent whoever picked this item up hunting for a
      note that was never there. The substance behind it is kept and now
      tested — see the calendar sub-item below.
      **Awaiting:** the self-timing probes, which need the tick loop. The
      interval timer counts at 250/125/62.5 kHz and nothing in this core
      advances on its own, so a probe cannot time it against itself yet.
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
  - [x] **The square-wave output and `DSE` are declined for the same reason**:
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
- [ ] SIO serial lines, keyboard and mouse. *Verification: console byte stream
      identical to the oracle's.*
      **Awaiting:** our half of that stream. The oracle's is captured byte-exact
      in `docs/references/MD.md`; ours does not exist because the PROM never
      transmits. Where it stops is now measured rather than described: with no
      input it polls both ports' status registers 487,558 times and stops at
      `000007AE`; a byte on serial 1 **channel A** — the keyboard's — carries it
      to `0000220E`, just past the scan-code table at `000021D2`, and there it
      runs C39's autobaud, 27,365 writes to channel B's clock select against
      13,683 to the ACR, two rate changes per pass, 13,683 passes, never
      completing. Repeated carriage returns on channel B do not complete it
      either. That is a `FINDINGS` campaign, not a coding item.
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
  - [ ] Drive the memory refresh from the DUART's timer. §3.9's period is
        pinned at exactly 99000 base units and the counter/timer is modelled;
        what is missing is anything advancing it, so this waits on the tick
        loop item below and on nothing else. The keyboard half of this child is
        done and ticked above.
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
- [ ] **The tick loop.** `CLAUDE.md` opens with "one `tick()` per machine
      cycle, every subsystem advancing inside it", and this core had none: a
      counter reached terminal count only if a test reached in and advanced it.
      *Verification: a device counter reaching terminal count with no program
      touching it.*
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
  - [ ] Remaining: the **DUART's counter/timer**, which has no advance function
        at all — registers and commands with nothing driving them — so calling
        it from the tick would be the pretence of a tick loop rather than one.
        It is the memory refresh's dependency and lands with that child.
  - [ ] Remaining: the CPU is stepped by instruction, because `ap_m68030_step`
        runs a whole one. A per-cycle processor is what "one `tick()` per
        machine cycle" asks for literally, and it is a Phase 8 question rather
        than a Phase 3 one — the identity harness has to exist before the run
        loop is rewritten under it.

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

- [ ] **A DN3000 core board, and `dn3000` boots.** Carried here from Phase 2b's
      68020 and 68851 items, whose verification it was: a boot needs a board,
      and a board is Phase 3's subject while a first boot is this phase's. The
      reference is in hand -- `008778-03` Table 2-6 gives the DS3000's 16 MB
      map, the counterpart to Table 2-8 that `board/ap_board.c` already builds
      the DN3500 from. Two differences are structural rather than cosmetic: main
      memory starts at `100000` rather than `1000000`, and there is no address
      translation map ("the Series 4000, unlike the Series 3000, incorporates an
      address translation map in its architecture"), so DMA reaches physical
      memory directly. *Verification: `dn3000` boots; oracle diff.*

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
  - [ ] **In progress — The display controller is the next module**, and now for a reason
        rather than as the next thing on a list. It stops being a probe target:
        the four regions already recorded (`05D800`/`05E800` registers,
        `0FA0000`/`000A0000` graphics memory) become the machine's output.
        - Kept as the *rationale* and marked **In progress**, because it is not a separate
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
  - [ ] **In progress.** Superseded reading, kept because it was right about the device and wrong
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
        - The rate is the **model's**, taken from the table by
          `ap_machine_init_model`, and there is no setter to override it with.
          A rate the base cannot represent is refused rather than rounded, at
          `ap_clock_init`, and `time_suite` pins that the base divides every
          model's clock — so an unrepresentable one is a red test rather than a
          machine that quietly keeps no time.
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
  - [ ] **In progress.** The long frame's INTERNAL REGISTER fields are stacked as zero — a
        deliberate approximation, since this model has no microsequencer state
        to save. **The coprocessor mid-instruction frame (`$9`) makes the same
        approximation for the same reason**, in its four INTERNAL REGISTERS
        words, and pays the same stated cost: an `RTE` from that frame is
        declined rather than resumed. Cost to close: an `RTE` resuming a fault *mid-instruction*
        cannot work from a zeroed frame, so the rerun must reconstruct the
        access from the SSW and fault address instead of from internal state.
        - **Landed, and now recorded as the convention requires**: marked
          `PROVISIONAL` in `ap_m68030_step.c` and entered in
          `PROJECT_STATUS.md`'s `PROVISIONAL` table, alongside the second
          approximation it produced — `RTE` re-executing the faulted
          instruction from the start rather than resuming mid-instruction.
        - **In progress** rather than simply unticked: the approximation is *made*, deliberately and
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
      **Awaiting:** the recovery itself. The one child below is the *tool* — it
      resolves the header, entry-point and string tables and runs clean over
      all five ROMs — and not a single register has been recovered with it yet.
      A disassembler that works is not a register map, and the parent's
      verification is the map.
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
- [ ] **In progress.** Close DN2500 `ram_base`, or record it as a documented gap with its cost to
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
  - Still **In progress** and still `PROVISIONAL`. The reset SSP proves memory exists at
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
