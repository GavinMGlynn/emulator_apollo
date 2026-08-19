# Working conventions

## The two-core strategy

Build a **strictly cycle-stepped reference core first**: one `tick()` per machine
cycle, every subsystem advancing inside it, no batching, no event queues, no
special cases. This is what makes the timing provable — bus contention is
emergent, probes measure real interleavings, and every number can be checked
against an oracle. Its job is to be *right*, and to be the thing everything else
is checked against.

The reference core will not reach real time, and that is accepted. A verified
fast mode comes later, and only under an identity harness. **Never weaken the
reference core to chase speed** — that is the classic mistake.

## Discipline

- **Reference-first.** Resolve behaviour from the manuals in `docs/references/`
  or from the oracle. *Never* by trial-and-error parameter search on our own
  code. Characterise the shape of a discrepancy — proportional? fixed?
  per-type? — before fixing it.
- **The resolution order is mandatory: reference → web → oracle.** Exhaust the
  documents before measuring anything.
  1. The part's own manual, and **read the page image**, not a text extraction:
     OCR mangles precisely what timing and register tables are made of, and
     `4(1/1/0)` arriving as `4(1/010)` reads as plausible data.
  2. **The sibling manuals already in `docs/references/`** — the 68020's,
     the 68851's, the 68040's. The same table, typeset differently, often
     carries the note that resolves an ambiguity in ours. This is the step most
     often skipped and the files are already on disk.
  3. The web: other transcriptions of the same tables, errata, archives.
  4. **Only then the oracle.** Instrumenting an emulator to recover a number
     that is printed in a book is the most expensive route and the least
     conclusive — it says what MAME does, and this project expects to
     out-accurate it.
  The oracle's job is what the documents cannot answer. When they genuinely run
  out, say so: name what was read and what it failed to settle, *then* measure.
- **A misbehaving module is presumed incomplete until its register tables are
  walked.** Before instrumenting anything, before the oracle, before a boot:
  take the part's manual, walk **every field of every register** against the
  code, and fix what is missing or transposed. Then search the web if a section
  is ambiguous. The oracle is *fourth*.
  This is not the same as the resolution order below, and it is the one most
  often skipped: that order governs *resolving a question*, this one governs
  *what to do first when something is wrong*. Grepping headers for `not
  modelled` cannot find a bit nobody ever noticed; a table walk can, and found
  five in one part that a header sweep had missed. A green test suite is not
  evidence of completeness — tests encode the same misreadings as the code, and
  three did.
  **The tell:** if a fix is justified by "this could explain the failure"
  rather than "this was measured to be the failure", stop and walk the tables.
  Detail and the session it came from: `../emulator-setup-guide.md`, Appendix.
- **A document that turns out to contain one unimplemented thing is a document
  that must be read WHOLE — page by page, line by line.** This is mandatory and
  overrides the resolution order's economy: the moment a manual, addendum or
  datasheet yields *one* fact this core does not have, stop treating it as a
  reference to be queried and start treating it as a document to be **derived
  in full**. Walk every page, capture every register, field, table row, figure,
  timing and stated behaviour into code — or into a named `PROVISIONAL` gap
  where it genuinely cannot be implemented — before moving on.
  **Why it is not optional.** A document that was wrong about one thing was
  never *consulted* about the rest; it was consulted about the one question
  someone happened to ask. `019411-A00` was grepped and declared silent, then
  read as page images and yielded three registers, a whole address map, a
  fourth memory bank and a bank-layout correction — from ten pages. The
  question "what else is in here?" has never once come back empty.
  **The tell:** finding a gap and fixing only that gap. If the commit that
  closes a documentary finding does not also say what the *rest* of the
  document contained, the document was not read.
  Record the walk's coverage — pages read, sections derived, what each yielded
  — so a later reader can tell a finished document from a sampled one.
- **FINISH THE MODULE. 100%, or it is not finished.** This is the rule most
  often broken here, and breaking it is the single most expensive habit in this
  project. A module is finished when **every** documented behaviour of the part
  is implemented and tested — every register, every field of every register,
  every command, every documented state and transition — walked against the
  part's own manual, one table at a time. Not "the parts the boot exercises".
  Not "the parts the firmware's self-test reaches". Not "enough to move on".
  - **A green suite is not evidence of completeness.** Tests encode the same
    misreadings as the code. Completeness is established by the table walk
    against the manual, and by nothing else.
  - **Do not start another subsystem while one is unfinished.** If a module
    cannot be finished — a genuinely missing document, a hardware fact no
    source carries — then say so *in that sentence*, name the specific gap, mark
    it `PROVISIONAL` in code and in `docs/PROJECT_STATUS.md`, and make it a
    named plan item. "Blocked" is a claim that must name what would unblock it.
  - **Never describe a partial module as working.** Not in a commit message,
    not in `PROJECT_STATUS.md`, not in a reply. "Working" means 100%. Anything
    else is reported with the fraction and the missing part named first —
    "the register interface is complete; the DMA path and interrupt are not
    implemented", never "the controller works".
  - **The tell:** if the answer to "is this module done?" needs a qualifier,
    the module is not done and the honest report is the qualifier, up front.
