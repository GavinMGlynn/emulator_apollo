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

**The loop, disassembled.** `3500_BOOT_12191_7.bin` at `$78E`-`$7AE`, decoded by
hand from the bytes:

```
0078E  0828 0000 0002   BTST  #0,($0002,A0)
00794  6600 0078        BNE.W $80E            ; leave
00798  7800             MOVEQ #0,D4
0079A  0828 0000 0012   BTST  #0,($0012,A0)
007A0  6644             BNE.B $7E6            ; leave
007A2  283C 0000 00F0   MOVE.L #$F0,D4
007A8  0828 0000 0102   BTST  #0,($0102,A0)
007AE  67DE             BEQ.B $78E            ; back to the top
```

Three `BTST`s against one base register, and a branch back if the last is clear.
The dumps agree: `IR` is `$0828` — a static `BTST` — at every sample, and the two
sampled PCs (`$794` and `$7AE`) are both inside this loop.

**What it is polling.** `A0` is `$00010401`, and `apollo.cpp` maps
`$010400`-`$0104ff` to the SIO. So the base is the serial controller, addressed
on odd bytes as an 8-bit peripheral on a 16-bit bus, and the three tested bits
are serial status bits. `D2` holds `$45` — ASCII `'E'` — and the instruction just
past the loop's exit is `MOVE.B #$45,($0104,A0)`, a character write.

**So possibility (3) is out: this is not a self-test failure.** The PROM has
reached its console code and is polling the serial controller, spinning while
every status bit it tests stays clear. The natural reading is that it waits for
console input a headless run never supplies.

**That reading was tested and did not hold.** `null_modem` reads its input from
the same `-bitb` image it writes to, so pre-populating the file supplies
keystrokes. Two carriage returns, 25 emulated seconds: the file comes back
unchanged at two bytes. Nothing was transmitted, and nothing was consumed.

**Where that leaves it.** The PROM polls the SIO and the SIO never reports
ready — in either direction. The next question is therefore about the SIO
itself rather than about the PROM: whether MAME's `apollo_sio` reports transmit
ready before the PROM has programmed it, and whether the PROM's programming
sequence runs before this loop at all (the registers written between reset and
`$78E` would say). That is answerable by dumping SIO writes with a Lua tap on
`$010400`-`$0104ff`, which is a small extension to `dump.lua` and needs no new
tooling.

**Meanwhile the plan should not lean on this route.** Phase 1 already lists a
second path — injecting probe state directly into a constructed machine, no
firmware involved — and describes it as the CI path precisely because it does
not depend on the PROM. On this evidence that path is also the one to build
*first*, and MD becomes the development-time confirmation rather than the
foundation.

### C5 — the oracle can be side-loaded and single-stepped, without the PROM

**Status: closed, and it unblocks the instruction-timing item.**

C4 left the measurement route in doubt: the boot PROM does not reach the MD
prompt, and MD over the serial console was the only route the plan recorded. The
same side-loading technique that works on our own machine works on the oracle,
which makes the PROM unnecessary for measurement.

What MAME's Lua binding offers, tested rather than assumed (each call wrapped in
`pcall`, at one emulated second, on `dsp3500`):

| Capability | Plain run | With `-debug -debugger none` |
| --- | --- | --- |
| `cpu.state["PC"].value` | works | works |
| `space:read_u16(addr)` | works | works |
| `space:write_u16(addr, v)` | works | works |
| `cpu.debug` | **nil** | present |
| `cpu.debug:step()` | fails | **works** |
| `cpu:total_cycles()` | not bound | not bound |
| `manager.machine.time` | works | works |

Three things follow.

**Single-stepping needs `-debug`, and `-debugger none` keeps it headless.**
Without `-debug` the `debug` field is nil, so `step()` cannot be called at all.
With both options MAME runs with no window and the script drives it.

**A write to the boot PROM's address range silently does nothing.** Writing
`$4E71` to `$1000` and reading it back returns `$0150` — the PROM's own
contents. The call reports success. So a probe must be side-loaded into *RAM*,
which on this machine is at `$01000000` and up (the reset ISP is `$01000180`),
and a harness that wrote low and never checked would run the PROM while
believing it ran the probe.

**`total_cycles` is not bound in this build**, so a cycle count has to come from
emulated *time*: `manager.machine.time` before and after a step, divided by the
CPU's clock period. That is exact only if MAME advances time in whole cycles,
which is the first thing the timing harness must verify — against an
instruction whose count is not in dispute — rather than assume.

**What this changes.** Instruction execution time, the `PROVISIONAL` ATC
replacement algorithm and the supervisor U-bit reading were all gated on
"measure against the oracle" with no working route. There is now a route that
needs no firmware on either side: side-load the same probe into our machine and
into MAME's, step both, compare. C4 stays open as a finding about the PROM, but
nothing depends on it any more.

### C6 — the timing harness works, and time advances in whole cycles

**Status: closed. `steptime.lua`.**

C5 named one thing to verify before any measured number could be trusted:
`total_cycles` is not bound in this MAME build, so a cycle count has to come
from emulated time divided by the clock period, and that is exact only if MAME
advances time in whole cycles.

**It does.** Stepping `NOP` at `$01001000` on `dsp3500`, whose 68030 runs at
25 MHz so one clock is exactly 40 ns = 40,000,000,000 attoseconds:

```
# step pc_before pc_after attos clocks
0 01001000 01001002 80000000000 2
1 01001002 01001004 80000000000 2
2 01001004 01001006 80000000000 2
```

Every delta is an exact multiple of the clock period. The script prints the raw
attoseconds beside the derived count and flags a non-zero remainder as
`FRACTIONAL`, so this is visible rather than taken on trust — and a fractional
result would be the signal that the whole approach needs revisiting.

**Four instructions, measured.** One word each, stepped in sequence:

| Word | Instruction | Oracle clocks |
| --- | --- | --- |
| `4E71` | `NOP` | 2 |
| `7042` | `MOVEQ #$42,D0` | 2 |
| `D280` | `ADD.L D0,D1` | 2 |
| `E288` | `LSR.L #1,D0` | 4 |

The shift costing more than the rest is the shape one would expect, which is
mild evidence the harness is measuring what it claims to.

**These are the oracle's numbers, not the hardware's.** MAME's 68030 is a
cycle-table model, and `CLAUDE.md` is explicit that this project expects to
out-accurate the oracle. So a disagreement between these and ours is a
*discrepancy to classify*, not a defect to fix by moving ours: the manual is the
arbiter, `[030]` §11.6 is the published check, and
`docs/references/M68030_TIMING.md` already records why those tables are a check
and not a recipe.

**The guard that matters.** The script writes the probe, reads it back, and
**refuses** if the readback disagrees:

```
# ERROR side-load failed at 00001000: wrote 4E71, read 0150
# (a write to the boot PROM's range succeeds and does nothing)
```

Without it a harness aimed at the PROM's range would measure the PROM while
reporting the probe, and every number would be wrong in a way nothing else would
catch. C5 found that trap; this is what closes it.

