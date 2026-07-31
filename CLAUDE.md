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
- **Complete modules, don't chase the boot.** Finish one subsystem with its
  tests before moving on. Boots are integration checks and thermometers, never
  milestones.
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