- **Don't chase the boot.** Boots are integration checks and thermometers,
  never milestones. A boot that gets further is not a module that got finished.
- **Measure, don't guess.** Timing figures come from probes against the oracle,
  or from the paper oracle with a cited manual page. Where the measurement does
  not exist — a data-dependent instruction published only as a range — model the
  documented value, mark it `PROVISIONAL` in code *and* in
  `docs/PROJECT_STATUS.md`, and make it a named plan item. Never invent a point
  number to fill a gap.
- **Release builds only for any timing or performance measurement.** Debug and
  CI builds are `-O0`. Emulated cycle counts, by contrast, must be identical on
  every build type and platform — that is what makes goldens portable, and CI
  asserts it.
- **Verify on the real output** — a booted machine, a decoded PNG, a console
  byte stream — not a proxy.
- **One item at a time, landing with its test.** Keep `ctest` green; a red tree
  is the stop-everything condition. Name tests as sentences stating the hardware
  or design fact: `test_a_word_write_to_an_odd_address_faults`.
- **Commit each finished item and push immediately.**
- **Every commit that lands an item updates both living docs in that same
  commit**: `docs/PROJECT_STATUS.md` (what now works, with its verification) and
  `docs/COMPLETION_PLAN.md` (tick the item, add any tails discovered while
  implementing it). Re-read both in full at every phase boundary — status docs
  rot fast.
- **A completed plan item is a summary; the balance moves to
  `PROJECT_STATUS.md`.** One line of what was done, its verification, and
  "Detail in `PROJECT_STATUS.md`" — the reasoning, traps, measurements and
  approximations belong in the status document. The two answer different
  questions: the plan is read forwards to choose the next thing, the status is
  read by whoever has to trust or change a subsystem. Detail left in the plan is
  invisible to the second reader, noise to the first, and buries the open items
  among the closed ones — which defeats the only job the plan has. An item still
  in progress keeps its detail in the plan while it is live, and is compressed
  in the same commit that ticks it.
  **This is checked, not remembered**: `tools/check_docs.py` fails a completed
  item over sixteen lines and names it, and runs as the `doc_claims` CTest
  entry. It drifted for months without it — items reached forty and eighty-one
  lines, several of them carrying "Detail in `PROJECT_STATUS.md`" above the
  detail they had never moved.
- **Deliberate approximations are fine** — documented, with reason and cost to
  close.
- **Temporary instrumentation is always reverted** before commit, in our core
  *and* in the oracle's checkout. Edit-revert-restore; never `git checkout
  <file>` over uncommitted work.
- **Optimization is only safe under an identity harness**: probe goldens plus a
  long-run state hash, checked after every change.

## This machine's specifics

- **Time is counted in `AP_TIME_BASE_HZ` units, never CPU cycles.** Several nodes
  of different models share one 12 Mbit/s ring; no CPU's cycle is a legal unit of
  account. The base is the LCM of every clock in the machine and is a *derived*
  constant: adding a clock it does not divide means recomputing it, which changes
  the unit and no behaviour. `ap_clock_init()` rejects an unrepresentable
  frequency rather than rounding it.
- **DN3500 is the reference superset.** Implement it first and express every
  other model as a subset, from the one table in `src/core/model/`, not from
  conditionals scattered through subsystems.
- **The ring has no runnable oracle.** Every ring figure cites `010005-00`,
  patent 4,716,575, `008778-03`, or a ring-firmware disassembly address. Record
  each in `docs/references/RING.md`. The ring firmware's own self-test is the
  first real test of the controller — the hardware's test suite, for free.
- **MAME is GPL-2.0-or-later; this core is MIT.** Build and instrument
  `ext/mame`, read `ext/musashi`. Never link either into `src/core`, and never
  copy code across.
- **`roms/` and `media/` are gitignored and stay that way.** This repository is
  public; Apollo firmware and Domain/OS media are not ours to redistribute.
  Never stage them, and never `git add -A` without checking what it caught.
- **Expect to out-accurate the oracle.** MAME's driver admits gaps in some FPU
  operations, Winchester bad-track formatting and video synchronisation. When our
  number differs, instrument the oracle to prove which side is wrong, then
  classify: hardware-truer than the oracle (keep ours, cite the evidence),
  sub-poll-slack equal, or actually wrong (fix ours).

## Layout

```
src/core/      the emulator core, a static lib with zero frontend deps
  model/       the model table: all machine variance lives here
  time/        global time base and scheduling
src/frontend/
  common/      shared by every frontend: naming, reports, common options
  headless/    deterministic: no wall clock, no host input, no threads
  sdl/         interactive (later; deliberately not stubbed)
tests/         one <suite>.c per subsystem, one CTest entry each
tools/         probes, the oracle harness, converters
docs/          PROJECT_STATUS.md, COMPLETION_PLAN.md, references/
ext/           third-party as pinned submodules
roms/ media/   user-supplied images, gitignored
```

`src/core` knows nothing about any frontend. Frontends depend on the core, never
the reverse.

## Build

```sh
cmake --preset linux-debug && cmake --build --preset linux-debug
ctest --preset linux-debug
```

Every build preset has a matching *test* preset; adding one without the other
fails only when CI runs it.
