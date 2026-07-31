# Completion plan

Phased road to done. Each item names **its verification** — an item without one
cannot be ticked. Tails discovered while implementing something go into this
file the moment they are found, not when someone remembers.

`[x]` done · `[~]` in progress · `[ ]` not started

## Phase 0 — Foundations

- [x] C23 + CMake/Ninja/Clang build, presets per platform, matching test presets
      for every build preset. *Verification: `cmake --preset` and `ctest
      --preset` succeed for each; CI runs all four platforms.*
  - [x] Supported set pinned to three 64-bit platforms — Linux x86-64 (Red Hat
        and Debian derived), Windows x86-64, macOS arm64 — with Clang the
        default and only supported compiler. 64-bit is enforced at configure
        time because time is a `uint64_t` in base units; a non-Clang compiler or
        unlisted platform warns instead. *Verification: `cmake/Platform.cmake`;
        the configure line names the resolved platform, compiler and width.*
- [x] Warning set applied to first-party targets only, `-Werror` **in every
      build type**, not debug and CI alone. `APOLLO_WERROR` now defaults to `ON`
      and `release-base` sets it explicitly. In an emulator a warning is rarely
      cosmetic — `-Wconversion` and `-Wsign-conversion` fire on exactly the
      silent width and signedness changes that make a cycle count or an address
      wrap differ between platforms, which is the one thing this project claims
      cannot happen. A warning that is an error in Debug and a note in Release
      is a warning that gets through in Release. Vendored code is unaffected:
      `apollo_set_warnings()` is never called on anything in `ext/`.
      *Verification: `build.ninja` carries `-Werror` for the release preset, and
      both `linux-debug` and `linux-release` build clean and pass 7/7 from a
      wiped build tree.*
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
        They had been on `develop`, `libpng18` and `main`, which pin a SHA but
        name a commit upstream never released. *Verification: `ext/README.md`
        records each tag; the suites pass on the new unity.*
  - [x] MIT/GPL boundary asserted at configure time rather than by review:
        `cmake/GplBoundary.cmake` rejects a `mame`/`musashi` include in any
        first-party source, and any target that acquires one in its link
        libraries or include directories. *Verification: both halves probed with
        a deliberate violation; both fail with a named message.*

- [x] Shared frontend layer in `src/frontend/common/`: part naming, the
      `--list-models` report, and the options every frontend accepts
      (`--model`, `--list-models`, `--help`), parsed one argument at a time so
      each frontend keeps its own flags. *Verification:
      `frontend_common_suite` — every model selectable by name, a typo rejected
      rather than silently emulating the wrong machine, a frontend-specific flag
      passed through untouched, and the report byte-identical across calls.*

## Phase 1 — Verification infrastructure, before the subsystems it checks

- [~] Build MAME with only the apollo driver and assemble the `dn3500` ROM set
      from `roms/firmware/`. *Verification: MAME boots Domain/OS from an SR10.x
      image to a login prompt.*
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
        Built and running: MAME v0.289, one driver, no tools.
        *Verification: `./apollo -listfull` lists all eleven apollo machines,
        `dn3000` through `dn5500`, and `test_oracle.py` passes against it.*
        Two tails recorded in `FINDINGS.md` rather than left to bite the next
        person: the binary is named `apollo`, not `mameapollo` — MAME's own
        naming moved, so `oracle.py` now accepts either, the same
        pin-independence rule `romset.py` already follows for ROMs — and the
        build is bounded by *memory* rather than cores, at a measured ~2.5 Gbyte
        for the peak translation unit, so `-j"$(nproc)"` is the wrong instinct
        on a small machine.
  - [ ] **Tail, found while assembling the media: there is no bootable image to
        boot.** All Domain/OS media we hold is *installation* media — `cptape`
        tape files (`tape1/00.img` is the `SYSBOOT` tape boot record) and QIC
        install cartridges. Reaching a login prompt therefore requires
        installing Domain/OS from tape onto a blank disk image under the oracle
        first. That is a much larger task than this item's wording implies, and
        it is the real gate on the first boot. *Verification: a disk image that
        boots to a login prompt under MAME, produced by a recorded, repeatable
        install rather than by hand.*
  - This also pulls `.ct` cartridge support (Phase 4) forward in importance: it
    is the format the first boot depends on, not merely a storage item.
  - [x] **Answered: the 68040 path does have an oracle.**
        `dn5500` starts and dumps reproducibly under the built binary (985
        bytes, two runs byte-identical) *despite* `MACHINE_NOT_WORKING`. So the
        flag is not grounds to move DN5500/DSP5500 to `paper` in the model
        table, and Phase 2's "`dn5500` oracle diff" stands as written — but the
        flag is MAME's own statement that it does not vouch for the result, so
        DN5500 readings are a divergence class to treat with suspicion rather
        than a missing oracle. Recorded in `FINDINGS.md`. The original wording
        of this item is kept below, because the reasoning it rejected — acting
        on the flag alone — is the part worth remembering.
        *Original:* **Verify empirically whether the 68040 path has an oracle at all.**
        `apollo.cpp` declares `dn5500`, `dsp5500` and `dn5500_19i`
        `MACHINE_NOT_WORKING` while no 3000 or 3500 machine carries the flag. If
        it holds, Phase 2's "`dn5500` oracle diff" is unachievable as written and
        DN5500/DSP5500 move from `mame` to `paper` in the model table (which is
        a golden change). Not acted on from the flag alone — the honest test is
        to run it. *Verification: an attempted `dn5500` run under the built
        oracle, recorded in `FINDINGS.md` either way.*
- [~] Oracle harness: drive MAME headless, run N frames/cycles, dump RAM and
      device state in our hex format. *Verification: two runs of the same
      workload produce identical dumps.*
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
        Both were concealed by a report that echoed its own input.
  - Determinism is the whole point, so the driver's flags are load-bearing and
    each closes one way a second run could differ: `-noreadconfig` (ignore
    `~/.mame/mame.ini`, which no one reviews), redirected and wiped
    `nvram`/`cfg`/`state`/`diff` directories (they persist across runs by
    design, so run 2 would otherwise start from a different machine than run 1),
    `-video none -sound none -nothrottle`, and `-seconds_to_run` as an
    emulated-time watchdog so a never-reached dump point fails instead of
    hanging.
- [x] `tools/mame-oracle/FINDINGS.md`: one row per probe campaign — ours, the
      oracle's, status, and the story. *Verification: the file exists and every
      closed row cites its evidence — vacuously true today, since no campaign
      has run and the campaign table is deliberately empty rather than
      pre-populated.* The ledger and its discipline exist: the four
      discrepancy classes (`ours-wrong`, `oracle-wrong`, `sub-poll-slack`,
      `open`), the ban on closing a row by tuning our timing to match, and the
      rule that an oracle number alone never closes a row.
- [ ] Python probe encoder emitting hand-assembled 68000 probes — no cross
      toolchain. *Verification: a trivial probe that stores a sentinel runs
      identically under both.*
  - **The route's assumption is now in doubt, and that is recorded rather than
    left implicit.** `tools/mame-oracle/FINDINGS.md` C4: the boot PROM does not
    reach the MD prompt under the oracle. `dsp3500` with a `null_modem` on the
    serial port transmits **zero bytes** at 20 and 90 emulated seconds, while
    the CPU sits in a short loop around `$7AE` with interrupts masked at 7 and
    the MMU never configured. The syntax work below stands; what is unproven is
    that there is a prompt to send it to. Next step is to disassemble
    `$780`-`$7C0` of the boot PROM and read what the loop waits on, which needs
    no further oracle runs. **Done, and it narrows the finding without closing
    it:** the loop polls three serial status bits through `A0` = `$10401`, which
    `apollo.cpp` maps to the SIO, so the PROM has reached its console code
    rather than failed a self-test. Supplying keystrokes through the
    `null_modem`'s input side changes nothing in either direction. The open
    question is now about `apollo_sio`'s ready reporting, not about the PROM.
    **Consequence for the plan: build the side-loading path first.** Phase 1
    already lists injecting probe state directly into a constructed machine as
    the CI path, precisely because it needs no firmware; on this evidence it is
    also the path that should come first, with MD as development-time
    confirmation rather than the foundation everything else is gated on.
  - **Route settled, and it is simpler than "Apollo's executable/boot format".**
    The boot PROM holds the Mnemonic Debugger (`008778-03` §1.5.1), whose `A`
    (access/deposit) and `G` (jump) commands take hand-assembled instruction
    words straight over the serial console — no object file, no executable
    format to recover first, and no Domain/OS boot, which is what keeps probes a
    Phase 1 deliverable instead of one gated on Phase 4. Command set recorded in
    `docs/references/MD.md` from `002398-04` ch. 5.
  - [x] **Input syntax: closed from the manual, not the oracle.** The blocker
        was recorded as "MD's syntax is not in the handbook's command list",
        which was true and misleading — the handbook continues *past* the list
        into a per-command reference (`[EH4]` pp. 5-7 on) and then states the
        grammar formally at pp. 5-13/5-14. It is now transcribed in
        `docs/references/MD.md`: the full BNF, hexadecimal-by-default input,
        `<size_spec>`/`<base_spec>` placement, `*` as current location, and the
        `AR` control-register names that are the Phase 2 MMU and cache probe
        surface. *Verification: cited to page; the OCR damage to `|` and `::=`
        is called out, and the reconstruction rests on the handbook expanding
        every token in prose immediately below the grammar rather than on
        inference.*
  - [ ] **Still open: the output format.** The handbook says `A` "prints address
        and contents" and never shows a literal line, so column layout,
        separators, prompt and terminator remain unknown — and the harness must
        parse exactly those bytes. Capture a real MD session under the oracle.
        *Verification: a captured session transcript in
        `docs/references/MD.md`, byte-exact.* The no-guessing rule still applies
        to the parser; it no longer blocks the encoder's input side.
