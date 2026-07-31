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

### C1 — 68030 descriptor status bit positions

**Question.** `ap_m68030_walk`'s descriptor decoding was *derived* rather than
transcribed: `[030]` Figures 9-10 and 9-11 lost their field boxes to the scan, so
the positions came from the `MC68851 PMMU User's Manual` plus four corroborating
arguments (see `ap_m68030_walk.h`). Does the oracle agree?

**Method.** Read, not run — `ext/mame/src/devices/cpu/m68000/m68kmmu.h`'s field
constants. Reading the oracle rather than measuring it is weaker than a probe in
general, but unusually strong *here*: a wrong bit position does not degrade
gracefully, it fails to translate, and this implementation boots Domain/OS.

| Field | Ours | Oracle | |
| --- | --- | --- | --- |
| DT | bits 1-0 | `M68K_MMU_DF_DT = 0x3` | agree |
| DT encoding | invalid 0, page 1, table-4 2, table-8 3 | `0/1/2/3` | agree |
| WP | bit 2 | `0x4` | agree |
| U | bit 3 | `0x8` | agree |
| M | bit 4 | `0x10` | agree |
| CI | bit 6 | `0x40` | agree |
| S | upper-word bit 8, i.e. descriptor bit 40 | `0x100` | agree |
| TABLE ADDRESS | 31-4 | `0xfffffff0` | agree |
| INDIRECT ADDRESS | 31-2 | `0xfffffffc` | agree |
| ATC physical field | 24 bits, shift 8 | `0x00ffffff`, shift `8` | agree |

**Class: agree.** Every derived position confirmed, by a sixth source
independent of the five that produced it. The derivation stands.

Two behaviours we had derived from prose are confirmed as code as well: the
oracle sets **U and M in a single write** (`entry | USED | MODIFIED`), which is
our one-`history_writes`-per-descriptor cost model; and it writes **no history
bit to an invalid descriptor** (`type != DT_INVALID`), which we had argued from
"the operating system can use these fields for its own purposes".

### C2 — the supervisor clause in the history-bit update

**Question.** `[030]` §9.5.1.1 says U is set "except after a supervisor violation
is detected", and M only "if the table search does not encounter a set WP bit
**or a supervisor violation**". We implement both clauses. Does the oracle?

**Ours.** Suppresses the U update once a supervisor violation is detected, and
defers the M rule to `ap_m68030_search_should_set_modified`, which honours it.

**Oracle.** `update_descriptor()` does not consider supervisor state at all. It
gates M on `!(entry & WP)` alone and U on `!(entry & USED)` alone. The clause is
simply absent.

**Class: `oracle-wrong`.** The manual states the condition twice, in two separate
sentences, for two separate bits. **Keep ours.** The practical effect is narrow —
it changes history bits only on a search that is already going to fault — but it
is a real difference in what gets written to a translation table, and a
supervisor-only tree probed from user state is exactly where it shows.

**Still open, and the oracle cannot settle it:** whether a descriptor whose *own*
S bit causes the violation still gets its own U set. We evaluate the violation
with that descriptor's S already folded in, so it does not. Since the oracle does
not implement the clause at all, it has no opinion to compare. Settling this
needs real hardware or a Motorola erratum, so it stays a documented reading.

### C3 — `MMUSR` bit positions, and a gap in the oracle

Our layout came from the `M68000 Family Programmer's Reference Manual`'s PTEST
page. The oracle's `M68K_MMU_SR_*` constants agree on every bit it defines:
B `0x8000` (15), S `0x2000` (13), W `0x0800` (11), I `0x0400` (10), M `0x0200`
(9), T `0x0040` (6).

**The oracle defines no LIMIT bit at all.** Ours is bit 14, from the PRM figure,
and `[030]` Table 9-3 describes L for the levels-1-7 form. So MAME never reports
a limit violation through `MMUSR`. Recorded as a place we are more complete than
the oracle rather than as a disagreement — nothing in MAME contradicts bit 14, it
simply has no such bit.

When the first campaign lands, each row carries:

| Field | Meaning |
| --- | --- |
| Campaign | What was asked, and the probe that asks it |
| Ours | Our figure, in `AP_TIME_BASE_HZ` units and in the subsystem's own cycles |
| Oracle | MAME's figure, with the instrumentation that produced it |
| Class | One of the four above |
| Evidence | Manual page, patent, or ROM address — an oracle number alone never closes a row |
| Story | Why they differed, in a sentence, so a future contradiction has history |

### C4 — the boot PROM does not reach the Mnemonic Debugger prompt

**Status: open, and it gates the probe path.**

Phase 1's probe route rests on one assumption, recorded in the plan as settled:
the boot PROM holds the Mnemonic Debugger, whose `A` and `G` commands take
hand-assembled instruction words over the serial console, so probes need no
object format and no Domain/OS boot. The *command syntax* is settled from
`002398-04` ch. 5. What was never checked is whether the PROM reaches the MD
prompt **under the oracle** at all.

It does not, in any configuration tried so far.

**What was run.** `dsp3500` is the headless variant: `apollo_terminal()` in
`apollo_m.cpp` wires the SIO's port B transmit to an `rs232` port whose default
option is `terminal`, at 9600 8N1. Substituting `null_modem` and pointing
`-bitb` at a file captures the transmitted bytes exactly, which is what a
byte-exact transcript needs.

```
apollo dsp3500 -rompath tools/mame-oracle/out/roms -video none -sound none
  -nothrottle -seconds_to_run 90 -rs232 null_modem -bitb md.txt
```

**Result: zero bytes**, at 20 and at 90 emulated seconds. MAME exits 0; the
capture file stays empty. (`-bitb` needs the file to exist first — it is a
read/write image, and MAME reports "Unable to load image" and continues without
it otherwise. That is a trap worth naming, since the run still succeeds.)

**Where the processor is.** Dumped through `oracle.py run --machine dsp3500`:

| `--at` | PC | IR | SR | ISP | TC / TT0 / TT1 |
| --- | --- | --- | --- | --- | --- |
| 3.0 | `0x00000794` | `0x0828` | `0x2704` | `0x01000180` | all zero |
| 8.0 | `0x000007AE` | `0x0828` | `0x2704` | `0x01000180` | all zero |
| 20.0 | `0x000007AE` | `0x0828` | `0x2704` | `0x01000180` | all zero |

So the PROM *is* executing — the stack is set up and the PC moved between 3 s
and 8 s — and then sits in a short loop around `0x7AE` with the same instruction
register. `SR` is `$2704`: supervisor, **interrupts masked at level 7**, Z set.
`TC`, `TT0` and `TT1` are all zero, so the MMU has never been configured.

**What this rules out and what it does not.** It rules out "the console works
and we captured it wrong": nothing is transmitted. It does not yet distinguish:

1. a legitimate wait on a device the run does not provide — the log shows the
   3c505 network coprocessor looping on unmapped writes and `Network interface
   -1 not found`, and the tape and disk report no media;
2. the PROM selecting the display console rather than the serial one, which on
   real hardware is an NVRAM/service-mode choice the run never makes;
3. a genuine early self-test failure, which with interrupts masked at 7 and no
   MMU would look exactly like this.

**Why it matters beyond the probe path.** The plan gates instruction-execution
timing, the `PROVISIONAL` ATC replacement algorithm, and the supervisor U-bit
reading on "measure against the oracle", and the only measurement route recorded
is MD over the serial console. If that route does not exist, those three items
are gated on something that has not been shown to work — which is a worse
position than being gated on something known hard.

**Next, cheapest first.** Disassemble `0x780`-`0x7C0` of `3500_BOOT_12191_7.bin`
and read what the loop is waiting on: that answers all three possibilities at
once and needs no further MAME runs. Then, if it is (1) or (2), configure what
it wants; if (3), the finding is about the oracle rather than about us, and the
probe path needs a different route — most plausibly injecting probe state
directly, which Phase 1 already lists as the CI path and which needs no firmware
at all.

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
