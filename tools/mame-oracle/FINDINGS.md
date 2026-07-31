# Oracle findings

One row per **probe campaign**: a question asked of both implementations, the
number each gave, and how the disagreement was settled. This is the ledger that
makes "cycle-accurate" a checkable claim rather than an assertion.

MAME's `apollo` driver is the runnable oracle. It is **built and instrumented,
never linked** — GPL-2.0-or-later against this core's MIT, so the relationship is
arms-length: MAME runs as a separate program and its output is compared with
ours. See `ext/README.md`.

## The rule that makes this file worth keeping

**A difference is not automatically our bug.** MAME's own driver notes admit gaps
in some FPU operations and operands, Winchester bad-track formatting, and certain
video synchronisation. So every disagreement is *classified*, never silently
"fixed" by moving our number to match:

| Class | Meaning | What happens |
| --- | --- | --- |
| `ours-wrong` | The oracle is right and we are not | Fix ours; the row cites the probe that proves it |
| `oracle-wrong` | We are hardware-truer than MAME | **Keep ours.** Instrument MAME to demonstrate it, cite the manual page, and record it as a divergence |
| `sub-poll-slack` | Both are within the granularity the probe can actually resolve | Neither is wrong; record the resolution limit so it is not re-litigated |
| `open` | Not yet settled | Must name what measurement would settle it |

Resolving a discrepancy by adjusting our timing until it matches is **forbidden**
— that is trial-and-error parameter search against our own code, and it launders
the oracle's bugs into ours. Characterise the *shape* of the difference first
(proportional? fixed offset? per-instruction-type? per-operand?), because the
shape names the cause.

## Campaigns

None yet. The oracle harness is not built, so no campaign has run and there is
nothing to report. This section stays empty rather than being pre-populated with
plausible-looking rows.

When the first campaign lands, each row carries:

| Field | Meaning |
| --- | --- |
| Campaign | What was asked, and the probe that asks it |
| Ours | Our figure, in `AP_TIME_BASE_HZ` units and in the subsystem's own cycles |
| Oracle | MAME's figure, with the instrumentation that produced it |
| Class | One of the four above |
| Evidence | Manual page, patent, or ROM address — an oracle number alone never closes a row |
| Story | Why they differed, in a sentence, so a future contradiction has history |

## Instrumenting the oracle

Temporary instrumentation in `ext/mame` is **always reverted before commit**,
in the oracle's checkout as well as ours. Edit, measure, restore — never
`git checkout <file>` over uncommitted work. A campaign that needed
instrumentation records what was patched and where, so it can be reproduced
without guesswork.

The oracle build is deliberately narrow — one driver, no tools:

```sh
cd ext/mame
make SOURCES=src/mame/apollo/apollo.cpp SUBTARGET=apollo TOOLS=0 -j"$(nproc)"
```

This produces a single-driver binary rather than a full MAME. It is never built
by our CMake, never linked, and CI never compiles it.

**The binary's name moved with MAME's makefile.** Older checkouts produced
`<TARGET><SUBTARGET>` = `mameapollo`; the pinned v0.289 names it after the
subtarget alone, `apollo`. `oracle.py` accepts either and prefers whichever
exists, for the same reason `romset.py` matches ROMs by SHA-1 rather than by
name: neither should break when the `ext/mame` pin moves. Verified against the
built binary — `./apollo -listfull` lists all eleven apollo machines, `dn3000`
through `dn5500`.

**Budget memory, not just cores.** `-j"$(nproc)"` is the wrong instinct on a
small machine: the peak translation units (`luaengine.cpp`, `emumem_aspace.cpp`)
each want ~2.5 Gbyte under `-O3`, measured, so parallelism is bounded by RAM
rather than by CPU. Eight jobs against 8 Gbyte does not build slowly — it swaps
until the machine is unusable. Roughly one job per 2.5 Gbyte of *available*
memory, and on a workstation that is also running a desktop, a cgroup ceiling
turns a bad guess into a failed compile instead of a hard reset:

```sh
systemd-run --user --scope -p MemoryHigh=7G -p MemoryMax=9G \
  nice -n 10 make SOURCES=src/mame/apollo/apollo.cpp SUBTARGET=apollo \
  TOOLS=0 NOWERROR=1 -j4
```

`ccache` is worth installing before the first build rather than after: the
instrument → measure → revert loop below recompiles the same translation units
repeatedly, and a cache hit spawns no compiler at all.

`REGENIE=1` is needed on the first build so genie regenerates the makefiles for
the narrowed `SOURCES`, and `NOWERROR=1` keeps a warning in third-party code
from stopping a build we are not maintaining. The pinned checkout has no tags,
so MAME's version step prints `fatal: No names found, cannot describe anything`
— harmless, and not an error in our build.

### The oracle runs, and two defects it hid

First readings against the real binary, 2026-08-01.

`verify` passes: two `dn3500` runs at the same point are byte-identical (969
bytes), and so are two `dn5500` runs (985 bytes). That closes the half of the
harness item that needed a real emulator.