- [x] Probes side-loadable into post-boot machine state, so CI needs no
      copyrighted firmware. *Verification: the probe suite runs in CI with
      `roms/` absent* — which it does: `apollo-headless --run-probes` reads no
      file, opens no ROM and needs no boot, and its output is pinned by
      `tests/goldens/probes.txt` under every build preset. Confirmed
      byte-identical between the `-O0` and `-O3` builds.
  - [x] **The machine to side-load into** (`src/core/machine/ap_machine.c`): a
        68030 wired to flat RAM and nothing else. Construct, poke memory and
        registers, run to a limit, read back — the whole cycle a probe performs,
        with no firmware and no boot. Built **first** rather than after the MD
        route, on C4's evidence.
        Not the DN3500: no I/O, no device, no arbitration point. Those are
        Phase 3, and machine variance belongs in the model table.
        **RAM is supplied, not allocated** — the core allocates nothing, so a
        probe picks its size and a test can put one on the stack.
        **Outside the RAM is a bus error**, counted rather than merely refused.
        Wrapping would invent an alias the hardware does not have; reading zero
        would make an out-of-range probe look like a working one that found
        empty memory. The range is checked as a *range*, so a long word
        straddling the top is refused rather than reading past the buffer.
        **A run takes a limit**, because a probe that loops forever must end as
        a failed probe rather than as a hung harness.
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
        multiplies and divides, `MOVEM` out and back, and a `PMOVE`.
        **A probe reports rather than judges.** Nothing in the module knows what
        any result *should* be — a unit test asserts what someone expected, and
        a golden pins what the emulator did, byte for byte, on every platform
        and both build types. That is the cross-platform identity claim, and the
        only mechanism that catches one platform quietly disagreeing with three.
        **Every probe ends with `STOP`**, so it finishes because its program said
        so rather than because it ran out of limit. Two were built without a
        terminator and their first goldens showed it: a loop reporting 20
        instructions for six iterations of work, and a subroutine that returned
        and then fell into its own callee. A probe that hits its limit reports
        whatever it happened to be doing.
        The runner blanks the RAM and plants a returning handler on every vector
        before each probe, so a result cannot depend on what ran before it and
        an unexpected fault lands somewhere legible instead of in blank memory.
        The reported clock is **bus and cache time only**, said so in the report
        itself. It is pinned anyway: when instruction execution time arrives the
        golden moves, and the diff says by how much for every probe at once.
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
        `ap_m68030_cache_clear` clears valid bits and leaves the tag and data
        behind — by design, and documented — so an invalid entry holds whatever
        the last occupant left, which no lookup can reach. The hash fed it
        anyway, and `ap_m68030_atc_flush` leaves the same debris.
        That is a **false positive**, and worse than a miss: a harness that
        rejects identical machines cannot be used at all, so every item gated on
        it would have been gated on something unusable. The hash now covers what
        an access can reach — a line's tag only when some entry is valid, an
        entry's data only when that entry is — with one deliberate exception,
        the ATC's history bit, which the replacement algorithm reads whatever the
        valid bit says.
        `ap_machine_init` also now defines *every* field rather than the ones it
        sets: a machine whose behaviour depended on what was in the caller's
        stack beforehand is not reproducible, which is the one thing it must be.
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
- [ ] Full-machine state hash over all architectural and timing state, excluding
      host pointers, with emulated cycle count and PC reported beside it.
      *Verification: same workload twice → same hash; a boot collapses to one
      number.*
  - [x] The hashing primitive itself (`src/core/state/`): FNV-1a 64-bit with an
        explicit little-endian feed and width-tagged typed helpers, so the same
        state hashes identically on a big-endian host and a re-typed field
        cannot silently preserve the hash. No `ap_hash_ptr()` exists — host
        pointers are excluded by construction rather than by discipline.
        *Verification: `hash_suite`, 11 tests, including the **published**
        FNV-1a 64 vectors — external constants, not our own output — plus
        little-endian feed, `2×u16 ≠ u32`, `time ≠ u64`, order sensitivity, and
        streaming equals one-shot.*
  - [x] **The CPU's contribution** (`src/core/cpu/m68030/ap_m68030_state.c`),
        landed now that there is a CPU to hash. Every register including the
        *inactive* stack pointers, the whole status register, the MMU registers,
        the cache control registers, the instruction pipe and its holding
        register, both caches, the ATC, the pending exception and interrupt
        state — and the **accumulated clock**, which is hashed with the
        registers rather than reported beside them. Two runs reaching the same
        registers by different numbers of bus cycles are not the same run on a
        machine whose whole claim is emergent timing, and that is precisely the
        divergence a fast mode introduces.
        The instruction and data sides are fed in that order, so a machine with
        the two caches exchanged does not hash the same as one without; an
        absent access context feeds a marker rather than nothing, since "no data
        side" and "a data side whose cache is empty" are different machines; and
        an *invalid* ATC entry still contributes its history bit, which is what
        the replacement algorithm reads.
        The failure mode this must not have is a field that moves while the hash
        does not — the harness would then report two diverging machines as one.
        So `state_suite` **sweeps every field individually**: perturb it, and
        the hash must change. A field added without a sweep entry is a gap
        visible in that file rather than one nobody can see.
        *Verification: `state_suite`, 10 tests — two identically built machines
        at different addresses agreeing, which is where a leaked host pointer
        would show; the register, processor, MMU, pipe, cache and ATC sweeps;
        the two root pointers and the two TTx registers told apart from each
        other; an invalid ATC entry's history bit; and the clock. `step_suite`,
        3 further tests (161 total) — **the same program run twice hashing the
        same at every step**, which is the property the harness rests on; the
        hash moving as the run proceeds, since one that never changed would
        satisfy that perfectly and detect nothing; and two runs with identical
        registers separated only by their clock.*
  - The device and bus parts stay open until there are devices and a bus. That
    is a Phase 3 tail, not something to fake now.

## Phase 2 — CPU family