**One MAME behaviour worth naming.** `manager.machine:exit()` *requests* an
exit; the periodic callback can fire once more before it takes effect, which
printed a spurious extra row after the report's own end marker. A `finished`
flag is what makes the report end where it says it does.

### C7 — our prefetch alternates where the oracle is flat, and the manual says ours is right

**Status: closed. Classification: `oracle-wrong` (an average reported as a point
value). Ours is kept, with the evidence below.**

The first differential measurement, taken by the same method on both sides:
side-load N copies of one instruction, step, and report the interval between
consecutive steps, discarding the first so neither side charges an instruction
for filling the pipe.

| Instruction | Oracle | Ours (bus and cache only) |
| --- | --- | --- |
| `NOP` | 2, 2, 2, 2, 2 | 0, 2, 0, 2, 0, 2 |
| `MOVEQ #$42,D0` | 2 | 0, 2, 0, 2, 0, 2 |
| `ADD.L D0,D1` | 2 | 0, 2, 0, 2, 0, 2 |
| `LSR.L #1,D0` | 4 | 0, 2, 0, 2, 0, 2 |

Two separate things are going on, and only one of them is a gap in ours.

**The alternation is right, and it is the manual's own model.** `[030]` §11.3.3
computes the no-cache case "assuming both caches miss and the associated
instruction prefetches require **one external bus cycle per two instruction
prefetches**". That is exactly the 0/2 pattern: the cache holding register is a
long word, so one external fetch serves two instruction words and the second one
is free. The same section then says why no published number shows it: "the
actual no-cache-case time depends on the **alignment** of prefetches associated
with an instruction, both alignment cases were considered, and the value shown
in the table is the **average** of the odd-word-aligned case and the
even-word-aligned case (rounded up)".

So the oracle's flat 2 is a per-instruction constant with no alignment structure,
which is what a cycle-table model produces. Ours exhibits the alignment
dependence the manual describes as real. `docs/references/M68030_TIMING.md`
predicted this before the core was written — "an emulator that looks up an
instruction's published cycle count and adds it is therefore not cycle-accurate
and cannot be made so by refining the table" — and this is the first measurement
that bears it out.

**The gap in ours is unrelated, and already named.** Every instruction shows the
*same* 0/2, because our clock covers bus and cache time only: the microcode
clocks between the bus cycles are not modelled yet. That is why `LSR.L #1,D0`
costs the oracle 4 and costs us the same as `NOP`. The shift's extra time is
execution time, not bus time, and the completion plan carries it as its own
item.

**What this does not license.** It does not license copying the oracle's numbers
into ours, nor treating the difference for `LSR` as settled. It settles one
thing: when the execution-time item lands, the target is *not* a flat
per-instruction constant that reproduces the oracle. It is a per-instruction
microcode time added to a bus time that keeps alternating -- and the check is
that our average over both alignments matches `[030]` §11.6, not that our
per-instruction figure matches MAME's.

### C8 — seven instructions measured against the oracle, seven agreements

**Status: closed for the instructions listed. No discrepancy to classify.**

The transcription of `[030]` §11.6 into `ap_m68030_timing_table.c` is checked
two ways already: against both published columns on a running machine, and
against the structural patterns that repeat across the tables. This is the third
and most independent check — the oracle's own figures, measured through
`steptime.lua`, which come from MAME's cycle-table model and have no connection
to the manual pages the transcription was read from.

| Instruction | Word | Manual, transcribed | Oracle, measured |
| --- | --- | --- | --- |
| `NOP` | `4E71` | 2 | 2 |
| `MOVEQ #$42,D0` | `7042` | 2 | 2 |
| `ADD.L D0,D1` | `D280` | 2 | 2 |
| `LSR.L #1,D0` | `E288` | 4 | 4 |
| `ASR.L #1,D0` | `E280` | 4 | 4 |
| `ASL.L #1,D0` | `E380` | 6 | 6 |
| `ROXR.L #1,D0` | `E290` | 12 | 12 |

**The `ASL`/`ASR` pair is the most interesting row.** The manual publishes 6
against 4 for the same immediate count; the oracle, independently, measures 6
against 4; and `ap_m68030_alu_shift` — written from the instruction's own page
long before any timing work — tracks `msb_changed` for the arithmetic *left*
shift alone, because "V is set if the most significant bit is changed at any
time during the shift operation" applies to it and to nothing else. Three
sources agreeing on which instruction does more work, one of them explaining
*why*.

**Where this agreement can and cannot discriminate.** Every instruction above
has `CC == NCC` in the published tables, and that is not incidental: their
microcode is long enough to hide a prefetch entirely. On such an instruction a
flat per-instruction model and a scheduled one *cannot* disagree, so the oracle
matching us is confirmation of the figures and not yet of the scheduling.

The rows that would discriminate are the memory-destination forms —
`ADD Dn,EA` is `CC 3` against `NCC 4` — where our core alternates 3 and 4 with
prefetch alignment and a flat model cannot. Those are checked against both
published columns already (`machine_suite`), but **not yet against the oracle**,
because `steptime.lua` sets only the PC and those instructions need an address
register pointing at writable memory.

That is the next extension to the script, and it is where a disagreement is
actually likely: C7 established that the oracle reports a flat constant where
the manual describes an alignment-dependent range.

### C9 — the first real disagreement, and it is ours

**Status: open. Classification: `ours-wrong`. Named as a plan item.**

C8 measured seven instructions against the oracle and got seven agreements,
while recording that none of them could discriminate: every one had `CC == NCC`,
so a flat model and a scheduled one cannot differ on them. The discriminating
rows were the memory-destination forms. `steptime.lua` gained the ability to set
registers, and they were measured.

They disagree.

| | `ADD.B D0,(A0)` |
| --- | --- |
| Oracle, measured | **7**, flat |
| Ours | 3 warm, alternating 3/4 cold |
| `ap_m68030_timing_table` row | `CC 3`, `NCC 4` |

**The oracle is right and the table row is incomplete.** §11.6.8 footnotes
`ADD Dn,EA` with `*` — "Add Fetch Effective Address Time" — and that footnote is
not decoration. `ADD D0,(A0)` reads the memory operand, adds, and writes back,
so it *fetches* an effective address; §11.6.1 gives `(An)` as `3(1/0/0)`, three
clocks including the operand read.

The manual's own composition, Equation (11-2) with the no-overlap assumption of
the no-cache case:

```
  NCC(ADD Dn,EA) + fea(An)  =  4 + 3  =  7
```

which is the oracle's figure exactly. The transcription carries
`needs_effective_address_time` on precisely these rows, so the *fact* was
recorded — but nothing acts on it, and the step adds only the instruction's own
part. Our figure is a component being reported as a total.

**Why nothing caught it before.** The two-sided check compares our totals to
`CC` and `NCC`, and those are the instruction's own columns. An instruction
whose published figure is deliberately partial passes that check while being
incomplete, because the check was built to compare like with like and the row is
not the whole like. The oracle, which has no such structure and simply reports
what the instruction cost, is what exposed it — which is the case for keeping an
independent source even when the internal checks are green.