**`dn5500` runs despite `MACHINE_NOT_WORKING`.** The flag was reason to suspect
the 68040 path had no oracle at all; run empirically, it starts and dumps
reproducibly. So it *is* diffable — but the flag is MAME's own statement that it
does not vouch for the result, which makes DN5500 a divergence class to treat
with suspicion rather than an absent oracle. Do not promote it to `paper` in the
model table on the strength of the flag.

**Defect found: the driver's parameters never reached MAME.** `oracle.py` built
an environment with `APOLLO_DUMP_AT`, `APOLLO_DUMP_CPU` and `APOLLO_DUMP_MEM`
and then called `subprocess.run(...)` **without `env=`**, so `dump.lua` fell back
to its defaults for all three. Every reading was taken at 1.0 emulated seconds
regardless of `--at`, and no memory range was ever dumped. The success message
printed the *requested* value, which is what hid it: `verify` reported
"reproducible at 1000000.000000s" for a run that had dumped at 1.0. Two runs of
the same wrong workload are still identical, so determinism passed and said
nothing. Fixed by passing `env`; `--at 1.0` and `--at 2.0` now yield different
machine states (`CURPC` `$7AE` against `$794`), which is the check that the
parameter is live.

The lesson generalises: a harness that reports back the value it was *asked* for
cannot detect that the value was ignored. Report what the run actually did.

**Fixed: no dump was produced beyond about 3.2 emulated seconds.** The dumper
triggered on `emu.add_machine_frame_notifier`, and frame notifications *stop
arriving* on `dn3500` at **3.246948 emulated seconds** — measured with a
throwaway probe script: frame 195 is the last one delivered, while the machine
itself runs on past 19s quite happily. So every sampling point beyond about 3.2s
silently produced nothing, and MAME still exited 0, which reads as success to
anything not counting dump blocks.

The cause is the notifier, not the machine: `-video none` plus whatever the boot
PROM does to the screen around 3.2s ends frame generation, and a frame-driven
hook ends with it. `emu.register_periodic` keeps firing for the whole run
(verified to 8s), so `dump.lua` uses that instead. `verify` now passes at 3.5,
5.0 and 8.0 emulated seconds, byte-identical across runs, with genuinely
different machine state at each (`CURPC` `$798` at 3.5s against `$78E` at 8.0s)
— which is the check that the dump is sampling rather than repeating.

`test_dump.lua`'s mock now supplies *only* `register_periodic`, so a change back
to a frame notifier fails the format test rather than silently truncating every
campaign to the first three seconds.

**Small open item:** the dump header's `at` line still reports the *requested*
time rather than the time actually sampled. That is deliberate — it keeps the
line deterministic — but it is the same shape as the `env` defect above, where a
report of the input concealed that the input was ignored. Worth adding an
`at_actual` line, which the fixed trigger now makes safe to do: the sampling
point is demonstrably reproducible.

### ROM sets

`tools/mame-oracle/romset.py` assembles them from `roms/firmware/` into
`out/roms/<machine>/`, which is gitignored, so no firmware can be staged.

Two properties make it trustworthy, and both are deliberate:

- **The ROM table is parsed from `apollo.cpp`, never transcribed.** A hardcoded
  table would be correct until the `ext/mame` pin moved and silently wrong
  after. Alias chains (`#define rom_dsp3500 rom_dn3500`) resolve too.
- **Our files are matched to MAME's by SHA-1, never by name.** Name matching
  would need a case-folding rule and then an exception to it; the SHA-1 is
  already in the driver and is what actually has to hold.

*Verified: all 11 apollo machines assemble, and the SHA-1 of every one of our
bitsavers images equals the one the driver declares — `3500_BOOT_12191_7.bin`
is `36f3c83d…`, `3000_BOOT_8475_7.bin` is `6c383d22…`, `5500_BOOT_A1631-80046`
is `7315a884…`. Our images are bit-for-bit the ROMs the oracle expects and
differ only in filename case.*

### What the oracle does not cover, from the driver itself

- `dn5500`, `dsp5500` and `dn5500_19i` are declared `MACHINE_NOT_WORKING`
  (`apollo.cpp` lines 1259–1261), while every 3000 and 3500 machine carries no
  such flag. If that flag reflects reality, **the entire 68040 path has no
  working oracle**, which would make Phase 2's "`dn5500` oracle diff"
  verification unachievable as written and reclassify DN5500/DSP5500 from
  `mame` to `paper` in the model table.
  **Not yet acted on: this is the driver's own claim about itself, and the
  honest test is to run it.** Recorded here so it is checked the moment the
  build finishes, rather than discovered in Phase 2.
- No ring ROM appears in any set, confirming from the driver source what
  `RING.md` already states from the manuals: MAME models no Apollo Token Ring.

## Where the ring is not

The Apollo Token Ring has **no runnable oracle at all**: MAME carries Domain
networking over an emulated 3c505 802.3 card instead. Ring figures therefore
never appear in this file as an oracle comparison. They live in
`docs/references/RING.md`, each citing `010005-00`, patent 4,716,575,
`008778-03`, or a ring-firmware disassembly address.

The one exception is the 3c505 itself, which *is* modelled by MAME: when that
path is implemented it gets ordinary rows here, like any other device.