Build the 68030 first (DN3500 is the superset), then subset and extend.

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
        This is the mechanism §11.3.3 averages over when it publishes a
        no-cache-case time, so modelling it is what lets this core produce the
        per-run number instead of the published mean.
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
        **A7 names one of three registers**, and in user state M is *ignored*
        rather than required to be zero: the PRM's table reads `S 0, M x → USP`,
        `1 0 → ISP`, `1 1 → MSP`. Switching on the pair as four cases invents a
        fourth stack. This is the 68020-and-later addition — on the 68000 "the
        M-bit is always zero" and there is one supervisor stack — so it is
        precisely what a 68000-shaped model gets wrong.
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
        **The table's overbars do not survive the scan** — `HI` reads as "C^ Z"
        where the manual means not-C and not-Z — so this is the first table
        whose *content* is damaged rather than its layout. It is recovered from
        structure rather than guessed: the encoding lays the conditions out in
        complementary pairs (`2k` against `2k+1`), so a misplaced bar must make
        some pair agree for some CCR value.
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
        **The MOVE families are not in size order**: `0001` byte, `0010`
        **long**, `0011` **word**. Assuming byte/word/long yields a decoder that
        runs and moves the wrong number of bytes for two thirds of all MOVE
        instructions, which is why the size is exposed here rather than left for
        each caller to re-derive — there is no arithmetic on the family number
        that produces it.
        Families `1010` and `1111` are named rather than lumped in as invalid,
        because they are exception *generators*: `[030]` Table 8-1 gives vector
        10 to the "Line 1010 Emulator" and 11 to the "Line 1111 Emulator", which
        is what lets an OS emulate an absent coprocessor in software. Those
        vector numbers come from `ap_m68030_exception.h`, so the two modules
        agree by construction rather than by two copies of a constant.
        *Verification: `opcode_suite`, 6 tests — the whole published map with
        the low twelve bits varied to prove they do not influence the family,
        the move sizes asserted as sizes rather than names, and both emulator
        families resolving to the exception module's own vectors.*
  - [x] **Branch family decode** (`src/core/cpu/m68030/ap_m68030_branch.c`),
        family `0110`, the first per-family decoder and the one that consumes
        `ap_m68030_cond`. `M68000 Family PRM` §8.2 and the Bcc/BRA/BSR pages.
        **The branch base and the BSR return address are different addresses.**
        The PRM gives the branch as "PC + dn → PC" with PC the instruction
        address *plus two*, while BSR pushes "PC" meaning the instruction that
        follows — a whole instruction length away. They coincide only for the
        8-bit form, which is exactly why the mistake survives casual testing:
        computing the return address as the base gives a BSR that returns into
        its own displacement words for the 16- and 32-bit forms.
        `$00` and `$FF` are escapes rather than displacements, so the 8-bit
        field cannot encode 0 or −1 — and the manual's own NOTE gives the
        visible consequence: "A branch to the immediately following instruction
        automatically uses the 16-bit displacement format".
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
        Neither it nor `TRAPcc` is a special case bolted on; both are holes in
        `Scc`'s own address space.
        Two traps, each tested. The quick data field's **zero means eight** —
        a decoder passing it through turns every add-8 into an add-0, an
        instruction that runs, sets condition codes and does nothing. And
        `DBcc` terminates on **−1 after decrementing**, so a starting count of
        zero decrements to `$FFFF` and stops after one pass rather than
        wrapping to 65535; zero is explicitly *not* the terminator, which is
        asserted directly.
        *Verification: `quick_suite`, 10 tests.*
  - [~] **Family `0100` ("Miscellaneous")**, the largest in the map. Being taken
        as coherent subtrees rather than claimed whole in one pass.
    - [x] **The `$4E` control group**
          (`src/core/cpu/m68030/ap_m68030_control.c`): TRAP, LINK, UNLK, MOVE
          USP, RESET, NOP, STOP, RTE, RTD, RTS, TRAPV, RTR, JSR and JMP. The
          subtree narrows in stages rather than by one field — bits 7-6 take
          JSR and JMP with a six-bit effective address between them, and `01`
          opens the control group where bits 5-3 choose the rest.
          **TRAP's four-bit field is an index, not a vector number**: Table 8-1
          puts TRAP #0-15 at vectors 32-47, so returning the field itself sends
          TRAP #0 to the reset vector. The vector comes from
          `ap_m68030_exception.h`, so the two modules agree by construction.
          **Four are privileged** — RESET, STOP, RTE and both directions of MOVE
          USP — and this is a class whose failure mode is silent: a user program
          able to execute them could halt the processor or forge a return from
          exception. The test states the distinction that matters, that RTS and
          RTR are ordinary while RTE is not.
          *Verification: `control_suite`, 10 tests — the `$4E70`-`$4E77` run
          checked as a whole so a transposition inside it cannot pass, all
          sixteen TRAP numbers, LINK/UNLK and MOVE USP splitting on bit 3,
          JSR/JMP effective addresses, only LINK/RTD/STOP carrying a following
          word, and `$4E78`-`$4E7F` being unassigned rather than aliased.*
    - [x] **The LEA/CHK and `$48`/`$4C` subtree**
          (`src/core/cpu/m68030/ap_m68030_misc.c`): LEA, CHK.W, CHK.L, PEA,
          SWAP, BKPT, EXT.W, EXT.L, EXTB.L, NBCD and MOVEM.
          **The same trick appears three more times here**, and getting the
          decode *order* wrong is how it bites: an instruction that takes only
          some addressing modes leaves holes, and other instructions live in
          them. `PEA` cannot push a register, so mode `000` there is `SWAP` and
          `001` is `BKPT`. `MOVEM` moves registers to or from memory, so mode
          `000` is `EXT`. `LEA` loads an address, so mode `000` under its opmode
          is `EXTB.L`. In each case the register-direct form must be recognised
          *before* falling through, and a decoder checking the wider instruction
          first produces a working instruction doing the wrong thing.
          `EXT`'s encoding also fixes bits 11-9 at `100`, which is `MOVEM`'s
          registers-to-memory direction — the other direction has no `EXT` at
          all, so a data register operand there is invalid rather than a third
          `EXT` form.
          *Verification: `misc_suite`, 11 tests, including each hole against the
          instruction whose space it sits in, and the `MOVEM` direction that has
          no `EXT`.*
    - [x] **The single-operand group**
          (`src/core/cpu/m68030/ap_m68030_single.c`): NEGX, CLR, NEG, NOT, TST,
          TAS, `MOVE to/from SR`, `MOVE to/from CCR` and ILLEGAL. **Family
          `0100` is now complete.**
          Size field `11` is an escape rather than a size, and what it escapes
          *to* differs per row — `$40C0` is `MOVE from SR`, `$42C0` `MOVE from
          CCR`, `$44C0` `MOVE to CCR`, `$46C0` `MOVE to SR`, `$4AC0` `TAS`. So
          the bit pattern meaning "long" one row up means an entirely different
          instruction here. That is the third distinct place in the encoding
          where an illegal size selects something else, after `ADDQ`'s
          conditional group and `Bcc`'s displacement escapes, and it is worth
          treating as a family idiom rather than five special cases.
          `ILLEGAL` (`$4AFC`) is a *defined* word inside TAS's range, not an
          absence of one — its purpose is to take the illegal instruction
          exception.
          Privilege reads backwards and is tested as such: `MOVE to SR` is
          privileged because it writes the S bit, `MOVE to CCR` is not; and
          `MOVE from SR` became privileged on the 68010 (a user program that can
          read S learns whether it is supervised) while `MOVE from CCR`, which
          the 68000 lacked, is unprivileged.
          *Verification: `single_suite`, 7 tests.*
    - [x] **Correction: `$4E7A`/`$4E7B` are MOVEC, not unassigned.** The `$4E`
          control group first landed with the whole `$4E78`-`$4E7F` run decoded
          as invalid, and a test that asserted it. That would have made every
          `MOVEC` an illegal instruction — and with it `VBR`, `CACR` and the MMU
          root pointers unreachable, since `MOVEC` is the only way to load them.
          Both the decoder and the test that endorsed the error are fixed, and
          MOVEC is privileged along with the rest.
  - [x] **MOVE and MOVEA decode** (`src/core/cpu/m68030/ap_m68030_move.c`),
        families `0001`, `0010` and `0011` — one per operand size.
        **The destination field is reversed**: source is `MODE` then `REGISTER`
        as everywhere else, destination is `REGISTER` then `MODE`. A decoder
        reading them the same way round gets a plausible wrong instruction
        rather than a fault, and the suite pins the reversal in a single
        comparison — the same bits are the immediate mode at `$29C0` and
        absolute short at `$21C0`.
        The size comes from `ap_m68030_opcode_move_size` rather than a second
        table, because these bits *are* the low half of the family number; a
        local copy could disagree with the map.
        `MOVEA` is not a separate encoding but the one destination mode that
        changes behaviour — it does not affect the condition codes, which is why
        it is worth distinguishing — and there is **no byte MOVEA**, so a
        byte-sized address register destination is not an instruction.
        *Verification: `move_suite`, 8 tests.*
  - [x] **Family `0000` decode** (`src/core/cpu/m68030/ap_m68030_immediate.c`):
        ORI, ANDI, SUBI, ADDI, EORI, CMPI, MOVES, the static and dynamic bit
        operations, MOVEP, and the `to CCR`/`to SR` forms. Bit 8 splits the
        family — clear selects an immediate row, set makes the bit number
        dynamic.
        **MOVEP is the strongest instance yet of this encoding's idiom.** It
        uses *the same four opmodes* as the dynamic bit operations and is
        separated only by the effective address mode: a bit operation cannot
        address an address register, so mode `001` there is MOVEP. The overlap
        is **total** rather than partial — there is no opmode that is MOVEP and
        not also a bit operation — which the suite checks by walking all four.
        The `to CCR`/`to SR` forms are likewise not separate opcodes but the
        immediate-*destination* encoding, meaningless as an address, with the
        size field choosing byte (CCR) or word (SR). SUBI, ADDI and CMPI have no
        such forms, so for them that encoding stays unassigned rather than
        aliasing — also tested.
        *Verification: `immediate_suite`, 10 tests.*
  - [x] **The arithmetic and logic families**
        (`src/core/cpu/m68030/ap_m68030_arith.c`): `1000` OR/DIV/SBCD, `1001`
        SUB/SUBX, `1011` CMP/EOR, `1100` AND/MUL/ABCD/EXG, `1101` ADD/ADDX.
        Five families with one shape, which is why they are one module —
        `family | register | opmode | effective address`, with opmodes `000`-
        `010` the register direction, `100`-`110` the memory direction, and
        `011`/`111` the wide forms.
        **The wide opmodes share a position but not a meaning**: `011` is a word
        `DIVU` in family `1000`, a word `MULU` in `1100`, and a word `SUBA`,
        `CMPA` or `ADDA` in the other three. The suite asserts all five at the
        same opmode, since a decoder assuming one shape for all of them gets
        four families wrong.
        The memory-direction opmodes leave register-direct **holes**, filled
        differently per family — SUBX, ADDX, ABCD, SBCD, CMPM and EXG — the same
        idiom as SWAP inside PEA, now in five families at once. Tested against
        the ordinary instruction at the same opmode with a real memory mode, so
        the holes are shown to be holes rather than special cases.
        `CMP` and `EOR` share family `1011` **without overlapping**: CMP has the
        register direction, EOR the memory one, and there is no
        `<ea> EOR Dn -> Dn` form at all — asserted as the absence it is.
        *Verification: `arith_suite`, 9 tests.*
  - [x] **Family `1110` decode** (`src/core/cpu/m68030/ap_m68030_shift.c`):
        shifts, rotates and the 68020's bit field instructions. **This completes
        the integer opcode map** — every family from `0000` to `1110` now
        decodes, with `1010` being the Line A trap the map itself handles.
        One family, three shapes. Bits 7-6 other than `11` is a register shift;
        `11` is not a size, and bit 11 then chooses between a memory shift (one
        word, by one) and a bit field instruction. That is the *fourth* place in
        the encoding where an illegal size selects something else.
        **The type field moves between the two shift forms**: bits 4-3 in the
        register form, bits 11-9 in the memory form — where the register form
        keeps its shift count. Reading one position for both gives a working
        shift of the wrong kind, so the suite checks all four types in each
        position and asserts that the same bits are a *count* in one form.
        The immediate count's **zero means eight**, the same quirk as `ADDQ`'s
        quick data — but only when `i/r` is clear; with it set the field is a
        register number where zero means register 0, so the substitution must
        *not* happen. Both directions tested.
        *Verification: `shift_suite`, 10 tests.*
  - [x] **Family `1111` decode** (`src/core/cpu/m68030/ap_m68030_coproc.c`):
        the coprocessor interface, and with it **the operation code map is
        complete** — every family from `0000` to `1111` now decodes.
        cpID 0 is the 68030's *own MMU*: "The MMU instructions use the same
        opcodes and coprocessor identification (CpID) as the corresponding
        instructions of the MC68851", so `PMOVE`, `PTEST` and `PFLUSH` are
        F-line words — which is how the MMU registers this project already
        models get written. The 68882 sits at a different ID alongside.
        **The same instruction word takes different vectors depending on
        privilege**, which is unusual enough to be the module's headline:
        an unsupported cpID-0 word is an F-line exception (vector 11) from
        supervisor state and a **privilege violation** (vector 8) from user
        state. Almost everywhere else in this architecture the exception a word
        takes is a property of the word alone. Reporting F-line in both cases
        would let a user program distinguish "unimplemented" from "not
        allowed" — exactly what the privilege violation exists to prevent.
        *Verification: `coproc_suite`, 6 tests, including the two vectors from
        one word and the rule holding for cpID 0 against all seven others.*
  - [x] **Decode dispatcher** (`src/core/cpu/m68030/ap_m68030_decode.c`) and
        MOVEQ, the one family that had no decoder. Given any 16-bit word: which
        family claims it, and what that family made of it. Family `0100`'s three
        subtrees are tried in the order their encodings nest — the `$4E` group
        first (a fixed top byte), then LEA/CHK and `$48`/`$4C` (bit 8 set), then
        the single-operand group (bit 8 clear) — and that ordering is stated in
        the header rather than left to be rediscovered.
        A word no family claims is reported **illegal rather than absorbed by a
        fallback**, which is the failure mode every family module was written to
        avoid.
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
        Table 2-3 is the part worth stating: a **byte immediate still occupies a
        whole extension word** ("Low-order byte of the extension word"), so byte
        and word both cost one and only long costs two. The indexed modes cost
        their own extension word *plus* whatever base and outer displacements it
        declares — one word for the brief format, up to five in total for the
        widest full format.
        *Verification: `ea_suite`, 6 further tests (17 total).*
  - [x] **Total instruction length** (`ap_m68030_instruction_length`), joining
        `ap_m68030_ea_words` to each family's own extension words. **The PC can
        now advance.**
        **MOVE needs two extension words, and the second is at a variable
        offset.** It is the only instruction with two effective addresses, and
        the source's extension words come first — so the destination's is not at
        a fixed position but after however many words the source took, which the
        caller cannot know until it has sized the source. Hence two parameters,
        with the second documented as "the word following the source's
        extensions" rather than "the second word of the instruction".
        Zero means *cannot be sized* — an illegal encoding, or a coprocessor
        instruction whose format varies by coprocessor and is declined rather
        than guessed. Zero is never a real instruction length, so it cannot be
        mistaken for one.
        *Verification: `decode_suite`, 10 further tests (17 total), including a
        second full 65536-word sweep asserting every sizeable instruction has an
        even, non-zero length — instructions are whole words, and that is the
        check no individual case can make.*
  - [x] **Effective address calculation**
        (`src/core/cpu/m68030/ap_m68030_addr.c`): decoded fields into an
        address, with the increment modes' register side effects applied.
        **A7 is not an ordinary address register.** "If the address register is
        the stack pointer and the operand size is byte, the address is
        incremented by two to keep the stack pointer aligned to a word
        boundary", and likewise decremented. A model that misses this keeps
        running — the stack merely drifts odd, and every later word or long
        access to it is misaligned, which on a 68030 does not fault. The symptom
        is silent corruption a long way from the cause, so it is the one rule in
        the module worth knowing by heart. Tested in both directions, against
        an ordinary register doing the opposite, and across the privilege
        switch so it follows whichever of the three stacks A7 currently names.
        The PC-relative modes are relative to the **extension word**, not the
        instruction word and not the next instruction — the same base `Bcc`
        uses.
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
        **A cache hit does not consult the MMU at all**: "Whenever a read access
        occurs and the required instruction word or data operand is resident in
        the appropriate on-chip cache (no external bus cycle is required), the
        MMU is completely ignored ... The MMU is used to validate all accesses
        that require external bus cycles." That is only possible because the
        68030's caches are **logically** addressed — their tag is the logical
        address with the function code — so the cache can answer before anything
        has been translated.
        Two consequences that look like bugs and are not: a page's protection is
        **not** checked on a cache hit, and the MMU's CI bit is irrelevant there,
        because the MMU is not asked. A model that translates first and then
        looks in the cache produces the same values with the wrong timing *and*
        the wrong faults.
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
        **A write to a page that has only been read costs a table search the
        read did not**, which is §9.4's consequence visible end to end: an ATC
        entry created by a read has M clear, and a write to it "aborts the
        access and initiates a table search". Paid once — a second write to the
        same page is an ordinary hit.
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
        **Half of all sequential prefetches are free, and which half depends on
        alignment.** The holding register holds one *long word*, so a prefetch
        of its odd half needs no bus cycle and no cache access. Four sequential
        words therefore cost **two** fetches from a long-word-aligned start and
        **three** from an odd one — no single number describes both, which is
        precisely why the manual publishes an average and why
        `docs/references/M68030_TIMING.md` says the published figure "is not a
        value any single execution ever takes". That claim is now produced by
        the memory path rather than asserted.
        *Verification: `fetch_suite`, 5 tests — the second word of a long word
        costing nothing, the two-against-three count, the words reaching stage D
        in order so the saving is real rather than a dropped fetch, a re-fetch
        hitting the instruction cache, and a pipe reset discarding the holding
        register so a branch target's neighbour is not wrongly free.*
  - [~] **The instruction step** (`src/core/cpu/m68030/ap_m68030_step.c`):
        fetch through the pipe and instruction cache, decode, execute, advance
        the PC, account the clocks. **A program runs.**
        Executing today: `NOP`, `MOVEQ`, and the 8-bit forms of `BRA` and
        `Bcc` — the instructions needing no operand access beyond the
        instruction word.
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
        *Verification: `step_suite`, 10 tests — `MOVEQ` sign-extending `$FF` to
        −1 and setting exactly the documented condition codes (with X asserted
        to *survive*), a four-instruction program running to its end, `BRA`
        landing on its target and the instruction there being the expected one,
        a conditional branch reading the flags the previous instruction set —
        the first interaction between two instructions — an unimplemented
        instruction reported rather than skipped, an illegal encoding distinct
        from it, and a second pass over the same code costing **zero** clocks
        because the instruction cache answers.*
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
        The source's extension words are consumed before the destination's,
        which is the ordering `ap_m68030_instruction_length`'s two parameters
        exist to describe and this is where it is actually performed.
        *Verification: `step_suite`, 6 further tests (22 total) — a long
        immediate as two words high-half-first; a byte immediate taking the
        *low* half of a whole word, Table 2-3 seen in running code; a negative
        displacement source; an absolute long destination; `(xxx).W`
        sign-extending so `$8000` addresses the top of memory; and both operands
        taking their words **in order**, checked by giving them different
        displacements so a swapped read produces the wrong address.*
  - [x] **The MMU instructions**, which are how every MMU register and the ATC
        this project already models actually get driven: `PMOVE`, `PFLUSH`,
        `PFLUSHA`, `PLOAD` and `PTEST`.
        **`PFLUSH` and `PLOAD` share the extension prefix `001`** and are told
        apart by the MODE field below it — PFLUSH's modes are `001`, `100` and
        `110`, and PLOAD is `000`. A decoder stopping at the prefix does the
        *opposite* of what was asked: PLOAD adds a translation where PFLUSH
        removes one.
        **The flush mask says which bits must agree, not which codes to flush.**
        "Each bit in the mask that is set to one indicates that the
        corresponding bit of the FC operand applies", so a *zero* mask selects
        every function code rather than none. Read the other way,
        `PFLUSH #0,#0` becomes a no-op where the hardware flushes everything.
        **The FC field is not a plain number**: `10XXX` is an immediate code,
        `01DDD` is *data register DDD's* low three bits, `00000` is SFC and
        `00001` DFC. Reading the low three bits directly makes `01DDD` name a
        function code that happens to be the register number — for D5 that is 5,
        an ordinary supervisor data code, so nothing looks wrong.
        **`PFLUSH` and `PLOAD` use the calculated address as the operand**, never
        reading through it: "The address field must provide the memory
        management unit with the effective address to be flushed ... not the
        effective address describing where the PFLUSH operand is located."
        `PTEST` at level 0 probes the ATC and at levels 1–7 walks the tree with
        a NULL history-update callback — which is what `ap_m68030_walk`'s
        nullable `update` was built for — so it "does not alter the ATC" and
        does not disturb the tree either. That is what makes it usable inside a
        fault handler without changing the state being diagnosed. A level-0
        `PTEST` with the A bit set is an F-line exception, since an ATC probe
        never fetched a descriptor whose address it could return.
        `ap_m68030_walk_result_t` gained `last_descriptor_address` for that
        return value, and `ap_m68030_atc` gained a mask-based flush.
        *Verification: `step_suite`, 8 further tests (139 total) — `PFLUSHA`
        clearing entries of differing function codes; the mask selecting by
        agreement, with a partial mask taking every supervisor code and leaving
        the user ones; a flush by address leaving the neighbouring page; a
        function code from a data register that is not the register's number;
        `PLOAD` adding where `PFLUSH` removes; `PTEST` level 0 in both the
        resident and non-resident cases; the level-0-with-A combination refused;
        and a table-search `PTEST` succeeding while leaving the ATC empty and
        returning the descriptor's own address.*
        **`PMOVE` has three instruction formats, told apart by the extension
        word's top three bits**, and the P-REGISTER field below them is not
        enough on its own: `010` names the *supervisor root pointer* under
        prefix `010` and *TT0* under prefix `000`. A decoder reading only
        P-REGISTER writes a transparent translation register where a root
        pointer belongs — and both hold plausible 32-bit values, so nothing
        faults until a translation goes somewhere strange.
        Sizes differ per register: quad for the root pointers, long for TC and
        the TTx pair, word for the status register.
        **Two writes fault *after* landing.** An invalid root pointer descriptor
        type and an inconsistent TC both take an MMU configuration exception
        "after moving the operand", and TC additionally has its E bit cleared.
        Refusing the write instead would leave the operating system unable to
        see what it wrote wrong. The invalid-root case also forced a fix: the
        long-descriptor unpack stops early on DT zero, so the root pointer's
        table address is taken from the lower long word directly, per Figure
        9-35's "Bits 3-0 of the root pointer are not used and are ignored when
        written".
        **The FD bit makes `PMOVEFD` a different instruction in one bit** —
        "If the FD bit equals one, the ATC is not flushed" — and the status
        register's format carries no FD bit at all, so flushing is not something
        a write to it does.
        The MMU registers now live on the CPU rather than in whatever the caller
        supplied, because there is one MMU and two access paths through it; a
        caller wanting translation to follow a `PMOVE` points both access
        contexts at them. The root pointers are unpacked by the *walk's* own
        long-descriptor code — "The field descriptions in the preceding section
        apply to corresponding fields of the CRP and SRP" — so the two cannot
        drift apart, and `ap_m68030_root_pack_upper` inverts it beside it.
        *Verification: `step_suite`, 7 further tests (131 total) — a TC round
        trip through memory; the same P-REGISTER field reaching two different
        registers under two prefixes, each leaving the other alone; an invalid
        root pointer faulting with the address already landed; an inconsistent
        TC landing with `E` cleared; the ATC surviving `PMOVEFD` and not
        surviving `PMOVE`; an MMU instruction in user state taking a privilege
        violation rather than F-line; and a register-direct operand refused,
        since only control alterable modes are legal.*
  - [x] **Full-format indexed addressing and the memory indirect modes**. The
        extension word declares its own base and outer displacement sizes, so
        the number of words to read is not known until that word has been read —
        and neither, therefore, is the instruction's length.
        **So the PC no longer advances by a predicted length.** It advances by
        the instruction word plus however many extension words the step actually
        took, which makes the fetch and the PC agree by construction rather than
        by two calculations matching. `ap_m68030_instruction_length` remains the
        decoder-level answer for anything disassembling rather than executing;
        the step had been calling it with zeroed extension words, which silently
        means "brief format".
        **The two memory indirect modes differ in where the index goes and in
        nothing else** — `([bd,An,Xn],od)` against `([bd,An],Xn,od)` — so the
        address calculation now reports the intermediate address *without* the
        index for the postindexed mode and carries what is still owed in
        `post_indirection`. Indexing in both places, or in neither, lands a
        scaled register away, and for a small index that is a *nearby* address.
        The indirection itself is performed in the step, not the calculation:
        "The processor accesses a long word at this address", and the bus
        belongs there. One resolver now sits in front of every address the step
        computes, so no call site can forget it.
        A **reserved** base displacement size is refused rather than treated as
        null, as `ap_m68030_ea.h` always insisted.
        *Verification: `step_suite`, 5 further tests (124 total) — a full-format
        base and index reaching an operand with the following instruction still
        running, which is the length check; the preindexed and postindexed forms
        on the **same** registers and displacements so only the placement can
        account for the difference; a suppressed base contributing zero rather
        than its register; the scale applied before the addition; and a reserved
        displacement size declined with the PC unmoved.*
  - [x] **Integer ALU** (`src/core/cpu/m68030/ap_m68030_alu.c`): ADD, SUB, CMP,
        AND, OR and EOR, with their condition codes.
        **Table 3-18's overbars are lost to the scan**, exactly as Table 3-19's
        were — `ADD`'s overflow reads as "V = Sm Λ Dm Λ Rm V Sm Λ Dm Λ Rm",
        whose two halves are identical as written and therefore unreadable. So
        the formulas are *not* transcribed. They are written in the standard
        equivalent form and then **verified exhaustively**: all 65536 byte
        operand pairs for both add and subtract, against references computed
        independently in wider arithmetic where no truncation can hide a carry.
        A misplaced overbar cannot survive that, and neither can a formula that
        is right everywhere except one boundary.
        *Verification: `alu_suite`, 8 tests — the two exhaustive sweeps; word
        and long cases that would catch a hardcoded byte sign bit; all four
        combinations of carry and overflow shown reachable, since a single
        "did it fit" flag would lose the distinction; `CMP` differing from `SUB`
        **only** in X; and X left alone by the logical operations against being
        replaced by the carry in the arithmetic ones.*
  - [x] **The ALU wired into the step**: `ADD`, `SUB`, `CMP`, `AND`, `OR` and
        `EOR` execute in both directions, over every addressing mode extension
        words reach.
        **The direction bit decides which operand is the destination**, and with
        it which way round a subtraction goes — `SUB.L D0,D1` is `D1 - D0`.
        Reversing it merely negates the result, which looks almost right, and
        inverts the carry in a way that only shows up in a later conditional
        branch. Tested in both directions and at the borrow boundary.
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
        `NEG` is implemented as `0 - destination` rather than as a second
        formula, because Table 3-18 gives it "V = Dm Λ Rm, C = Dm V Rm" — which
        is exactly what subtracting from zero produces, so reusing the
        subtraction means the two cannot drift apart. `NOT` is a *logical*
        operation for condition code purposes despite looking arithmetic: V and
        C cleared, X untouched.
        *Verification: `step_suite`, 11 further tests (41 total) — including
        `SUBI` subtracting *from* the destination rather than the reverse,
        `CMPI` and `TST` writing nothing, byte forms leaving a register's upper
        bytes intact through a third and fourth code path, `NEG` borrowing for a
        non-zero operand and **not** for zero (the boundary the rule turns on),
        and **a countdown loop that terminates** — five instruction kinds
        cooperating, and the first program here whose control flow repeats.*
  - [x] **`ADDQ`, `SUBQ`, `Scc` and `DBcc` execute.**
        **ADDQ to an address register is a double special case**, and both
        halves are silent when missed: "the condition codes are not altered, and
        the entire destination address register is used regardless of the
        operation size". So `ADDQ.W #1,A0` changes all 32 bits *and* leaves the
        flags alone — which is what lets a pointer be bumped inside a loop
        without clobbering the comparison the loop branches on. Both halves are
        tested, against a data register destination that *does* set the flags.
        `Scc` writes **all ones**, not one, which is what makes its result
        usable directly as a mask. `DBcc` decrements only the **low word**, so a
        loop counter cannot borrow into the register's upper half.
        *Verification: `step_suite`, 10 further tests (51 total), including a
        `DBcc` loop running the documented number of times — a count of three
        runs the body four times, three decrements that branch and a fourth that
        reaches −1 and falls through.*
  - [x] **The address-register forms and the bit operations execute.**
        A word A-form is **not a word operation**: the source is sign-extended
        and the whole register takes part, so `ADDA.W` with a negative operand
        subtracts from the full address rather than wrapping in its low half.
        `ADDA` and `SUBA` alter **no** condition codes — an address calculation
        must not disturb the flags a following branch depends on — while `CMPA`
        does, which is the whole reason a compare against an address register is
        a separate instruction.
        For the bit operations the operand size comes from the **destination
        kind**, not an encoding field: a data register is a *long* operation
        with the bit number modulo 32, memory is a *byte* operation modulo 8. A
        model picking one width addresses the wrong bit for half of all uses,
        silently. And Z reflects the bit as it was **before** the operation, so
        `BSET` on an already-set bit clears Z — testing after the write inverts
        it.
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
        **Only the arithmetic *left* shift sets V**, and it is set "if the most
        significant bit is changed at **any time** during the shift" — not if
        the sign differs at the end. A value whose sign shifts out and back in
        sets V despite finishing as it started, which is tested directly.
        **The rotates split on X**: `ROL`/`ROR` leave it alone, `ROXL`/`ROXR`
        rotate *through* it. Treating all four alike breaks multi-precision
        shifts, which are why the extend forms exist — verified by a nine-step
        `ROXL` on a byte returning both operand and extend bit to their starting
        values, against an eight-step `ROL` doing the same.
        *Verification: `alu_suite`, 9 further tests (17 total); `step_suite`, 3
        more (64), including a register count taken modulo 64 so a count of 64
        shifts by nothing.*
  - [~] The last of the instruction semantics: divides and multiplies, and the
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
        `ABCD`/`SBCD`'s **`N` and `V`, which the manual documents as
        undefined**. A reference
        core must still be deterministic, so `N` is taken from bit 7 and `V`
        cleared, marked `PROVISIONAL` in code; nothing correct may depend on
        either, and an oracle probe would settle what the part actually does.
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
        **The two halves count their sizes differently.** `CMP2`/`CHK2` use the
        family's ordinary "00 Byte, 01 Word, 10 Long"; `CAS` uses "01 Byte,
        10 Word, 11 Long", one higher throughout. The same three bits in the
        same position mean a byte in one half and a word in the other, so a
        decoder reading the size once for the whole escape gives every `CAS` the
        wrong operand width, silently. The unassigned value moves with it: `11`
        for `CMP2`/`CHK2`, `00` for `CAS`.
        **And `CAS` size `00` is not merely unassigned — it is the static bit
        operations.** `BSET #n,(A0)` is `$08D0`, which has the escape's shape
        exactly: family `0000`, bit 8 clear, bits 7-6 reading `11`. So the two
        subtrees interleave at one point, and a decoder that stops at the escape
        turns every static `BTST`/`BCHG`/`BCLR`/`BSET` into an illegal
        instruction — which is how this was found, three suites going red at
        once. The escape declines and the decode falls through.
        **`CMP2` and `CHK2` are separated by the extension word**, not the
        instruction word — "identical to CHK2 except that it sets condition
        codes rather than taking an exception" — so the decode reports the pair
        and a second call resolves it, the same two-stage shape the indexed
        addressing modes need. **`CAS2` hides behind the immediate mode's
        encoding**, `111100`, which `CAS` cannot use because its operand must be
        memory alterable.
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
        Two more were found. The **arithmetic forms' register direction** takes
        an immediate source — "If the location specified is a source operand,
        all addressing modes can be used" — so `ADD.W #$10,D0` in family `1101`
        is a real instruction, distinct from the `ADDI` that assembles to the
        same thing, and it was declining. And **`TST #<data>`** is marked
        "MC68020, MC68030, MC68040, and CPU32" on its page: the 68000 had no
        such form, which is exactly why a 68000-shaped model refuses it.
        The remaining sites are correct by category rather than by accident: the
        `100`-`110` opmodes, `MOVE`'s destination, `Scc`, the memory shifts, the
        MMU instructions and `JMP`/`JSR` all take categories an immediate is not
        in, and the addressing mode category module now says so.
        *Verification: `step_suite`, 3 further tests (158 total) — an immediate
        source accepted in two different families with the following
        instruction still running, which is the length check; the memory
        direction refusing an immediate destination; and `TST` in both the zero
        and negative cases.*
  - [x] **Addressing mode categories** (`src/core/cpu/m68030/ap_m68030_category.c`),
        which decide whether a decoded mode is *legal* for an instruction.
        **Table 2-4 is not transcribed**, because its Alterable column does not
        survive the scan — and it fails in a way that is not a plausible
        reading. The extraction gives absolute addressing `—` and Program
        Counter Memory Indirect `X`, which is the truth of both rows exchanged;
        the same rows also give Absolute Long a register field of `000` where
        `MOVEM`'s, `PMOVE`'s and `PFLUSH`'s own tables all give `001`.
        So the table is **derived** from §2.3's four definitions, which survive
        intact: data is everything but `An`; memory is everything but the two
        register direct modes; control is memory *without an associated size*,
        so not the increment modes and not the immediate; alterable is
        everything but the PC-relative modes and the immediate. The derivation
        agrees with every surviving cell.
        Two independent checks confirm the exchanged column: `MOVE.W D0,$1234`
        is a legal instruction and `MOVE`'s destination must be data alterable,
        so absolute *is* alterable; and nothing PC-relative is a legal `MOVE`
        destination, so the PC modes are not.
        Applied where it was already being approximated: the MMU instructions
        had "not a register and not an immediate", which let `(An)+`, `-(An)`
        and every PC-relative mode through. `LEA` and `PEA` take control modes,
        `JMP`/`JSR` take control modes, `MOVE`'s destination must be data
        alterable and `NBCD`'s operand too, and `CHK` takes any data mode
        including the immediate.
        **The check must precede the address calculation**, which was a real
        defect the first version had: the calculation applies the increment and
        decrement side effects, so a refusal that came afterwards had already
        moved the register. An instruction the processor refuses must leave no
        trace.
        *Verification: `category_suite`, 8 tests — each of the four definitions
        swept over every mode, plus the two independent cross-checks against
        `MOVE`'s and the MMU instructions' own pages, `LEA` admitting the
        PC-relative modes (which is what makes position-independent code
        possible) while excluding the increments, and an invalid mode belonging
        to no category. `step_suite`, 3 further tests (142 total) — `LEA (A0)+`
        refused **with A0 unmoved**, `MOVE.W D0,(d16,PC)` refused with nothing
        stored, and `PMOVE (A0)+,TC` refused.*
  - [ ] **Instruction execution time — the microcode clocks between the bus
        cycles.** Named here because it was missing from this plan entirely,
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
    - [~] **Wiring the figures in is a *scheduling* problem, not an addition**,
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
          Adding each instruction's `CC` to the bus time the core accumulated
          was implemented and backed out: the tables contradict it.
          `ADD Rn,Dn` is `CC 2(0/0/0)` and `NCC 2(0/1/0)` — one more instruction
          bus cycle, **the same total** — so that prefetch cost nothing, having
          happened while the microcode ran. `ADD Dn,EA` is `CC 3(0/0/1)` against
          `NCC 4(0/1/1)`, where the extra prefetch costs *one* clock rather than
          zero or two. How much of a fetch is hidden depends on how much
          execution there is to hide it in, which is §11.2's "eight
          independently scheduled resources ... very little of the scheduling is
          directly related to instruction boundaries".
          **The target is now two-sided and needs no oracle**: for any
          transcribed row, a cold-cache run must come to that row's `NCC` and a
          warm-cache run to its `CC`. Two published numbers bracketing the same
          execution. A model satisfying both schedules the resources correctly;
          one satisfying neither is adding where it should overlap.
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
          The table's own markers are kept rather than flattened. The four
          divides carry `+`, "Indicates Maximum Time (Actual time is data
          dependent)", and are **`PROVISIONAL`**: using 56 clocks for every
          `DIVS.W` would be slow by a data-dependent amount rather than wrong by
          a fixed one, which is much harder to notice. Closing that needs
          measurement per operand pair.
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
          The pairing is **directional** — the *following* instruction's head
          against the *preceding* instruction's tail — and reversing it reads
          plausibly, costs nothing on any single instruction, and only shows up
          on a sequence whose entries have asymmetric heads and tails, which is
          most of them. A test picks a pair where the two orders differ.
          **Zero net execution time is a documented outcome**: "the heads of
          some instructions equal the total instruction-cache-case time for
          those instructions makes a zero net execution time possible". A model
          clamping every instruction to at least one clock would be wrong in the
          direction that *hides* a fast mode's error — it would make the
          reference core slower than the hardware, so a fast mode that skipped
          work would look closer to correct rather than further from it.
          Head and tail compose only with CC; §11.3.3 says they do not apply to
          NCC, so feeding no-cache figures through this rule would subtract a
          saving the published number already excludes.
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
  - [~] **Effective address times, §11.6.1–§11.6.5**, and composing them
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
          **Which of the two an instruction uses is load-bearing**, not a
          formality: `(An)` is `3(1/0/0)` to fetch and `2(0/0/0)` to calculate,
          the difference being the operand read. Using the wrong table costs or
          saves a memory access, which is the size of error C9 measured.
          Two notations are carried rather than flattened. The register-direct
          rows give head and tail as **`-`**, not 0 — there is no address
          computation to overlap *with*, which is a different statement from an
          overlap of zero. And the calculate table writes several heads as
          **"2+op head"**: the head is the *operation's* head plus a figure, the
          table expressing a dependency between the two halves of Equation
          (11-2). Flattening that to 2 would drop whatever the operation
          contributes.
          The calculate table has **no immediate row**, and the lookup returns
          absent rather than zero: an operand in the instruction stream has no
          address to compute, so zero would read as "free" rather than as "not a
          thing".
          *Verification: `ea_timing_suite`, 8 tests — a register operand costing
          nothing either way and its head marked inapplicable; fetching never
          costing less than calculating, for every mode, which a swapped
          transcription would break everywhere at once; the two tables differing
          by exactly the operand read; the relative heads distinguished from the
          plain ones; an immediate costing by the words it occupies, with byte
          and word alike per Table 2-3; the immediate absent from the calculate
          table; and the long absolute being the one fetch row whose two columns
          differ.*
    - [ ] **Composing them.** C9's second question proposed
          `max(microcode, hideable) + blocking`; working it through shows that
          **does not work either**, and `docs/references/M68030_TIMING.md`
          records the arithmetic. Both splits of the bus give 6 for
          `ADD.B D0,(A0)` uncached where the manual and the oracle say 7,
          because the extra prefetch is worth two clocks and the answer must
          move by *one*. No all-or-nothing split produces a partial cost.
          The marginal cost of a prefetch is therefore fractional and
          per-instruction — and it is **published**: `NCC − CC` is 0 for
          `ADD Rn,Dn`, 1 for `ADD Dn,EA`, 2 for a taken `Bcc`. That is the slack
          Motorola measured, not a rule to invent.
          **A check was proposed, appeared to pass, and did not.** Dividing
          `NCC−CC` by the published `p` was claimed to give 0 or 1 uniformly
          across eleven rows. Run over *every* transcribed row it does not:
          `BSR` gives 1.5, `DBcc` with the condition true gives 2, and `LINK.L`
          gives 0.5 — all three already in the table when the claim was made.
          `docs/references/M68030_TIMING.md` records the withdrawal and the
          method error: a pattern found on a subset and stated generally is a
          hypothesis presented as a result, and computing it mechanically over
          everything is what overturned it.
          So there is **no licence** to apply `NCC−CC` per prefetch, which is
          what the composition needed it for. The `p`-is-a-rounded-average
          caveat is now load-bearing rather than a hedge: 1.5 and 0.5 are what a
          rounded denominator looks like, which would mean the per-prefetch cost
          is not recoverable from the published pair for those rows at all.
          **`p` is now transcribed and the division runs in code.**
          `ap_m68030_prefetch_cost` reports `exact` false where `NCC − CC` is
          not divisible by `p`, and `timing_table_suite` requires every inexact
          row to be *named in the test* — `BSR` and `LINK.L` are; a new one
          fails, and so does a stale name for a row that is no longer inexact.
          That is the property the prose claim could not have: it was true of
          the rows its author happened to look at, and nothing made the others
          speak up.
          A second test pins the substance of the withdrawal: an exact cost of
          **2** exists (`DBcc` with the condition true), which the claim denied.
          *Verification: `ADD.B D0,(A0)` coming to 7 against the oracle and
          against `NCC + fea`; and the second worked example of §11.3.4, which
          exists precisely to exercise Equation (11-2).*
  - [ ] Wire the bus to a memory system so the termination kind and its arrival
        clock come from a device rather than a test. That is what makes
        contention emergent, and it belongs with Phase 3's single arbitration
        point.
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
        The word at `+$06` carries the vector **offset**, not the vector number
        — TRAP #0 stacks `$2080`, not `$2020`. And formats `$3`, `$4` and `$7`
        are defined by *other* M68000 family members but not by the 68030, so
        accepting them would silently import another processor's frame; they are
        a format error, vector 14.
  - [x] Level 7 interrupts, which cannot be expressed as a comparison against
        the mask. "Level 7 interrupts cannot be masked by the interrupt priority
        mask, and they are transition sensitive ... recognizes an interrupt
        request each time the external interrupt request level changes from some
        lower level to level 7, regardless of the value in the mask." So
        recognition needs the *previous* level, which the interface takes: held
        at 7 is not a new interrupt, dropped and re-raised is. The manual's own
        level 6 contrast is tested alongside it.
  - Note on the priority table's wording, which reads as a contradiction and is
    not: "0.0 is the highest priority, 4.2 is the lowest", then "the lower the
    priority of an exception, the sooner the handler routine for that exception
    executes". The higher-priority exception is *processed* first, which stacks
    it deeper, so the lower-priority handler runs first and returns into it.
    Reset is the stated exception to its own rule.
  - [x] **Family `0100` executes**, completing the family the decoder finished
        some commits ago. `LEA`, `PEA`, `SWAP`, `EXT.W`/`EXT.L`/`EXTB.L`,
        `NBCD`, `CHK.W`/`CHK.L`, `MOVEM` in both directions, `NEGX`, `TAS`,
        `MOVE` to and from both `SR` and `CCR`, and the `ILLEGAL` word.
        **`MOVEM`'s mask is reversed for the predecrement mode** — "bit 0 A7 …
        bit 15 D0" against "bit 0 D0 … bit 15 A7" — which makes one loop over
        bits 0 to 15 give both documented orders, "from D0 to D7, then from A0
        to A7" for the control modes and "from A7 to A0, then from D7 to D0" for
        predecrement. Reading it the same way round for both saves the right
        number of registers into the right amount of space with every one in the
        wrong place, and a matching postincrement `MOVEM` restores them anyway —
        so only an outside observer of memory can see it.
        Two more `MOVEM` facts that are this part's rather than the 68000's: a
        word transfer **sign-extends into the whole register**, data registers
        included, which is unlike every other data register write; and "if the
        addressing register is also moved to memory, the value written is the
        initial register value decremented by the size of the operation. The
        MC68000 and MC68010 write the initial register value (not decremented)."
        **`CHK`'s comparisons are signed** — "The upper bound is a twos
        complement integer" — so an unsigned model lets a negative register pass
        any bound whose top bit is clear, which is nearly every bound written.
        **`TAS` flags the value before setting the bit**; the other order makes
        it always report an already-taken semaphore, and everything built on it
        deadlocks. **`MOVE from SR` is privileged and `MOVE from CCR` is not**,
        which reads backwards from the 68000 and is why it is stated.
        `BKPT` is the one form left: it runs a breakpoint acknowledge cycle in
        CPU space, a bus transaction this step does not issue, so it is declined
        rather than called illegal — which is what it becomes only if nothing
        answers. It is now the step suite's unimplemented-instruction
        placeholder, replacing `LEA`, which replaced `MULU`, which replaced
        `ADD`.
        The single-operand escapes also gained their **sizes in the decoder**:
        the size field was the escape that selected them, but `TAS` is still
        "Size = (Byte)" and the four transfers "Size = (Word)". Reporting zero
        conflated "carries no size field" with "has no size" and left every
        executor re-deriving it.
        *Verification: `step_suite`, 14 further tests (111 total) — `LEA` against
        `MOVEA` on the same operand, one indirection apart and both plausible;
        the predecrement mask reversal seen in memory; a save/restore round trip;
        a word `MOVEM` reaching the whole register; `CHK` inside, above and
        below its bound, with the negative case distinguished by `N`; `TAS`
        reporting a free semaphore and taking it; `MOVE from SR` trapping in
        user state while `MOVE from CCR` does not; `MOVE to CCR` unable to reach
        the system byte; `ILLEGAL` taking vector 4 with its own address stacked;
        `NBCD` in both the tens and nines complement; and the three `EXT` forms
        reaching different distances from the same source byte.*
  - [x] **The `$4E` control group executes in full**: `JSR`, `JMP`, `BSR`,
        `RTS`, `RTR`, `RTD`, `RTE`, `LINK`, `UNLK`, `TRAP`, `TRAPV`, both
        directions of `MOVE USP`, both directions of `MOVEC`, `STOP` and
        `RESET`, with the privileged ones raising a privilege violation in user
        state.
        **`MOVEC`'s control register codes are not a dense index**: bit 11
        separates the 68010's SFC/DFC/USP/VBR from the 68020's CACR/CAAR/MSP/
        ISP, so `$800` is not `$002` with a different index. A model treating
        the field as a small number puts the USP where CACR belongs, and both
        hold plausible 32-bit values. A code this part does not define is an
        illegal instruction rather than a silent no-op — which is how the
        68040-only MMU codes are kept out. This is also what makes `VBR` and
        `CACR` reachable at all, and the boot PROM sets both.
        **`STOP` loads the status register first**, interrupt mask included:
        that is the whole point of the instruction, and loading the mask after
        halting would leave a window at the old priority. A stopped processor
        does not prefetch, so the step returns before touching the pipe.
        **`RESET` changes nothing inside the processor** — "The processor state,
        other than the program counter, is unaffected, and execution continues
        with the next instruction" — so it is counted rather than acted on. A
        model that halted or reset itself here would stop the boot PROM at its
        first line, since resetting the devices is among the first things it
        does.
  - [x] **The wider branch displacements**: `BRA`/`Bcc`/`BSR` at 16 and 32 bits,
        all three sizes sharing one base, "the instruction address plus two".
        An **untaken** wide branch still consumes its displacement words — the
        read has to happen before the condition is tested, or the PC lands
        inside the displacement and executes it as an instruction, which decodes
        as something.
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
        Still open, each declined rather than approximated: reset (stacks
        nothing), the bus and address error frames (formats `$A`/`$B`, which
        carry internal state this model does not have), and the coprocessor
        mid-instruction frame. `CHK`, `TRAPV`,
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
        `[030]` Figure 9-37's lower half does not survive the scan — the bit
        positions of E, CI, R/W, RWM, FC BASE and FC MASK OCR to nothing but a
        stray "FC MASK", under both `pdftotext` modes — so the register was
        modelled as decoded fields with *no* packing rather than an invented
        one. The `M68000 Family Programmer's Reference Manual 1992` Figure 1-9
        gives it intact: `31-24 ADDRESS BASE`, `23-16 ADDRESS MASK`, then
        `E(15) 0(14-11) CI(10) R/W(9) RWM(8) 0(7) FC BASE(6-4) 0(3) FC
        MASK(2-0)`, with prose agreeing field for field with §9.7.3's. That is
        exactly the cross-check this item asked for, so the packing is
        transcribed from two agreeing sources rather than reconstructed. The
        decoded struct stays the interface every other module uses; the packing
        exists because `PMOVE` moves a register image, not a struct.
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
  - Note: unlike TT, this register's bit layout *is* pinned — the prose states
    "the E bit (bit 31)", the figure's column markers (31, 25, 24, 20, 16, 15,
    12) survived, and the field widths are given in prose. `ap_m68030_tc.h`
    records that reasoning bit by bit, so the difference from TT's deferred
    packing is a documented judgement rather than an inconsistency.
  - [x] **Descriptor semantics and accumulated search state**
        (`src/core/cpu/m68030/ap_m68030_desc.c`), `[030]` §9.5.1.1 pp. 9-20 ff.
        Two facts that are easy to implement wrongly and that surface only once
        an OS is running, so both are pinned before the walk exists:
        the DT field is **context-dependent** — `$2`/`$3` describe the next
        table's format in a pointer table but are *indirect descriptors* in a
        page table, so a walk that ignores its level would follow an indirect
        descriptor as though it were a table; and protection **accumulates**
        down the tree, since "the states of all WP bits encountered during a
        table search are logically ORed", making a permissive page reached
        through a protected pointer still protected.
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
        **An ATC hit must cost zero clocks** — "the translation time of the ATC
        is always completely overlapped by other operations; thus, no
        performance penalty is associated with ATC searches" — so all the time
        lives in the miss, in the table search's bus cycles.
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
        **What remains PROVISIONAL** is only which entry is chosen among those
        whose history bit is clear. That is genuinely unstated in both manuals.
        *Verification: `atc_suite`, 3 further tests (20 total) — a hit marking
        through the explicit call and *not* through the lookup alone; marking a
        miss touching nothing; and a repeatedly hit entry surviving a sweep that
        evicts idle ones, which is the property the bit exists for and which was
        absent while only inserts marked. Remaining: measure eviction order
        against the oracle for the last undocumented half.*
  - [x] **MMU status register (`MMUSR`)**
        (`src/core/cpu/m68030/ap_m68030_mmusr.c`), `[030]` §9.7.4 pp. 9-59 f.
        and Table 9-3. One name over two different registers: the bits mean
        different things depending on whether `PTEST` searched the ATC (level 0)
        or the tables (levels 1-7), and a bit one form defines the other clears
        outright. Two constructors rather than one with a mode flag, so neither
        can produce the other's answer.
        **The bit layout is transcribed, not deferred** — Figure 9-38 lost its
        field boxes to the scan exactly as Figure 9-37 did, but the `M68000
        Family Programmer's Reference Manual 1992` gives it intact on the
        `PTEST` page (p. 6-64): `B(15) L(14) S(13) 0(12) W(11) I(10) M(9) 0(8)
        0(7) T(6) 0(5) 0(4) 0(3) N(2-0)`. The two documents cross-check — the
        PRM's last named single bit is T, and the 68030 manual's surviving
        column markers stop at 6.
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
  - "Undefined" is represented as zero, and it is a representation decision
    rather than a claim: Table 9-3 marks W, S and M undefined when I is set, and
    every other bit undefined when T is set. Clearing them keeps our output from
    implying a guarantee the manual does not make — so an oracle diff must mask
    those bits rather than treat a difference as a fault.
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
    - [ ] **Open reading, to settle against the oracle: when a supervisor
          violation suppresses the U update.** `[030]` says the U bit is set
          "except *after* a supervisor violation is detected" without saying
          whether a descriptor whose own S bit causes the violation still gets
          its own U set. We evaluate the violation with the current descriptor's
          S already folded in, so it does not — which is consistent with the
          hardware being able to do it (the RMC write half follows the read) and
          with the manual's other sentence, that "a pointer may be fetched, and
          its U bit set, for an address to which access is denied at *another*
          level of the tree". The 68851 manual repeats that sentence and drops
          the supervisor clause entirely, so it does not arbitrate. This is a
          documented reading, not a measurement. *Verification: a user-mode
          access to a supervisor-only tree under the oracle, comparing whether
          the root descriptor's U bit changed.*
    - [x] **Fill the ATC from a completed search**
          (`ap_m68030_walk_fill_atc`), so a miss populates the entry a hit then
          serves for free. This is the join between `ap_m68030_walk` and
          `ap_m68030_atc`, and the point at which the "hit costs nothing, miss
          costs the search" claim becomes measurable end to end.
          A *failed* search fills an entry too, rather than leaving the address
          uncached: "If a limit violation is detected, the ATC is loaded with an
          entry having the bus error (B) bit set." So a faulting address does not
          re-run the table search on every access — the fault itself is cached,
          which is a timing claim as much as a correctness one. B folds in all
          four conditions §9.4 names: bus error, invalid descriptor, supervisor
          violation, limit violation.
          *Verification: `walk_suite`, 7 further tests — a filled entry turning
          the next access into a hit at the right physical address, WP and CI
          reaching the entry, each of the three fault kinds cached as a faulting
          entry (including a supervisor violation, where the search itself
          succeeded and only the access was illegal), and the end-to-end form of
          the §9.4 timing rule: a read fills M clear, so a later write is a hit
          that still forces a search, and refilling after that search makes the
          write free.*
    - [x] **Descriptor status bit positions: derived, and labelled as derived.**
          `[030]` Figures 9-10 and 9-11 did not survive the scan below the
          address fields, so unlike TTx and `MMUSR` there is no second document
          that simply states the 68030's. The positions are therefore *derived*,
          from five sources that agree, and `ap_m68030_walk.h` records the
          argument so it can be checked rather than trusted:
          the `MC68851 PMMU User's Manual 3ed` §5.1.5.3 gives every position in
          prose; `[030]` §9.6 says the 68030 "is program compatible with the
          MC68020/MC68851 combination" and its list of MMU differences does not
          include descriptor format, which it could not omit if the bits moved;
          the features it *does* list as absent — access levels, gates, lockable
          entries, shared-globally — are exactly the 68851 bits the 68030 has no
          field for, leaving precisely the 68030's set; `[030]` Figure 9-10 does
          survive in raw extraction as far as `TABLE ADDRESS` at 31-4 over a
          **4-bit** status, matching the 68851's U/WP/DT being the only status
          bits a table descriptor carries; and `[030]` Table 9-3 independently
          says `MMUSR`'s S comes from "the S bit of a **long** format table
          descriptor or long format page descriptor", confirming the 68851's
          placement of S at bit 40, long format only.
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
    - [ ] **Still open, and not settleable from the oracle:** whether a
          descriptor whose *own* S bit causes the violation gets its own U set.
          We fold that descriptor's S in first, so it does not. The oracle omits
          the clause entirely and therefore has no opinion, so this needs real
          hardware or a Motorola erratum rather than another reading.
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
        The function code is part of the tag in *both* caches, which is what
        lets them survive a supervisor/user switch unflushed.
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
  - Note: `CACR`'s bit positions are transcribed from §6.3.1's prose ("Bit 13,
    the WA bit", "Bit 9, the FD bit", …) rather than from Figure 6-14, so this
    register needed no derivation. `CD`, `CED`, `CI` and `CEI` are modelled as
    *actions* performed by the write rather than as fields, since all four "are
    always read as zero" — storing them would invent a readable bit the hardware
    does not have.
  - [x] **The cache's half of the timing join: the `CBREQ` decision**, `[030]`
        §7.3.7. Whether a miss asks the memory system for a whole *line* rather
        than one long word, which is worth 5 clocks against 8 and so misprices a
        line fill if it is wrong even when the data ends up right. The manual
        gives two conditions and it is an **or**: the tag does not match, *or*
        "all four long words corresponding to the indexed tag ... are marked
        invalid". The second is the one easily left out — a line whose tag
        matches but whose entries were all cleared still bursts, and without it
        a cleared cache would refill an entry at a time and never burst at all.
        Suppressed by a clear `DBE`/`IBE`, a disabled cache, a frozen cache, or
        any read-modify-write cycle.
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
- [ ] 68882 FPU. *Verification: probe suite over each operation and rounding
      mode; note the oracle's admitted FPU gaps as a divergence class.*
- [ ] 68020 subset: no on-chip MMU or cache differences, external 68851.
      *Verification: `dn3000` boots under both; oracle diff.*
- [ ] 68851 external PMMU as its own subsystem. *Verification: `MC68851 PMMU
      User's Manual 3ed` cited per figure; oracle diff.*
- [ ] 68040 for DN5500: different pipeline, caches, and MMU descriptor format;
      integrated FPU. *Verification: `MC68040 User's Manual 1993` cited;
      `dn5500` oracle diff, expecting to exceed the oracle's FPU coverage.*
- [ ] Data-dependent instruction timings published only as ranges are modelled
      at the documented value and marked `PROVISIONAL`. No invented point
      numbers. *Verification: each such instruction appears in the PROVISIONAL
      table with its manual page.*

## Phase 3 — Core board

- [ ] Memory bus with one shared arbitration point, so contention is emergent.
      *Verification: probes measuring contention between CPU and DMA.*
- [ ] Address translation map (`0x017000`), CPU status/control, cache control,
      task alias, master request, latch-page-on-parity-error registers.
      *Verification: `008778-03` cited per register; oracle diff.*
- [ ] Two 8259 interrupt controllers and the Apollo interrupt vector scheme.
      *Verification: probe-driven interrupt ordering vs oracle.*
- [ ] Two AT DMA controllers. *Verification: transfer probes; device request
      lines gate DMA at block granularity, not per word.*
- [ ] Interval timer and calendar. *Verification: self-timing probes; the
      14-day calendar interval hazard noted in the MAME driver is reproduced or
      explained.*
- [ ] SIO serial lines, keyboard and mouse. *Verification: console byte stream
      identical to the oracle's.*
- [ ] Node ID PROM (`0x011200`), including node ID taken from the logical volume
      label. *Verification: `lcnode`-visible node ID matches the configured
      value.*

## Phase 4 — Storage, then a first boot

- [ ] Winchester controllers: OMTI (DN3000), WD7000 ESDI and SCSI (DN4500).
      *Verification: DMA-completion device shape — transfer now, schedule
      completion in emulated time; oracle diff on a real disk image.*
- [ ] Floppy (programmed-I/O state machine, correct power-up/no-media state).
      *Verification: first register read matches the oracle exactly.*
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
      33 MHz, from `[CFG]`'s Series 4500 Product Summary; the conflicting 30 MHz
      in `[CFG]`'s own overview table is recorded as a resolved discrepancy.
      *Verification: two independent citations in the same document, both
      recorded in `docs/PROJECT_STATUS.md`.*
- [x] Add DSP4500, which `[CFG]` documents and the table was missing.
      *Verification: `model_suite` checks it matches its DN4500 sibling exactly.*
- [x] Close most of the DN2500 `PROVISIONAL` set: 68030 @ 20 MHz, 68882 @ 20 MHz,
      on-board mono graphics, 4–16 MB RAM, all from `[CFG]`'s Series 2500 Product
      Summary. Only `ram_base` remains open.
- [ ] Close DN2500 `ram_base`, or record it as a documented gap with its cost to
      close. *Verification: an address-space table for Series 2500, or the boot
      PROM's own memory sizing code.*
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