**What closing it needs.** The `fea`, `cea` and jump effective address tables of
§11.6.1–§11.6.5, transcribed the same way, and the step composing them through
Equation (11-2) rather than (11-1) for the footnoted rows. That is a larger pass
than the instruction tables and it is where `head`/`tail` finally earn their
place: Equation (11-2) overlaps the effective address's tail against the
operation's head, which is why both were transcribed from the start.

Until then the rows stay in the table with the flag set, because a partial
figure that says it is partial is more useful than no figure — but the step
should arguably decline to price them at all rather than report a component.
That is the first question for the plan item.

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

### C10 — the core-board registers, measured because no manual lays them out

`008778-03` Table 2-8 gives each core-board register an address and says
nothing at all about its bits. The manual that carries the layouts is the
*Domain Personal Workstations and Servers Hardware Architecture Handbook*,
which is **not** in `docs/references/` — we hold only `019411-A00`, an addendum
that patches its Chapter 4. So there is no paper route to these registers, and
`CLAUDE.md`'s remaining sanctioned source is the oracle.

That the oracle is GPL and this core is MIT is why this is a **probe** and not
a reading. `CLAUDE.md` says "build and instrument `ext/mame`", and instrumenting
means running it and writing down what happened. `tools/mame-oracle/regprobe.lua`
drives every bit of a register in both directions and records the read-back;
what follows is measurement, reproducible from this checkout, in the same form
as every other figure here.

**Method.** For each bit: read the original, write a value with just that bit
set, read back; write a value with just that bit clear and all others set, read
back; restore. Both directions are needed — a bit that reads set after *both*
writes is stuck high rather than writable, and a probe that only ever wrote ones
would call it read/write.

**Control.** Before concluding that a register is absent, the signature of
absence had to be established. Two addresses in gaps of Table 2-8 —
`016000` and `030000` — were probed identically. Both read `FFFF` with no
writable bit, so *all-ones with nothing writable is what unmapped looks like*
on this machine. That control is what makes the two rows below sayable.

| Register | Address | Measured |
| --- | --- | --- |
| CPU status | `010000` | 16-bit. Bit 15 reads 1 whatever is written. Initial value `8100`; after any write it reads `8000` — bit 8 is latched and **cleared by writing**, and the probe could not restore it. Bits 0–14 read 0 here |
| CPU control | `010100` | 16-bit, **all 16 bits plain read/write storage**. Initial `F700` |
| Cache control | `010200` | **8-bit, not 16.** The byte is aliased across the range: a 16-bit read returns it twice (`EFEF`), and `010201` behaves identically to `010200`. Only **bit 7** is writable; the rest read `6F` |
| Task alias | `010300` | Indistinguishable from unmapped — matches the control signature exactly |
| Latch-page-on-parity-error | `011300` | 16-bit, all bits plain read/write storage. Initial `0000` |
| Master request | `011600` | Indistinguishable from unmapped — matches the control signature exactly |

**What this does and does not settle.** It settles *width, aliasing and which
bits are storage* — the cache control register being a mirrored byte rather than
a 16-bit register is exactly the kind of thing a transcription would have got
wrong and a measurement cannot. It settles **nothing about what any bit means**.
A register whose sixteen bits all store is a register we can implement without
inventing; it is not a register we understand.

Two further limits, stated because they bound how far this may be pushed:

- The CPU status register's bits 0–14 read 0 **in this machine state**. They
  report hardware conditions — timeouts, parity, the service switch — that are
  simply not asserted two emulated seconds into boot. "Reads 0 now" is not
  "unimplemented", and this probe cannot tell those apart without driving the
  conditions.
- Task alias and master request are absent **from the oracle**, which is not the
  same as absent from the hardware. `CLAUDE.md` warns that the driver admits
  gaps, and Table 2-8 lists both registers, so the hardware plainly has them.
  Implementing all-ones would bake an oracle gap into this core as though it
  were a measurement. They stay declined.

Reproducible: two full runs diff byte-identical.

### C11 — the cascade is on IR3, and Table 2-3's priorities were never anomalous

C10 measured registers by reading them. The 8259A cannot be measured that way:
its four initialization command words configure priority, triggering and
vectoring, and **none of them can be read back**. The only way to learn how the
board is wired is to watch the firmware program it.

