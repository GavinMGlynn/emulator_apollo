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
- [x] Warning set applied to first-party targets only, `-Werror` in debug/CI.
      *Verification: debug build is clean with `APOLLO_WERROR=ON`.*
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
        assemble, and every one of our bitsavers images has exactly the SHA-1
        the driver declares; output lands in the gitignored
        `tools/mame-oracle/out/roms/`.*
  - [~] The narrow build itself (`SUBTARGET=apollo`, `REGENIE=1`, `NOWERROR=1`).
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
  - [ ] **Verify empirically whether the 68040 path has an oracle at all.**
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
  - [ ] The remaining half: `verify` against the **real** oracle, showing two
        runs of a real workload byte-identical. Needs the binary.
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
- [ ] Probes side-loadable into post-boot machine state, so CI needs no
      copyrighted firmware. *Verification: the probe suite runs in CI with
      `roms/` absent.*
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
  - The whole-machine part stays open until there is machine state to hash: a
    CPU, devices and a bus. It is a Phase 2/3 tail, not something to fake now
    over an empty machine.

## Phase 2 — CPU family

Build the 68030 first (DN3500 is the superset), then subset and extend.

- [ ] 68030 integer core, strictly cycle-stepped: one `tick()` per machine
      cycle, no batching, no event queues. *Verification: probes against the
      oracle; `MC68030 User's Manual 3ed` for the paper timing figures, each
      cited.*
- [ ] Exceptions, traps, interrupt priority, bus/address error stack frames.
      *Verification: probes that deliberately fault, diffed against oracle.*
- [ ] 68030 on-chip MMU: translation tables, ATC, transparent translation,
      `MMUSR`. *Verification: probe walks and faults; oracle diff.*
- [ ] 68030 on-chip instruction and data caches, and their effect on bus timing.
      *Verification: self-timing probes measuring hit vs miss.*
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
