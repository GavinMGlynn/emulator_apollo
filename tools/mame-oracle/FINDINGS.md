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
and without it. So whatever drives `00 40` there is not the cartridge controller
responding -- either something else decodes the range or the card sits elsewhere.
Not resolved here, and it is the first question to settle before modelling the
tape, because a register model built against that dump would be modelling the
wrong device.

## Where the ring is not

The Apollo Token Ring has **no runnable oracle at all**: MAME carries Domain
networking over an emulated 3c505 802.3 card instead. Ring figures therefore
never appear in this file as an oracle comparison. They live in
`docs/references/RING.md`, each citing `010005-00`, patent 4,716,575,
`008778-03`, or a ring-firmware disassembly address.

The one exception is the 3c505 itself, which *is* modelled by MAME: when that
path is implemented it gets ordinary rows here, like any other device.