`tools/mame-oracle/writetrace.lua` installs a write tap over each controller's
range and logs every write. This is the firmware-behaviour evidence route that
`CLAUDE.md` already admits for the ring ("a ring-firmware disassembly
address"), arrived at by running rather than reading. What it records is the
Apollo boot PROM's own initialization — real firmware, not the oracle's opinion.

Data appears byte-replicated across the 32-bit bus and the mask picks the lane,
so `FF000000` is `011000` and `00FF0000` is `011001` — which incidentally
confirms the register pair is at consecutive byte addresses, A0 being address
bit 0.

| Word | Master `011000` | Slave `011100` |
| --- | --- | --- |
| ICW1 | `11` — edge triggered, cascaded, ICW4 to follow | `11` |
| ICW2 | `A0` — vector base | `A8` — vector base |
| ICW3 | `08` — **slave attached to IR3** | `03` — **slave ID 3** |
| ICW4 | `01` — 8086 vectoring; no AEOI, no SFNM, not buffered | `01` |
| OCW1 | `FF` — all masked after initialization | `FF` |

**The finding: the slave is cascaded on IR3, not IR2.** Master ICW3 `08` sets
bit 3, and the slave answers with ID 3; the two agree, which is the check that
this is a wiring fact and not a stray write.

**What that resolves.** `008778-03` Table 2-3 was recorded here as anomalous —
it gives IRQ3 priority 3 and the whole slave group 4+1 through 4+8, so the slave
appeared to be outranked by IRQ3, which on a stock AT it is not. With the
cascade on IR3 the table is *plain fixed priority with no anomaly whatsoever*:
IR0, IR1, IR2, then the slave on IR3, then IR4 through IR7. The prose agrees for
the same reason — it says the slave beats IRQ4 to IRQ7 and never mentions IRQ3,
because in Apollo's numbering "IRQ3" sits on master IR2.

So the manual was right and the assumption was wrong. What was actually being
imported was the AT convention that the cascade lives on IR2, which this board
does not follow. Worth recording as its own kind of error: not a
mis-transcription, but a fact carried in from a *neighbouring* system and never
checked against this one.

**Still open, and now sharper.** Whether Apollo's IRQ numbering maps "IRQ3" to
master IR2 as a naming convention, or whether Table 2-3's IRQ2 and IRQ3 labels
are simply transposed in the scan. The two readings predict identical behaviour
— the priority order and the vectors are the same either way — so nothing in
this core depends on settling it. It is recorded so the next reader of Table 2-3
does not have to rediscover the discrepancy.

Reproducible from this checkout: `APOLLO_TRACE_RANGES="pic1@011000-0110FF,pic2@011100-0111FF"`.

### C12 — which IPL the controllers drive cannot be measured yet, and why

Wiring the interrupt controllers to the CPU needs one more fact than C11
supplied: which of the 68030's seven interrupt levels the master's INT output
drives. `008778-03` §3.2 puts "interrupt priority encoding and vector
generation ... on the logical bus" and gives the parity NMI as "a Level 7
interrupt ... from the Level 7 autovector location in the CPU exception table
(0x07c)", but never states the level for the controllers. `019411-A00` does not
either.

It is not observable in an idle boot, and the reason is worth recording so the
experiment is not repeated:

- Over 15 emulated seconds the boot PROM programs both controllers **once**, at
  0.29 s, and never writes them again. The last word to each is `OCW1 = FF`.
- Forcing them unmasked from Lua changes nothing: sampled at 1 s, **master IRR
  `00` and slave IRR `00`**. No device in the machine is requesting.
- The CPU runs at `SR = 2700` — supervisor, interrupt mask 7 — spinning in a
  short loop. Forcing the mask to 0 as well still produces no acknowledge,
  because there is still nothing asserted to acknowledge.

So all three of the things that would have to line up are absent at once, and
the missing one that matters is a *device*. With no media the PROM never gets
far enough to start anything that interrupts.

**Route out, and it is a plan dependency rather than a research problem.** The
interval timer and calendar (Phase 3's fifth item) are the first devices that
raise interrupts unprompted. Once either runs in the oracle, an acknowledge
occurs and the level falls out of the CPU's own `SR` at that moment. Failing
that, a bootable image would let the PROM reach the same place by itself.

Recorded as blocked rather than guessed. A level picked to look plausible would
be indistinguishable from a measured one in the code, and wrong in a way only a
booting machine would reveal.

**Update, after the MC6840 was read.** Knowing the timer's register model made
it possible to start a device by hand, which moved three of the four unknowns
and left the fourth standing.

*The timer's address mapping, established.* A clean dump of `010800` reads
`00 00 00 00 00 FF 00 FF ...`: even bytes zero, odd bytes `00 00` then six
`FF`s. That is the part on **odd addresses at stride 2** — `RS n` at
`010801 + 2n` — with RS0 (no operation) and RS1 (status, nothing pending) reading
zero and the remaining six reading `FF` because, per `[6840]` §4.1, "if the
latches are not written, they default to $FFFF". Standard 68000 placement for a
byte peripheral, confirmed independently by the latch default. Contrast
`016000`, which reads `FF` throughout: the unmapped signature from C10.

*The calendar is live.* `010900` reads `21 00 09 00 89 00 06 31 07 26` — BCD
seconds, alarm, minutes, hours, weekday, day, month, year. Note for later that
the oracle seeds it from the **host clock**, which is a determinism hazard for
any probe that reads it.

*The timer reaches the controllers as IRQ0, established.* Programmed for
continuous mode with its interrupt enabled, the status register reads `87` —
composite set, and all three timer flags — and the master controller's ISR reads
`01`. That confirms `008778-03` Table 2-3's "IRQO ... MC6840 Timer" by
measurement rather than by transcription.

*The CPU interrupt level, still not established.* An acknowledge cycle does
occur, and the CPU's `SR` reads `2704` at that moment, which is mask 7. That is
suggestive and it is **not** conclusive, because the firmware loop runs at `2700`
anyway: the mask may be the exception raising it to the level, or simply what
the loop had already set.

The obvious discriminator — hold the mask below 7 and see whether the interrupt
still gets through — **invalidated itself**. Forcing `SR` on every periodic
callback produced zero acknowledges at mask 6, mask 5 *and mask 0*, where a
single write of mask 0 had produced one. So the repeated intervention prevents
the exception rather than measuring it, and none of those three runs says
anything about the level.

Recorded that way deliberately. A failed experiment that is reported as a result
is worse than no experiment, and "mask 7 was observed" would read as "level 7"
to anyone skimming.

**Resolved: the controllers drive interrupt level 6.**

The control that unlocked it was the cheapest run of the lot. With nothing able
to interrupt, write `SR = 2000` once and look again later: it reads `2004` half a
second on and two seconds on. **The firmware does not touch SR at all** — the
`2704` seen earlier was the exception raising the mask, not a loop restoring it,
and a forced mask therefore persists. That makes the single-shot sweep valid, and
it also explains why the repeated version failed: writing `SR` on every periodic
callback was never needed and only prevented the exception from being entered.

Sweeping a single write of the mask, timer armed on IRQ0:

| Forced mask | `SR` after | Master ISR | Interrupt taken |
| --- | --- | --- | --- |
| 7 | `2704` | `00` | no |
| 6 | `2604` | `00` | **no** |
| 5 | `2704` | `01` | **yes** |
| 3 | `2704` | `01` | yes |
| 0 | `2704` | `01` | yes |

A mask of 6 permits only level 7 and blocks it; a mask of 5 permits levels 6 and
7 and lets it through. The level is therefore **6**. Reproduced across two
independent runs at each of the two masks that bracket it, and confirmed from
the other side by the master controller's ISR, which reads `01` — IRQ0, the
timer — exactly when the interrupt is taken and `00` when it is not.

Two details worth carrying forward. The `SR` ending at `2704` after a level-6
interrupt is the *handler* setting `2700`, not the exception, which sets mask 6;
and no acknowledge appears on a read tap over the controller's range, so the
vector is not fetched by a bus cycle the tap can see. Whether that means
autovectoring or simply that a CPU-space acknowledge is invisible to a program
space tap is **not** settled here, and `008778-03` §3.2's "interrupt acknowledge
cycle ... a CPU space cycle" says it should be the latter. That question is
separate from the level and does not block wiring.

### C13 — the two DMA controllers sit at different strides

`008778-03` Table 2-8 places "DMA CONTROLLER #1" at `010C00` and "DMA
CONTROLLER #2" at `010D00` and says nothing about how their sixteen registers
map onto those ranges. Following the pattern established for the timer and the
calendar, it was measured before anything was written.

A clean dump of thirty-two bytes at each:

    dma1  010C00: 00 x15  0F   00 x15  0F
    dma2  010D00: 00 x30  0F 0F

**DMA 1 is stride 1**, sixteen registers aliased every sixteen bytes: the `0F`
falls at offset 15 and repeats at 31.

**DMA 2 is stride 2**, so its register 15 lands at offset 30 rather than 15.
Offset 15 reads `00` there, which rules stride 1 out directly rather than by
inference. That matches the ordinary AT arrangement, where the second controller
carries the 16-bit channels and is mapped a word apart.

**Corrected.** This finding first said the `0F` "is the all-mask register
reading all four channels masked". That was an interpretation, and `[8237]`
Figure 6 contradicts it: register 15 is **"Illegal"** to read — the mask register
is write-only, and only status (8) and temporary (13) may be read at all. What
the oracle returns for that illegal read is its own business; `0F` is a
plausible thing for a part to drive there and is not a register value this core
should reproduce.

The stride conclusion is untouched, because it never depended on *what* the
byte was — only on the fact that a distinguishable byte falls at offset 15 on
one controller and offset 30 on the other. Worth separating carefully: the
measurement was sound and the gloss on it was not.

**Divergence recorded.** This core returns zero for a read of any write-only
register, where the oracle returns `0F` at register 15. `[8237]` calls the read
illegal, so neither answer is specified and ours is the one that does not invent
a register value. If firmware is ever seen depending on it, that is the moment
to revisit -- and it would be evidence about the Apollo board's bus rather than
about the part.

Two byte-wide peripherals on adjacent pages with different strides is now the
third such pair on this board — the interval timer is odd-address stride 2 and
the calendar beside it is stride 1. No placement here can be inferred from a
neighbour, and this finding exists mostly to say that with three examples the
rule is now established by induction rather than by luck.

**The part is confirmed an 8237A independently.** Writing `AB` then `CD` to a
channel address register and reading it back returns `AB` — the internal
first/last byte-pointer flip-flop taking two bytes on the write and handing the
low one back first. A device that merely decoded the address would have returned
`CD`.

### C14 — the serial ports, and a warning about dumping registers at all

`008778-03` §3.9: "All ports are implemented using the Signetics 2681 dual
asynchronous control chip", at `010400` and `010500` in Table 2-8. Both are
live. Thirty-two bytes at each:

    sio1 010400: 07 07 0C 0C FF FF 00 00 10 00 5D 5D 00 00 18 18
                 07 07 0C 0C 61 61 00 00 FF FF E0 E0 FF FF FF FF
    sio2 010500: 07 07 0C 0C FF FF 00 00 74 04 11 11 00 00 00 00
                 07 07 0C 0C 61 61 00 00 FF FF E0 E0 FF FF FF FF

**Stride 2**, sixteen registers over thirty-two bytes: every value appears as a
pair because both bytes of each word select the same register. That is the same
placement as the second DMA controller and the interval timer, and different
from the calendar and the first DMA controller — the fourth pair on this board
where the stride had to be measured rather than inferred.

**The exception in the pairs is the finding.** Offsets 8 and 9 read `10 00` on
the first port and `74 04` on the second: *not* a pair. Both bytes address
register 4, which on a 68681 is the input port change register — and reading it
clears it. The probe read the register twice and watched it empty.

**So a register dump is not a passive observation on this part.** Registers 14
and 15 are the start-counter and stop-counter commands, taken on a *read*; the
receive holding registers pop a FIFO; the interrupt status and input-port-change
registers clear. The dump above therefore started a counter and discarded input
state as a side effect of being taken.

That is worth recording beyond this device. Every placement measurement in this
file so far — C10's registers, C12's timer, C13's DMA — has been a read sweep,
and it has been safe because those parts have no read side effects. This one
does, and the next part might. A dump is an experiment, not an observation, and
the fact that it usually behaves like an observation is a property of the parts
so far rather than of the method.

### C15 — the DN3500's disk controller is the OMTI, and there is no disk image

Reconnaissance before starting Phase 4, and it changes the phase's first item.

`docs/COMPLETION_PLAN.md` says "Winchester controllers: OMTI (DN3000), WD7000
ESDI and SCSI (DN4500)", which leaves the reference machine unaccounted for. The
oracle's slot list settles it:

    isa1  wdc  OMTI 8621 ESDI/floppy controller (Apollo)
          ctape  Archive SC-499
          3c505  3Com EtherLink Plus

**The DN3500 uses the OMTI 8621, and it is one controller for both the
Winchester and the floppy** — the slot's own name says "ESDI/floppy", and the
floppy drive options hang off it as `isa1:wdc:omti_fdc:0`. So Phase 4's first
two items are not two devices on this machine; the WD7000 is the DN4500's.
`roms/firmware` carries `3000_OMTI_8621_102640-B.bin`, matching.

**There is no Winchester image to diff against.** The item's stated verification
is an "oracle diff on a real disk image", and `media/` holds none. What it holds
is the Domain/OS SR10.3.5 distribution as **cartridge tape** images — the
`.ct.gz` set including `CRTG_STD_SFW_BOOT_1`, plus an archive of 182 tape files.
The oracle takes `.awd` for its two Winchester slots and `.ct` for the tape.

So the route to a first boot runs through the tape, not the disk: boot the
cartridge, and let it install onto a blank Winchester. That reverses the plan's
implied order, in which storage is built and a disk image supplied.

**One incidental confirmation.** The oracle exposes `node_id` as a *media slot*
taking `.ani` or `.bin`. The node ID PROM this core just gained takes its
identifier from its caller rather than holding a constant, on the grounds that a
device whose purpose is to be unique per machine must not be the same on every
one. The oracle agrees to the point of making it a mountable image.

### C16 — the storage controllers' placement, and a slot that displaces itself

`008778-03` Table 2-9 maps the AT I/O space: the Winchester at `04D000`-`04D007`
(AT `1A0`-`1A7`) and the tape drive at `050000`-`050F80` (AT `218`-`21F`), eight
registers each. Chapter 8 covers the tape controller physically -- dimensions,
connectors, jumpers, LEDs -- and does not give a programming model, the same gap
as the core-board registers of C10.

Dumped in the oracle's **default** configuration:

    tape   050000: 00 40 FF FF FF FF FF FF   (repeating every eight bytes)
    winch  04D000: FF C0 FC 00 FF FF FF FF   (repeating every eight bytes)

Eight registers each at stride 1, aliased -- matching Table 2-9's eight-address
allocations exactly, from a completely independent direction.

**The trap: `-isa1 ctape` removes the disk controller.** Run again with the tape
card placed in slot 1 and the Winchester reads `FF` throughout, the unmapped
signature from C10:

    winch  04D000: FF FF FF FF FF FF FF FF

The DN3500's default configuration already has the OMTI 8621 in `isa1`, so
naming another card for that slot *replaces* it rather than adding to it. A
future comparison wanting both must place them in different slots. Worth
recording because the failure is silent: the machine still runs, the tape still
answers, and only the disk quietly stops existing.

**And `050000` answers identically either way**, with the tape card installed
and without it.

**Retracted, and then settled the other way.**

This entry first reported a negative result: that `050000` reads the same with
`-isa2 ctape` as without, that a differential scan of the whole AT I/O window
found no page changing when the card was added, and therefore that `050000` was
not the cartridge controller.

**That was wrong, and the reason is the whole value of this entry.** The DN3500's
*default* configuration already carries the tape card -- listing the machine's
devices shows `:isa2:ctape` with no flag given at all, beside `:isa1:wdc` and its
disks and floppy. So `-isa2 ctape` added nothing, both arms of the comparison had
the card, and "no page differs" was measuring one configuration against itself.

Tested properly, by *removing* it -- `-isa2 ""`, and again with a different card
in the slot -- `050000` reads `FF` throughout, the unmapped signature. With the
card present it reads `00 40 FF FF FF FF FF FF`.

**So `050000` is the cartridge tape controller, exactly where `008778-03`
Table 2-9 puts it.** Eight registers, aliased on an eight-byte period, and the
manual was right the whole time.

**The lesson, which is the second instance this session.** C12 recorded an
experiment that invalidated itself by perturbing the machine on every clock; this
one invalidated itself by never perturbing it at all. Both produced confident,
well-controlled-looking results, and both were measuring nothing. A differential
is worthless until the control is shown to differ -- and the check is cheap:
enumerate the devices and see what is actually there before assuming a flag added
something.

The original reasoning is left above rather than deleted, because a retraction
that hides what was believed teaches nothing about how it came to be believed.


### C17 — the tape controller has two registers, and the probe cannot map them

With `050000` confirmed as the cartridge controller (C16), the next step was the
register sweep that worked for the core-board registers in C10. It half worked,
and the half that did not is the more useful result.

**What it establishes.** Only two of the eight addresses are live:

    050000  reads 00, no writable bit
    050001  reads 40 at reset, several bits responding
    050002-050007  read FF throughout

Two ports is the classic QIC interface shape -- a data register and a
status/command register -- and it matches `008778-03` Table 2-9 giving the drive
eight addresses of which only the low pair need be decoded.

**What it cannot establish: the bit map of `050001`.** The probe reported the
register as a mixture of `rw`, `ro1`, `ro0` and `inv/w1c` bits, and then reported
that it could not put the register back: `original=40 final=37`.

That note is the finding. `regprobe.lua` works by writing a value, reading it
back, and restoring the original, and its whole method assumes a write is
idempotent and reversible. On a **command** register it is neither -- each write
is an instruction to the controller, the controller's state moves, and every
subsequent bit's "classification" is taken against a different machine. The
readbacks bear this out: they drift across the sweep (`47`, `37`, `D7`) rather
than depending only on the bit being driven.

So the classification above is recorded as *contaminated* and must not be used as
a bit map. This is exactly the hazard C14 predicted for this class of part, one
device later: "a dump is an experiment, not an observation, and the fact that it
usually behaves like an observation is a property of the parts so far rather than
of the method."

**What a sound measurement needs instead.** Not a bit sweep but a protocol: drive
the documented QIC command sequence and watch the status register answer, one
transaction at a time, with a known-good reset between them.

**And the document exists.** `docs/references/archive/` now holds the *Archive
Corporation SC-499 Tape Controller Information Guide* from bitsavers -- the
controller's own manual, with a QIC-02 command description section. It extracts
cleanly.

It confirms the measured span immediately and independently: "BASE ADDRESS +0
(200 HEX): Data/Command Register", a status register at +1, and a control bit 7
that resets the controller's microprocessor. Two live ports, exactly as the sweep
found, arrived at from the opposite direction.

The base address differs -- the guide's is jumper-selected and "factory-set at 200
HEX" where Apollo's Table 2-9 puts the drive at `218`-`21F` -- which is what a
jumper is for, and is why the placement had to be measured on this board rather
than taken from the controller's own manual.

So the bit map is no longer a measurement problem at all: it is transcription
from a manual, which is the cheaper and better source, and the protocol probe is
needed only to *check* the result rather than to derive it.

### C18 — the SC-499's registers, transcribed, and what the sweep had half right

From the *Archive SC-499 Tape Controller Information Guide*, §1.9:

    BASE+0   Data/Command Register        read or write
    BASE+1   Control Register (write) / Status Register (read)
    BASE+2   Start DMA (DMAGO)            "Any write to this register will
                                          cause DMAGO to be active"
    BASE+3   Reset DMA (RSTDMA)           any write asserts RSTDMA

Control register, write: bit 7 resets the controller microprocessor, bit 6 is
"Request to LSI chip", bit 5 enables interrupts ("IEN = 0, masks interrupts"),
bit 4 enables the DONE interrupt. Bits 0-3 unused.

Status register, read, five sources in the order the guide lists them: the
interrupt request flag ("ORing of RDY AND EXC, and DONE if DNIEN is set"), then
Ready and Exception "from LSI chip", Done "from DMA logic", and Direction, which
"indicates direction of bus is from controller to IBM PC".

**This reconciles the sweep rather than contradicting it.** C17 reported "only
two of the eight addresses are live", which was a fair reading of a *read* sweep
and an incomplete account of the part. The guide says "only four of the address
locations are used" -- and the other two are **write-only command addresses**,
triggered by any write regardless of value. A probe that reads finds two ports; a
part that is written has four. Both statements are true and only together are
they the truth.

**One cross-check the measurement supplies.** The guide's OCR loses the status
register's bit *numbers*, listing only the five sources in order. The sweep read
`40` from that register at reset -- bit 6 -- and the second source in the guide's
list is Ready, which is exactly what an idle controller asserts. So the
measurement corroborates Ready at bit 6 and, with it, the guide's list being
ordered downward from bit 7. That is a fact the manual alone could not supply
here and the probe alone could not interpret.

**RSTDMA is specified precisely enough to test**: it "initializes the DMA
sequencer, clears all Control Register bits to 0, and sets DONE to 1 (power-on
reset from the IBM PC performs the same functions)". A reset command that is
defined as equal to power-on reset is the cheapest possible test of both.

### C19 — the reset dump disambiguates the guide's own sentence

Implementing the SC-499 from C18's transcription produced two disagreements with
the measured part, and in both the measurement was right.

**The interrupt flag is a conjunction.** `[SC499]` describes it as "ORing of RDY
AND EXC, and DONE if DNIEN is set", which reads equally as a list of two sources
or as a conjunction of them. The oracle's controller reads `40` at reset -- Ready
set at bit 6, and the flag at bit 7 **clear**. A disjunction would have made the
flag follow Ready and set bit 7 too. So:

    IRQ = (RDY AND EXC) OR (DONE AND DNIEN)

Read as a list it would have interrupted on every idle controller. One byte of
measurement settles a sentence no amount of re-reading would have.

**DONE is clear at reset, against the guide.** It says RSTDMA "sets DONE to 1"
and that power-on reset "performs the same functions", so a reset part should
read `50`, not `40`. It reads `40`.

Not reconciled, and the reason is worth stating: the guide's scan lost the status
register's **bit numbers** entirely -- C18 recovered them by inference from this
same reset value -- so "DONE" may simply not be the bit this core calls DONE. The
core follows the measurement, the divergence is recorded at
`ap_sc499_reset`, and a status read after a real transfer would settle which bit
moves.

**And the write-only addresses float rather than reading zero.** The dump reads
`FF` at `BASE+2` and `BASE+3`. That is the board answering, not the part, so the
part now reports which registers it drives and the board supplies the floating
value for the rest. A part returning zero for an address it does not drive would
be claiming the bus.

### C20 — the OMTI's span, and the limit of what a read sweep can say

The disk controller characterised the same way as the tape, with the control
**verified** rather than assumed -- C16's lesson, applied deliberately: the probe
enumerates the machine's devices and prints whether `:isa1:wdc` is present, so
the two arms are known to differ before their dumps are compared.

    isa1:wdc present = true    04D000: FF C0 FC 00 FF FF FF FF  (repeating)
    isa1:wdc present = false   04D000: FF FF FF FF FF FF FF FF

So the range is the controller's -- it is entirely absent without the card -- and
it aliases on an eight-byte period, matching `008778-03` Table 2-9's eight
addresses at `04D000`-`04D007`.

**What the sweep can say: offsets 1, 2 and 3 are driven**, reading `C0`, `FC` and
`00`.

**What it cannot: anything about offsets 0 and 4 to 7.** They read `FF` with the
card fitted, and `FF` is also what the bus floats to without it. A register
holding `FF` and an address nobody drives are indistinguishable to a reader.
That ambiguity did not arise for the tape controller, where the live registers
happened to hold `00` and `40`, and it is worth naming because the same sweep
produced a clean answer there and a partial one here for no reason the method
controls.

Settling it wants either the controller's own manual -- the OMTI 8621 is a
documented part -- or a write probe, which for a disk controller carries exactly
the command-register hazard that contaminated C17's tape sweep. The manual is the
better route and the cheaper one, as it proved for the SC-499.

**The manual is now in `docs/references/omti/`**: *OMTI 8000 Series IBM PC AT
Intelligent Data Controllers Reference Manual*, Scientific Micro Systems, June
1986. A scan with no text layer, so it reads from page images as the MC6840's
did.

Finding it took the sibling-manual route rather than the obvious one. Bitsavers
does carry an `8621_AT_ESDI/` directory, and it holds only ROM dumps and two
photographs of the board -- no documentation at all. The *series* reference is
what covers the part, which is the same pattern as `019411-A00` covering the
DS3500 where `008778-03` covered only a generation.

**Checked, and the inference was wrong.** The manual's title page lists its
models: OMTI 8100, 8200, 8500 and 8600. Not the 8621. "8000 Series" here means
the 8x00 parts, a different family from the 86xx despite the numbering that made
the inference look safe.

The obvious fallbacks do not cover it either. `OMTI_8640_Technical_Reference_Manual_Jun89`
has a text layer and mentions the 8640 fifty-one times and no other model, and
`OMTI_AT_Controller_Series_Jan87` is an unread scan. Both are in
`docs/references/omti/` for the next attempt.

**And the 8640 is probably the wrong shape anyway.** It presents the standard
IBM PC/AT task file at `1F0`/`170`, eight live registers. Apollo's Table 2-9 puts
the Winchester at AT `1A0`, and C20 measured offsets 1-3 driven with 0 and 4-7
indistinguishable from undriven -- which is not what a task file looks like. That
is consistent with the oracle naming the part "OMTI 8621 ESDI/floppy controller
**(Apollo)**": an Apollo variant with its own interface, not a stock AT
controller at a jumpered address.

So the register model for this part is not, so far, obtainable from a manual. The
routes left are the unread `OMTI_AT_Controller_Series_Jan87` scan, the boot PROM
driving the controller, and a write probe -- which for a disk controller carries
C17's hazard and is the last resort rather than the first.

**The fourth candidate is the right family.** `OMTI_AT_Controller_Series_Jan87`
lists its models on page 3:

    OMTI 8620  Winchesters ESDI and ST506/412 (MFM) and Flexible Disks
    OMTI 8627  Winchesters ESDI and ST412 (2,7 RLL) and Flexible Disks
    OMTI 8120  Winchester ST506/412 (MFM)
    OMTI 8127  Winchester ST412 (2,7 RLL)

The **8620** is the DN3500's part in all but its last digit: a combined ESDI and
*flexible disk* controller, which is exactly what the oracle means by "OMTI 8621
ESDI/floppy controller (Apollo)" and exactly what `008778-03` needs -- one
controller for both, as C15 established. It is the only one of the four
candidates describing a combined part at all; the others are Winchester-only or a
different family.

**And it covers the 8621 outright.** §4.1 does not speak of the 8620 but of the
family: "the OMTI **862X** controller looks like two independent controllers -
one controller for the floppy disk, and one controller for the fixed disk". The
title page's model list was narrower than the text; the manual is the 862X
reference, and the open question from the previous entry is closed.

**Two statements in §4.2 match C20 from the other side.** "There are **four
registers** (or I/O ports) that the host uses to access the fixed disk. The
registers have different meanings when they are read or written. These registers
are normally located at the I/O address listed in table 4-1 but **may be altered
by jumpers**."

Four registers is what the measurement found -- offsets 1, 2 and 3 driven with
offset 0 unreadable either way, which is four addresses of which three answer a
read. And the jumper sentence accounts for Apollo putting the controller at AT
`1A0` where the manual's default is elsewhere, without needing an Apollo-specific
variant to explain it. The 8640's eight-register task file was the wrong shape;
this is the right one.

Also worth carrying: the part "looks like two independent controllers ... through
two independent sets of registers", and §3.4 confirms the hardware matches the
programming view -- "This allows full concurrent operations between these two
sections. For example, DMA data transfer could be occurring at the same time as
programmed Input/Output data transfers are occurring on the fixed disk." A model
treating this as one controller with a mode bit would serialise what the hardware
runs concurrently.

**The shape of the search is the reusable part.** The directory named after the
part held no documentation. The series manual named after its number covered a
different family. The closest sibling by number described a different interface.
The one that worked was named after neither the part nor its number but after the
*bus* -- "AT Controller Series" -- and was found last. Four plausible sources,
and the ordering that looked obvious was the wrong ordering.

### C21 — the OMTI's fixed-disk registers, and the byte that proves it

Transcribed from `OMTI_AT_Controller_Series_Jan87` Tables 4-1 and 4-2.

**Table 4-1, I/O Port Addresses.** Four ports, each meaning different things read
and written, at a default base of `320H` that Apollo has jumpered to AT `1A0`:

    PORT    READ             WRITE
    +0      DATA IN          DATA OUT
    +1      STATUS           RESET (Function)
    +2      CONFIGURATION    SELECT (Function)
    +3      N/A              MASK

Note `+3` reads "N/A" -- there is nothing to read there -- and that `+1` and `+2`
are *function* registers on write: writing them performs a reset and a select
rather than storing a value, in the same way the SC-499's DMA addresses do.

**Table 4-2, the status register:**

    Bit 7   Not Used (Set to 1)
    Bit 6   Not Used (Set to 1)
    Bit 5   IREQ   0 = No Interrupt, 1 = Command Complete
    Bit 4   DREQ   0 = No DMA Request, 1 = DMA Cycle Requested
    Bit 3   BSY    0 = Controller Idle, 1 = Controller Selected
    Bit 2   C/D    0 = word being transferred is data or status
                   1 = byte being transferred is a command or status byte

**And this is where the measurement pays.** C20 read `FF C0 FC 00` from the four
ports of an idle controller. The status register at `+1` read **`C0`** -- bits 7
and 6 set and everything else clear -- which is exactly and only what Table 4-2
predicts for a controller that is idle, not interrupting, not requesting DMA and
not transferring: the two "Not Used (Set to 1)" bits and nothing more.

Two documents that have never met agreeing on a byte is the strongest
confirmation this device will get before it runs. It also retroactively justifies
C20's caution: offset 0 read `FF` and was recorded as indistinguishable from
undriven, and Table 4-1 says it is DATA IN -- a real register that happened to
hold `FF`. The sweep was right not to guess.

**C/D changes the width of the data register**, which is the trap in this part:
"This is an 8 or 16 bit register depending on the state of the controller
(determined by the C/D bit in the STATUS register) ... When the C/D bit is 1,
only bits 0-7 are valid. When C/D is 0 all 16 bits are valid with bits 8-15
containing byte 1 and bits 0-7 containing byte 0." A model with a fixed-width
data register would transfer commands correctly and corrupt every data word, or
the reverse.

### C22 — the OMTI's floppy half is a standard PC controller, elsewhere

Table 4-3, the other of the part's "two independent sets of registers". Five
eight-bit registers, selectable between a primary and a secondary address:

    PRIMARY  SECONDARY  READ                  WRITE
    3F2H     372H       N/A                   Digital Output Register
    3F4H     374H       Main Status Register  N/A
    3F5H     375H       Data Register         Data Register
    3F6H     376H       N/A                   Additional Control Register
    3F7H     377H       Digital Input Register Diskette Control Register

Those are the IBM PC's own floppy addresses. So the OMTI's floppy side is a
conventional PC floppy interface, while its fixed-disk side is the four-port
proprietary set of C21 at a jumpered base. One card, two register sets, two
completely different programming models -- which is exactly what §4.1 meant by
"looks like two independent controllers" and why §3.4 can promise concurrent
operation between them.

**Digital Output Register**, write only, "All bits are cleared when a channel
reset occurs":

    Bit 5  Drive B Motor Enable when 1
    Bit 4  Drive A Motor Enable when 1
    Bit 3  Interrupts and DMA enable when 1
    Bit 2  Reset floppy disk function when 0; "comes out of reset when this bit
           is set to 1"
    Bit 0  Select Drive-A; "A 0 selects drive A, A 1 selects drive B"
    Bits 7, 6, 1 reserved

Bit 2 is inverted against every other control bit in this part -- zero *holds*
reset -- so a driver clearing the register to disable the motors also holds the
floppy in reset, and a model that missed the inversion would come out of reset
exactly when the hardware went in.

**Digital Input Register**, read only: bit 7 "is received from pin 34 of the
floppy disk control cable and is normally used for diskette change status", bits
0-6 reserved.

**Measured: the floppy half is not beside the fixed-disk half.** Every address
in `04D000`-`04DFFF` was read with the card fitted and with `isa1` emptied. The
control is clean -- 384 addresses answer with the card and **none** without it --
and every one of the 384 belongs to the fixed disk:

- they occupy offsets 1, 2 and 3 of each eight-byte block and no others,
- they carry only the values `C0`, `FC` and `00` -- C21's status, configuration
  and mask-port readings,
- and they run from `04D000` to `04D3FB`, so the four-register set aliases
  through exactly 1 KB and the rest of the 4 KB reads `FF`.

No second register block appears anywhere in the range. The natural assumption --
that one card's two halves sit next to each other -- is wrong, and would have
sent a search for the floppy through addresses that provably do not carry it.

**Found: `05F800`.** A page-signature scan of the whole AT I/O window with and
without the card shows exactly two regions differing -- `04D000`-`04D3FF`, the
fixed disk, and `05F800`-`05FBFF`, a second kilobyte 74 KB away.

It dumps as `FF FF FF FF 00 FF 00 80`, repeating every eight bytes, and decodes
straight against Table 4-3 with the block's base at AT `3F0`:

    offset 2  AT 3F2  Digital Output      write only, reads FF
    offset 4  AT 3F4  Main Status         00, an idle controller
    offset 5  AT 3F5  Data                FF
    offset 6  AT 3F6  Additional Control  write only
    offset 7  AT 3F7  Digital Input       80

That last byte is the confirmation. Table 4-3 says the Digital Input Register's
bit 7 "is received from pin 34 of the floppy disk control cable and is normally
used for diskette change status" -- and bit 7 set with no media present is exactly
what a drive with an open door asserts. The one register in the block whose
content the manual predicts is the one that matches.

### C23 — the AT I/O window's mapping rule

Three devices now have both an AT address from `008778-03` and a measured Apollo
address, and one rule fits all three:

    Apollo = 0x040000 + (AT address x 0x80)

    Winchester  AT 1A0  ->  04D000   measured
    tape        AT 200  ->  050000   Table 2-9
    floppy      AT 3F0  ->  05F800   measured

Within a device's block the AT addresses then run as *consecutive Apollo bytes*
-- the fixed disk's four at `04D000`-`04D003`, the floppy's at `05F802` upward --
and each block aliases every eight bytes through 1 KB.

So the window spreads each AT address across `0x80` bytes of Apollo space for the
purpose of *placing* a device, and packs a device's own registers consecutively
within its block. That is why the two halves of one card sit 74 KB apart while
each half's registers sit next to each other: the distance between them is the
distance between `1A0` and `3F0` in AT space, multiplied by 128.

Worth having as a rule rather than three coincidences: any future AT device's
Apollo address can now be predicted from its AT address and then confirmed,
rather than searched for.



### C24 — the cartridge tape image, and its boot record

`media/` holds the Domain/OS SR10.3.5 distribution as `.ct.gz` cartridge images.
Decompressed, `019593-001.CRTG_STD_SFW_BOOT_1` is **53,678,592 bytes, exactly
104,841 blocks of 512 with no remainder** -- so the format is a raw block image
and not a container with a wrapper. That alone is worth knowing: a reader needs
no parsing, only block addressing.

The first block is a boot record. Its first sixteen bytes are four big-endian
32-bit words, followed by identifying text:

    0013D800   word 0
    0013D82A   word 1, which is word 0 + 0x2A
    0013F6BC   word 2, which is word 0 + 0x1EBC (7868)
    56AC0D83   word 3
    "SYSBOOT REV \0\0\0\0 M68K    "

and then, at the offset word 1 points past, MC68000 code: `41FA FFD4` is a
PC-relative `LEA` and `2008` a `MOVE.L A0,D0`.

**Reading, marked as inference.** The shape fits load address, entry point, end
address and a checksum -- word 1 lands exactly where the code begins after the
0x2A-byte header, and word 2 gives a 7868-byte image. That is consistent and it
is not confirmed; the words could equally be three addresses of something else.
What *is* established is that the block carries 68000 code with an ASCII
identification, at a fixed offset, in a raw 512-byte-block image.

The identification is worth having on its own: "SYSBOOT REV" and "M68K" say the
boot record announces both its purpose and its processor, so a reader can
recognise a bootable cartridge without executing anything.

**Why this matters for the phase.** C15 established that the tape is the only
bootable medium present -- no Winchester image exists -- so the first boot must
come from here. This is the block the boot PROM would fetch first, and it is
readable now, before any tape controller command set is modelled.

## Where the ring is not

The Apollo Token Ring has **no runnable oracle at all**: MAME carries Domain
networking over an emulated 3c505 802.3 card instead. Ring figures therefore
never appear in this file as an oracle comparison. They live in
`docs/references/RING.md`, each citing `010005-00`, patent 4,716,575,
`008778-03`, or a ring-firmware disassembly address.

The one exception is the 3c505 itself, which *is* modelled by MAME: when that
path is implemented it gets ordinary rows here, like any other device.
