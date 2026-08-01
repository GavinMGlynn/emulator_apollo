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

**Reading, confirmed by the code itself.** The shape fits load address, entry
point, end address and a checksum -- word 1 lands exactly where the code begins
after the 0x2A-byte header, and word 2 gives a 7868-byte image. That much was
arithmetic, and arithmetic alone could not rule out three addresses of something
else.

The first instruction settles it. `41FA FFD4` is `LEA (d16,PC),A0` with a
displacement of -44. The 68000 computes that against the address of the extension
word, which is the instruction's address plus two. **If** the instruction is
executing at word 1:

    0013D82A  entry point, where the code begins
    0013D82C  extension word
       - 002C  displacement
    ============
    0013D800  which is word 0, exactly

So the boot record's own first instruction takes the address of its own header
into A0 -- which is only meaningful if word 1 is where the code runs and word 0 is
where the image sits. The file contains the proof of its own layout, and the two
readings are no longer independent guesses but a single consistent one that the
code depends on.

That is worth more than a manual would be here: a manual would say what the
fields are called, and this says what the firmware actually does with them.
What *is* established is that the block carries 68000 code with an ASCII
identification, at a fixed offset, in a raw 512-byte-block image.

The identification is worth having on its own: "SYSBOOT REV" and "M68K" say the
boot record announces both its purpose and its processor, so a reader can
recognise a bootable cartridge without executing anything.

**Why this matters for the phase.** C15 established that the tape is the only
bootable medium present -- no Winchester image exists -- so the first boot must
come from here. This is the block the boot PROM would fetch first, and it is
readable now, before any tape controller command set is modelled.

### C25 — the QIC-02 command set, and two codes the scan lost

`[SC499]` §1.13: "The SC-499 controller is designed to accept the QIC-02 command
set." §1.13.1 lists it, and the page carries handwritten annotations that the OCR
has mangled into the text -- so this is transcribed from what is legible, with the
gaps named rather than filled.

    SELECT, SOFT LOCK OFF   0000 0001   01     "selects the tape drive"
    SELECT, SOFT LOCK ON    0001 0001   11     as above, plus a cartridge lock
    BOT                     0010 0001   21     "positions the tape ... to BOT"
    ERASE                               --     code not legible
    RETENSION               0010 0100   24
    SELECT Q11 FORMAT                   --     code not legible
    SELECT Q24 FORMAT                   27
    WRITE                               40
    WRITE FILE MARK (WFM)               60
    READ                                80
    READ FILE MARK (RFM)                A0
    READ STATUS                         C0

Two codes are unrecovered: ERASE and SELECT Q11 FORMAT. Both sit in the `2x`
group with BOT, RETENSION and Q24, so their values are constrained -- `22`, `23`,
`25` and `26` are what remain unassigned there -- but constrained is not known.
They are left blank for the same reason the 8259A's one unnamed OCW2 combination
was marked "by elimination": a plausible value written in as fact is
indistinguishable from a transcribed one later.

**Two semantics worth carrying.** SELECT is sticky -- "The drive shall remain
selected until changed by another SELECT command or RESET" -- so it is state, not
a momentary action. And the soft lock is released by more than its own command:
"Execution of the SELECT command or RESET unlocks the cartridge", so a plain
SELECT clears a lock set by the locking variant.

**And one that shapes the drive model.** §1.13: "The SC-499 shall discriminate
between DC300XL and DC600A cartridges by measurement of BOT to LOAD POINT
distance and shall select appropriate basic drive write current." The controller
identifies the cartridge *type* from tape geometry rather than from anything
written on it -- which a `.ct` image, being a raw block image with no geometry
(C24), cannot supply. Whatever models the drive will have to be told the
cartridge type rather than deriving it.

### C26 — the QIC-02 command handshake, and why its timings are ranges

`[SC499]` §1.13.2 is entirely timing *diagrams* -- Figures 1-5 through 1-10 --
and their OCR is unusable waveform fragments. The page images are perfectly
legible. Figure 1-7, "Command Transfer, READY Asserted":

    T1  Bus Data Valid
    T3  Controller Asserts REQUEST     0 us < T1->T3
    T4  Device Deasserts READY         0 us < T3->T4 < 1 us
    T5  Device Asserts READY                 T4->T5 < 500 ms
    T6  Controller Deasserts REQUEST   0 us < T5->T6
    T7  Bus Data Invalid               0 us < T6->T7
    T8  Device Deasserts READY        20 us < T6->T8 < 100 us

So a command is a five-edge exchange: the controller puts the opcode on the bus
and raises REQUEST; the device drops READY to acknowledge it within a
microsecond; the device raises READY again when it has *executed* the command;
the controller drops REQUEST; and the device drops READY a last time to close
the transaction. The diagram marks the T4-to-T5 gap "DEVICE STARTS EXECUTION",
so that 500 ms is a command's execution budget rather than a bus delay.

**Every figure in that table is a bound, not a value.** `T4->T5 < 500 ms` says a
command completes within half a second; it does not say when. `20 us < T6->T8 <
100 us` gives a window 80 microseconds wide. `CLAUDE.md`'s rule for a quantity
published only as a range is to model the documented value, mark it
`PROVISIONAL` in code and in `PROJECT_STATUS.md`, and name it in the plan --
which is what implementing this will require, exactly as the 68030's two-clock
input synchroniser did.

The cheaper observation is that the *ordering* is fully determined even though
the durations are not. A model can carry the five edges in sequence, with no
timing at all, and be right about everything a polling driver can observe --
which is what the current join does implicitly, one byte per access. What it
cannot do is answer a driver that watches for the edges themselves.

One incidental confirmation, from a previous owner rather than from Archive: the
page carries a handwritten annotation against the 500 ms figure reading "accept
interrupt here". Someone else worked out that the command-complete interrupt
falls at T5, and wrote it on the page.

**Figure 1-6, the read data transfer**, is legible too, and is a different
handshake from the command one -- four signals rather than two, and per *byte*:

    T1  Device Changes DIRECTION           the device takes the bus
    T2  Device Asserts READY
    T3  Device Asserts ACKNOWLEDGE
    T4  Bus Data Valid                     T3->T4 < 40 us
    T5  Controller Asserts TRANSFER        0 us < T4->T5
    T6  Device Deasserts READY             0 us < T5->T6 < 1 us
    T7  Device Deasserts ACKNOWLEDGE       0.5 us < T5->T7 < 3 us
    T9  Controller Deasserts TRANSFER      0 us < T7->T9
    T10 Device Asserts ACKNOWLEDGE         (next byte) T10->T11 < 40 ns
    ...  repeating T10-T15 per byte, then
    T16 Device Asserts READY               after the last data octet

So ACKNOWLEDGE and TRANSFER pace each byte, while READY frames the *block*:
asserted before the first octet and again after the last. A byte-at-a-time model
that ignores READY gets the bytes right and cannot tell a driver where a block
ends.

**One rule that is not a timing at all**, and is the most useful line on the
page: "READY shall not be asserted for an EXCEPTION condition." So READY and
EXCEPTION are mutually exclusive by specification, not merely by convention --
a model that raised both, or that raised READY on the exception path, would
present a state the device cannot be in. The current join asserts Exception at
end of tape and leaves the controller's Ready alone, which by this rule is
wrong and is now a named defect rather than an unexamined choice.

The note beside it is worth keeping for the same reason: "If the Controller
asserts TRANSFER before the device asserts READY, then the behavior of READY is
device dependent." A specification declining to define a case is itself
information -- it says a driver must not do that, and that an emulator has no
correct answer to give if one does.

**Figure 1-8 closes it**, and gives the other half of the rule. It is the command
transfer with EXCEPTION already asserted:

    T1  Bus Data Valid
    T2  Controller Asserts REQUEST      0 us < T1->T2
    T3  Device Deasserts EXCEPTION      0 us < T2->T3
    T4  Device Asserts READY           10 us < T3->T4
    T5  Controller Deasserts REQUEST    0 us < T4->T5
    T7  Device Deasserts READY         20 us < T5->T7 < 100 us

So EXCEPTION is cleared *by issuing a command*, and READY rises only after it
falls -- at least ten microseconds after. Figure 1-6 says the two are never both
asserted; Figure 1-8 shows the order in which they change. A driver recovers from
an exception by commanding, not by reading.

The defect recorded above is fixed accordingly, and the invariant moved somewhere
it cannot be forgotten: `ap_sc499_set_exception` sets the condition and clears
ready in one call, rather than leaving two fields for a caller to keep consistent.
An invariant that can be broken by omitting a line is not an invariant.

**Figure 1-10, the status byte transfer**, is a third handshake again -- and the
important thing about it is not the timing but that it *repeats*:

    T1  Device Changes Bus DIRECTION       the device takes the bus
    T2  Bus Data Valid                     0 us < T1->T2
    T3  Device Asserts READY               0 us < T2->T3
    T4  Controller Asserts REQUEST         0 us < T3->T4
    T5  Device Deasserts READY             0 us < T4->T5 < 1 us
    T7  Controller Deasserts REQUEST      20 us < T4->T7
    T8  Bus Data Valid                     0 us < T7->T8
    T9  Device Asserts READY              20 us < T7->T9
    ... T10-T13 repeat T4-T7 for the next byte

The diagram is annotated "ECHO REMAINING STATUS Byte" between the two halves. So
READ STATUS does not return *a* status byte: it returns a status *block*,
transferred one byte at a time by repeating the REQUEST/READY exchange, with
DIRECTION reversed so the device drives -- the same reversal the read data
transfer makes.

**Figure 1-9 completes the command side.** It is the command transfer with
DIRECTION still asserted -- the device holding the bus after a read or a status
block:

    T1  READY Asserted
    T2  Controller Asserts REQUEST      0 us < T1->T2
    T3  Device Deasserted READY               T2->T3
    T4  Device Deasserts DIRECTION      0 us < T3->T4 < 150 us
    T5  Bus Data Valid                        T4->T5 < 1 us
    T6  Device Asserts READY                  T4->T6 < 500 us
    T7  Controller Deasserts REQUEST    0 us < T6->T7
    T10 Device Deasserts READY         20 us < T7->T10 < 100 us

So DIRECTION is the bus-ownership signal, and a command issued while the device
holds the bus requires it to hand the bus back first -- within 150 microseconds --
before the exchange can proceed.

**Which makes the command handshake a state machine with three entry
conditions**, one figure each: Figure 1-7 when the device is ready, Figure 1-8
when it is in exception, Figure 1-9 when it holds the bus. The device's state on
entry selects the sequence. That is the shape to implement, and it is not
guessable from any one of the three -- each looks like the whole protocol until
the next is read.

**How many status bytes, and what they mean, is not on any of these pages.** The
figures give the protocol and not the payload. This core's `READ STATUS` currently succeeds and
returns nothing, which is consistent with the protocol being unmodelled but is
not a status block; a driver asking for status would find the command accepted
and no bytes forthcoming. Named rather than guessed: the conventional QIC-02
status block has a well-known length, and writing that number in from memory is
exactly the move this project does not make.

### C27 — the OMTI's fixed-disk command set

`[OMTI]` §5.1.1. Commands are Command Descriptor Blocks of six bytes, or ten for
`COPY`:

    Byte 0   bits 7-5 Command Class, bits 4-0 Operation Code
    Byte 1   bit 7 C10 (cylinder MSB), bit 6 unused, bit 5 LUN,
             bits 4-0 Head Number
    Byte 2   bits 7-6 C09 and C08, bits 5-0 Sector Number
    Byte 3   Cylinder low, C07-C00
    Byte 4   Interleave (for FORMAT) or Block Count
    Byte 5   bits 7-5 Control Byte, bits 4-0 unused

So addressing is cylinder-head-sector with the **cylinder split across three
bytes** -- eleven bits, C10 in byte 1's top bit, C09 and C08 in byte 2's, and the
low eight in byte 3. Five bits of head and six of sector. A model reading the
cylinder from byte 3 alone would work perfectly on any disk under 256 cylinders
and fail on every real one.

§5.1.2's command set, common to all models:

    00 TEST DRIVE READY      01 RECALIBRATE          03 REQUEST SENSE
    04 FORMAT DRIVE          05 READ VERIFY          06 FORMAT TRACK
    07 FORMAT BAD TRACK      08 READ                 0A WRITE
    0B SEEK                  0D READ ECC BURST LEN   0E READ FROM SECTOR BUF
    0F WRITE TO SECTOR BUF   11 ASSIGN ALTERNATE TRK 1B CHANGE CARTRIDGE
    1E READ DATA TO BUFFER   1F WRITE DATA FROM BUF  20 COPY (10-byte CDB)
    E0 RAM DIAGNOSTICS       E2 READ ID              E3 DRIVE DIAGNOSTIC
    E4 CONTROLLER INT DIAG   E5 READ LONG            E6 WRITE LONG

READ and WRITE carry 1 to 256 *blocks*; REQUEST SENSE, READ ID and ASSIGN
ALTERNATE TRACK carry four bytes; most of the rest carry none.

**The DN3500's controller is the ESDI variant**, so its model-specific commands
are the ones that apply:

    10 CHECK TRACK FORMAT    37 READ ESDI DEFECT LIST (256 bytes)
    EC READ CAPACITY (10 bytes)

and `0C INITIALIZE DRIVE CHARACTERISTICS` is **not** -- it is listed under
"COMMANDS SPECIFIC to the ST506/412 drives". That distinction matters: a model
accepting `0C` on an ESDI controller would accept a command the hardware rejects,
and drive geometry would appear to be settable where it is actually read back
with `READ CAPACITY`.

### C28 — where the boot firmware stops, and what it wants

The SR10.3.5 boot cartridge's image runs 16,933 instructions on flat RAM and
then faults. Where it stops says more than that it stops:

    final PC     0017E81A
    bus errors   5634
    image        0013D800 - 0013F6BC
    RAM          000000   - 0014F6BC

**The final PC is outside the allocated RAM**, not merely outside the image. So
the firmware transferred control somewhere the harness has no memory at all, and
the fault is the fetch failing rather than an instruction this core mis-executed.

**And 5634 bus errors preceded it.** Flat RAM from zero means the core-board
register addresses -- `010000` upward -- are *inside* the RAM and read as zeros
rather than faulting, so those thousands of errors are accesses **above**
`0014F6BC`. The firmware is reaching high, repeatedly, long before it jumps.

**Which fits the machine's real address map.** `008778-03` Table 2-8 puts main
memory at `1000000`-`2FFFFFF`, and everything below `120000` is boot PROM,
core-board registers and the AT bus. A boot image loaded at `0013D800` is not in
main memory at all -- it is in the address space's lower reaches, among devices.

So the harness's flat-RAM-from-zero is the wrong shape for this firmware, and
that is the next thing to change: give the machine the DN3500's actual address
map, with RAM where Table 2-8 puts it and the devices already built mapped where
they belong. The 16,933 figure is the thermometer for whether that helps.

Recorded rather than acted on, because changing the machine's address map is a
larger change than this entry, and because the number is only useful as a
before-and-after if the before is written down.

**Acted on, and the thermometer went the other way.** Routing the machine
through the DN3500's real map takes the run from 16,933 instructions to **zero**,
with one unmapped read and the PC still at the entry point. The image cannot be
placed at all.

The reason is the finding. `0013D800` is not main memory: Table 2-8 puts
`120000`-`FBFFFF` in **AT-compatible bus memory space**, and main memory at
`1000000`. So the boot image's declared load address is not a physical
main-memory address on this machine, and flat-RAM-from-zero was answering it only
because flat RAM answers everything.

That reframes C24's confirmation without weakening it. The boot code's first
instruction really does compute word 0 from word 1, so word 0 really is where the
image expects to find itself -- but "where it expects to find itself" need not be
a physical address. The 68030 boots with translation off, so either the PROM
enables the MMU and maps this range before loading, or the image is placed
physically elsewhere and the header's addresses are logical.

**Confirmed against the oracle: the address is logical.** A dump of the real
machine settles all three possibilities at once:

    0013D800   FF FF FF FF ...   unmapped
    00120000   FF FF FF FF ...   unmapped
    01000000   55 55 55 55 ...   main memory, present
    TC         00000000          translation disabled

So `0013D800` is not physical memory on a DN3500, which is exactly what this
core's address map already said -- the map is right and the zero is correct
behaviour. Main memory is where Table 2-8 puts it, and the MMU is *off* at this
point in the boot.

The only reading left is that the boot image's addresses are **logical**, and
that whatever loads the image enables translation and maps that range first. The
68030 boots with translation off, so the PROM must turn it on; and this core has
had the MMU -- translation control, transparent translation, the ATC and the
table walk -- working for some time, so nothing is missing but the firmware that
programs it.

That also explains why the flat-RAM run got as far as 16,933 instructions: with
memory answering everywhere, a logical address and a physical one are
indistinguishable, and the firmware ran until it needed something flat memory
could not fake.

**The number is not to be repaired by mapping RAM at `0013D800`.** That would
raise the thermometer to something like its old reading and mean nothing, because
the reading would again come from memory that answers rather than from a machine
that matches. 16,933 on flat RAM and 0 on the real map are both honest, and the
second is more informative: it says exactly which assumption was carrying the
first.

### C29 — the boot PROM runs, and stops on one named instruction

C28 concluded that the PROM must run first, because it is what enables
translation. It does, and it works:

    reset SSP    01000180  (main memory)
    reset PC     0000633C  (boot PROM)
    executed     20 instructions
    stopped      UNIMPLEMENTED at 000005FE (boot PROM)
    bus errors   0
    unmapped     0 read, 0 written

**Zero bus errors and zero unmapped accesses.** Everything the PROM touched in
those twenty instructions, this core's address map served -- which is the first
independent check on that map by something other than a test written alongside
it. The reset vector is right too: the PROM's first long word is a stack pointer
in main memory and its second a program counter inside the PROM, and both land in
the regions the map says they should.

That is a far better position than the side-loaded tape reached. 16,933
instructions on flat RAM came from memory that answered everything; twenty
instructions here came from a machine that answers what a DN3500 answers.

**The blocker is now one instruction.** At `000005FE` the PROM holds
`007C 0700`, which is `ORI #$0700,SR` -- setting the interrupt mask to seven, the
ordinary thing firmware does before touching hardware. This core decodes it and
reports `UNIMPLEMENTED`.

`ORI to SR` is a different encoding from `MOVE to SR`, which does work here, and
the same gap almost certainly covers `ANDI to SR`, `EORI to SR` and their CCR
forms -- the immediate-to-status-register group as a whole. That is a small,
bounded piece of the instruction unit, and it is now the single thing between
this core and a PROM that runs on.

Worth noting what made this findable in one step: the machine reports *why* it
stopped and *where*, and the PROM is a file that can be read at that address. A
core that stopped with a generic fault would have needed a debugger to reach the
same sentence.

**Implemented, and the PROM goes further: 20 instructions to 35.** All six of
`ORI`, `ANDI` and `EORI` to `SR` and to `CCR` were missing together, which is why
one instruction blocked the boot -- the decoder knew them and the step had no
semantics for any.

The `ANDI to CCR` case is the one worth a test of its own. A CCR form reaches
only the low byte, so the AND must preserve the high byte rather than AND it
against the discarded half of the immediate. Getting that wrong drops the machine
out of supervisor state silently, in the middle of firmware that has just
finished masking interrupts.

**The next stop is `000028D0`**, whose word is `0C2D` -- `CMPI.B` with a
`(d16,An)` destination. `CMPI` *is* implemented, and the run now shows one
unmapped read, so this is not simply another missing instruction and should not
be assumed to be one. Recorded as the next thing to investigate rather than
diagnosed from the opcode alone.

## C30 -- the stop was never the instruction, and the status was lying

Investigated. `CMPI.B` was not the problem, and neither was `(d16,An)`. The
step was **reporting a bus fault as an unimplemented instruction**.

Every executor signals failure with a bare `false`, and two unrelated things
arrive that way: an instruction this model has no semantics for, and an
instruction whose operand access faulted. The caller turned both into
`UNIMPLEMENTED`. So a perfectly good `CMPI` over an address the board does not
decode reported as a gap in the CPU -- and pointed the investigation at a
decoder that had been correct for weeks.

That is the more dangerous direction of the same error the module's header has
always warned about. The fix carries the distinction on the CPU as
`access_faulted`, set where the access fails and read where the status is
chosen; the PROM now reports `FAULT` at the same PC. **The instruction count did
not move.** Nothing about the machine's behaviour changed, only what it says
about itself -- which is the whole content of this finding.

The lesson is narrower than "test more". The previous three blockers *were* all
missing instructions, and the status agreed each time. A status that has been
right three times running is exactly the kind of evidence that stops being
checked.

### What the PROM is actually reaching for

With the fault correctly attributed, the address is the interesting part. The
faulting read is `0005E801`, from `MOVEA.L #$0005E800,A5` two instructions
earlier. Through the C23 window rule, `Apollo = 0x040000 + (AT x 0x80)`:

| Apollo address | AT address | What sits there on a PC/AT |
|---|---|---|
| `0005E800` | `3D0` | CGA, whose 6845 is at `3D4`/`3D5` |
| `0005D800` | `3B0` | MDA, whose 6845 is at `3B4`/`3B5` |

Both are stored into a table at `(0x138,A6)` and `(0x13C,A6)`, alongside the
constant `000A0000` -- the standard EGA/VGA frame buffer address. The firmware
is **probing for a display adapter** at the two standard PC bases and recording
where each one's frame buffer would live.

Three independent facts agree here: the window rule is already confirmed by
three measured devices, the arithmetic lands exactly on two adapter bases rather
than near them, and the third stored constant is the matching frame buffer
address. That is why this is recorded as a reading rather than a guess. It is
still an inference from the map and not an oracle measurement, so it is marked
**to be confirmed against the oracle** before anything is built on it.

## C31 -- the conclusion was right, the derivation was wrong, and the three facts were not independent

Confirmed against the oracle, which both settles C30's reading and refutes how
it was reached. MAME's DN3500 map:

```
map(0x05d800, 0x05dc07)  apollo_mcr_r/w   Monochrome Controller Registers
map(0x0fa0000, 0x0fdffff) apollo_mgm_r/w  monochrome graphics memory
map(0x05e800, 0x05ec07)  apollo_ccr_r/w   Colour Controller Registers
map(0x0a0000, 0x0bffff)  apollo_cgm_r/w   colour graphics memory
```

So the firmware **is** probing for a display controller, and it is storing
{controller registers, graphics memory} pairs into that table. C30's conclusion
stands.

Its derivation does not. These are **Apollo's own monochrome and colour
controllers, natively mapped** -- not PC MDA and CGA seen through the AT window.
Two things refute the window reading outright:

- The ranges are `0x408` bytes each. An AT window port is one byte on a `0x80`
  stride, so a 1032-byte block cannot be one however the arithmetic comes out.
- `000A0000` is not "the standard EGA/VGA frame buffer". It is the base of
  Apollo's **colour graphics memory**, `0x0a0000-0x0bffff`. The same number for
  an entirely different reason.

### Why this one is worth keeping

C30 said: "Three independent facts agree here ... That is why this is recorded
as a reading rather than a guess."

They were not independent. Apollo's designers placing their controllers at
PC-adjacent numbers is a *single common cause* sitting underneath all three
agreements -- the `3B0`/`3D0` landing, the `0x20`-apart spacing, and the
`A0000` coincidence. Three consequences of one design decision look exactly
like three independent confirmations, and counting them as three is what made a
wrong derivation feel safe.

The check that would have caught it was not more reasoning. It was reading the
oracle's memory map, which took one grep and was available the whole time.
"Confirm before building on it" was the right instinct; the cost of confirming
was so low that doing it *first* would have been cheaper than writing the
inference down.

### What it means for us

Nothing is mis-implemented. The DN3500 config we boot has no graphics
controller, so a bus error is the correct answer to the probe, and that is what
the machine now gives. The graphics controllers are a **new module**, not a
correction to an existing one -- recorded in `docs/COMPLETION_PLAN.md` with the
four regions above.

## Where the ring is not

The Apollo Token Ring has **no runnable oracle at all**: MAME carries Domain
networking over an emulated 3c505 802.3 card instead. Ring figures therefore
never appear in this file as an oracle comparison. They live in
`docs/references/RING.md`, each citing `010005-00`, patent 4,716,575,
`008778-03`, or a ring-firmware disassembly address.

The one exception is the 3c505 itself, which *is* modelled by MAME: when that
path is implemented it gets ordinary rows here, like any other device.

## C32 -- there was never a handler bug: the DN3500 has a graphics controller

The open item was "the run re-enters `000028D0` after the handler returns --
five bus errors at one address, which is why the stack runs out". It was framed
as a question about the PROM's bus error handler, or about what the `$B` frame
gives it. Both framings were wrong, and one grep of the oracle's map settles it.

`dn3500_map` (`ext/mame/src/mame/apollo/apollo.cpp`, lines 673-717) contains:

```
map(0x05d800, 0x05dc07)  monochrome controller registers
map(0x05e800, 0x05ec07)  colour controller registers
```

**A DN3500 answers at `0005E801`.** It never takes a bus error there at all. Our
machine does, because we have not built the graphics controller -- so the
handler runs on a real machine's behalf for a fault that machine never has, and
everything downstream of it (the re-entry, the stack running off the bottom of
RAM, the double fault) is an artefact of a missing device rather than a defect
in anything we built.

There is no handler bug to find. There is a device to build.

### The shape of the mistake

The instruction to characterise a discrepancy before fixing it exists for
exactly this. The observable was "five faults at one address, then a double
fault", and that shape is equally consistent with two very different causes:

- a handler that fails to resolve the fault, and
- a fault that should never have been raised.

Everything about the first is *visible* -- the handler is right there in the
PROM, disassemblable, and it does have a nested-entry path that looks like it
might be misfiring. The second is invisible by construction, because a device we
have not written leaves nothing to look at. Reading the handler felt like
progress and could not have reached the answer.

The three previous findings in this chain (C30's wrong derivation, C31's
correction, the read-only write defect) all resolved the same way: a question
answerable by one grep of the oracle, which reasoning got wrong. That is now a
pattern rather than a coincidence, and worth acting on -- **when a question is
about what the hardware does, ask the oracle before reasoning about it, not
after the reasoning fails.**

### What this does not change

Nothing built in this chain is wasted or wrong. Bus error and address error are
real exceptions this processor takes, they are needed, and they are correct and
tested. The read-only-write fix is right on its own terms. What changes is only
which module comes next: the graphics controller, and not more of the CPU.

## C33 -- the boot PROM's bus errors are its self-test, not our defect

With the cache fixed, the PROM runs 300000 instructions with the PC still inside
the PROM and shows **129 bus errors**, all unmapped *reads*, none written. The
obvious reading is that 129 faults means 129 things missing.

The oracle says otherwise, twice over.

`apollo_unmapped_r` ends:

```
    /* unmapped; access causes a bus error */
    apollo_bus_error();
    return 0xffffffff;
```

So an unmapped read really does bus error on a DN3500, and our behaviour
matches. More usefully, the same function carries:

```
    } else if (address == 0x00030000 && VERBOSE < 2) {
        // omit logging for Bus error test address in DN3500 boot prom and self_test
```

**The boot PROM provokes bus errors deliberately, as part of its self-test.**
MAME's author hit the same noise and silenced it by address. A machine that took
*no* bus errors running this PROM would be the suspicious one.

### What this changes about reading the counters

A bus error count is not a defect signature for this firmware, and the instinct
to drive it to zero is wrong. What matters is *which* addresses fault and
whether the firmware carries on afterwards -- it does, which is what a passing
self-test looks like.

This is the second time in this investigation a clean-looking number was
misleading, and they point opposite ways: five million instructions with zero
faults was a runaway, and 129 faults is a self-test passing. Neither count meant
what it looked like. The first unmapped address is recorded now
(`ap_board_t::first_unmapped_read`) precisely because a count alone cannot
distinguish these, and it reports `FFF90000` -- high space, in the range MAME
leaves commented out as `apollo_f8_r/w`.

### The rule

Report *what* an access touched, not only how many there were. Every counter in
`ap_board_t` that says only "how many" is one an investigation will have to
extend at the moment it matters.

## C34 -- the DN3500 does not print on serial at boot, and the oracle agrees

Phase 1's open item asks for a byte-exact MD session transcript, captured under
the oracle. The first attempt at capturing one settles something else first.

`writetrace.lua` already does the job -- no new probe was needed. Tapping
`010400-0104FF` on `dn3500` for six emulated seconds captures the whole serial
region, and the answer is two writes:

```
25920000000000000 sio1 010408 E0E0E0E0 FF0000
25920280000000000 sio1 010410 77777777 00FF
```

Offset `010408` is register 4 (auxiliary control) and `010410` is register 8
(mode register B). **Neither is a transmit buffer.** The oracle's DN3500 does
not print on its serial port at boot either.

That corroborates our own core, which makes exactly the same number of transmit
writes -- none -- and it means the silence found in C33 was never a defect. Two
independent implementations agreeing is worth more here than either alone,
because the thing being checked is an absence.

### What it means for the MD transcript

An MD session cannot be captured by tapping DN3500 serial at boot, because that
is not where MD talks. With a display and keyboard fitted -- which `dn3500`
has -- the console is the display, and a transcript would have to come from the
frame buffer rather than from a byte stream.

The `dsp` variants are the candidate: they are the diskless server nodes, have
no display in their machine configuration, and must therefore use the serial
port as console.

`dsp3500` does not yet yield a transcript, and **the reason is not what this
finding first said**. The retraction is below, because the mistake is the more
useful half.

- "no dump ... the Lua script did not run to completion" is a harness artefact.
  `oracle.py` already passes `-autoboot_script dump.lua`, and adding another
  appends a second flag, so MAME takes the last and `dump.lua` never runs. Any
  run that substitutes a script reports this, `dn3500` included. That part
  stands.
- The tap timing does **not**. This finding claimed `writetrace.lua` installed
  its tap at "20 emulated seconds" on a screenless machine, blamed
  `emu.register_periodic` being frame-driven, and named the script's own comment
  as the defect. All of that rested on misreading `20000000000000000`
  attoseconds as 20 seconds. **It is 0.02 seconds.** A second is 10^18 attos.
  `dn3500`'s `17458411763588544` is 0.0175 s. The tap was installing early on
  both machines the whole time.

So the real position is simply that `dsp3500` makes **zero** SIO writes in six
emulated seconds, where `dn3500` makes two. That is a fact about the machine and
not about the harness, and why it holds is unknown.

### The retraction is the finding

A fix was written for the imagined defect -- installing the tap at
`add_machine_reset_notifier` instead -- and it was **reverted**, because a change
made for a reason that turned out false should not survive the reason. It was
plausible, it passed a no-regression run on `dn3500`, and it would have read as
a sound improvement in the log forever. What it would not have done is fix
anything.

Two things would have caught this earlier, and both are cheap: printing a
derived unit next to a raw one, and dividing before believing a magnitude. This
project counts time in attoseconds precisely because they are exact; the cost is
that they are unreadable, and an unreadable number invites exactly this.

### Tap alignment

MAME refuses a write tap whose range is not dword-aligned: `010406` is rejected
with "start address has low bits set, did you mean 10404". So a byte register
cannot be tapped alone on this bus, and the whole device range has to be taken
and filtered. Worth recording because the rejection names a *different* address
and it would be easy to accept the suggestion and tap the wrong register.

## C35 -- the oracle sits in the same loop we do, with the same registers

`dsp3500` is running perfectly well. The zero SIO writes in C34 were not a
stalled machine; they are what this firmware does. Dumping its state at six
emulated seconds:

```
A0  00010401     A1  0005D800     A3  00010400     A5  000A0000
A6  01000180     A7  01000180     PC  00000794     SR  00002704
```

Every one of those is a value our own core holds at the same point. `A0` is the
console poll's base, `A3` is SIO1, `A1` is the monochrome controller's register
block, `A5` is the colour graphics memory, and `A6` is the firmware's data base.

The program counters agree too. The oracle stops at `00000794` and ours at
`000007AE`, which are the two ends of the *same three-instruction loop* --
`BTST` SIO1, `BNE`, ... `BTST` SIO2, `BEQ` -- so the difference is only where
each sample fell within one cycle. Feeding our core a character on SIO1
channel A moves it to `00000794` exactly, which is where the oracle is.

### Why this one matters

This is the first **direct state comparison against the oracle on real
firmware** rather than on a probe. Everything before it compared a number we
produced against a number the manuals published, or checked our behaviour
against MAME's *source*. This checks the running machines against each other,
and eight registers and a PC agree.

It also settles C34's leftover honestly. "`dsp3500` makes zero SIO writes where
`dn3500` makes two" looked like a defect to chase. It is the same machine doing
the same thing, and the two writes `dn3500` makes are a screen being configured
on a node that has one. Nothing was wrong.

The lesson is the one the retraction above already paid for, arriving from the
other side: a difference between two runs is not evidence of a fault until the
runs are known to be comparable. Checking whether `dsp3500` was running at all
took one dump and should have come before any theory about why it was quiet.

## C36 -- APOLLO_XXL is bit-rotted, and enabling it does not make the PROM talk

`apollo.h:66` carries `// #define APOLLO_XXL`, commented out. Uncommenting it is
the only way to attach MAME's stdio terminal, because `-listslots dn3500` offers
ISA slots and no serial one.

It does not build. Two call sites have rotted against current MAME:

- `apollo_m.cpp:1274`, `m_tx_w.resolve()` -- `devcb_write_line` has no
  `resolve()` any more; MAME resolves devcb objects automatically.
- `apollo.cpp:908`, `omti8621_device::set_verbose(...)` -- no longer a member.

Both are inside `#ifdef APOLLO_XXL`, so nothing in a default build touches them
and the rot went unnoticed. Removing both calls builds cleanly, and the
incremental rebuild is three translation units and a link -- minutes, not the
full-tree build the plan budgeted for.

**And it changes nothing.** With the stdio terminal compiled in, `dn3500` still
makes two SIO writes in ten emulated seconds -- auxiliary control and mode
register B -- and neither is a transmit buffer. The PROM does not print on
serial with a terminal present any more than without one.

That is consistent rather than disappointing: the terminal is wired to serial 1
channel *B* as an input, and this machine's console is its display. Attaching a
terminal does not change what the firmware decides its console is; that decision
comes from what it found while probing.

### The oracle's checkout is modified, and this says so

Two lines in `ext/mame` are changed and the binary is rebuilt with `APOLLO_XXL`
defined. That is deliberate and it is recorded here rather than left silent,
because a reading taken against a differently-built oracle is not comparable to
one taken before it, and the difference would otherwise be invisible to the next
person -- including a later me.

The next step for the MD item is *not* another oracle build. It is to find what
makes the firmware choose a serial console: on a real DN3500 that is a
configuration setting, and MAME models one -- `apollo_config`. Read that before
running anything else.

## C37 -- service mode does not make the PROM print on serial either

`tools/mame-oracle/mdcapture.lua` is new: it sets the machine configuration
before the run and taps serial 1's two transmit buffers, printing each character
the firmware sends. Two things it got wrong on the way are worth keeping, since
both cost a run each:

- The configuration port is `:apollo_config`, not `:conf`. `APOLLO_CONF_TAG` is
  `"conf"`, which is the *device* tag, and the port is reached by a different
  name. The script now lists the ports it can see when it cannot find the one it
  wants, because "not found" alone cannot distinguish a wrong tag from a machine
  that has no such port.
- MAME refuses a write tap that is not dword-aligned, so a byte register cannot
  be tapped alone. The tap takes the whole device range and filters by **byte
  lane**: which lane of the mask is set decides which register was written, and
  the address alone is not enough on a 32-bit bus.

With the configuration confirmed set -- the script prints
`# Normal/Service = 1` -- the result is:

| machine | service mode | transmit characters in 10-12 s |
|---|---|---|
| `dn3500` | yes | 0 |
| `dsp3500` | yes | 0 |

So service mode is not what makes this PROM talk, and the `dsp` variant is no
different from the workstation here.

### What has actually been established

Four routes to a serial transcript have now been closed by measurement rather
than by argument: a plain `dn3500` run, an `APOLLO_XXL` build with the stdio
terminal compiled in (C36), configuring the display away (impossible -- the port
offers no *none*), and service mode. None produces a byte on either transmit
buffer.

The remaining candidates, in the order they should be tried: tap **serial 2** as
well, since `dsp3500` maps both and the DSP's console may not be serial 1; and
dump *every* SIO write decoded by register, rather than filtering to the two
transmit buffers, so a console being driven some other way is visible instead of
silently excluded. The second is strictly more informative and should probably
have come first -- filtering to the answer you expect is how a search misses the
thing next to it.


## C38 -- the oracle is waiting for the same character our core was

The unfiltered dump, which should have come first. `mdcapture.lua` now reports
*every* write to both DUARTs, decoded by its write-side register name -- which
are not the read-side names, since this part has different registers at the same
address in each direction.

On `dsp3500`, in ten emulated seconds, in service mode: **zero writes to either
serial port.** Not zero characters -- zero writes of any kind, including the
mode and clock-select registers a driver must set before it can send anything.

That is not a machine failing to print. It is a machine that has not got as far
as configuring its serial port, and C35 already said where it is instead: PC
`00000794`, in the console poll loop, reading a status register and branching on
a bit that never sets.

**The oracle is waiting for the same character our core was.** MAME's keyboard
device sends nothing unattended, so nothing ever arrives, and the firmware waits
exactly as ours did before `--boot-input` existed. Two machines, the same loop,
the same cause -- which is the strongest form C35's agreement could have taken.

### What this makes the next step

Feed the oracle a character, the way `--boot-input` feeds ours. Either drive
MAME's `apollo_kbd` device, or inject into the DUART's receiver from lua as our
`ap_sio_receive` does. The second is closer to what we already know works and
does not depend on the keyboard's scan-code encoding.

Worth noting the shape of the whole detour. Four routes were closed by
measurement -- a plain run, the `APOLLO_XXL` terminal, the display
configuration, service mode -- and every one of them was a theory about why the
machine would not *speak*. It was never about speaking. The machine was waiting
to be spoken to, and our own core had already demonstrated that, at
`000007AE`, several days of findings earlier.


## C39 -- the oracle's DUART configuration, decoded, and it is on the odd lane

With every SIO write reported and named, `dn3500` in service mode makes exactly
four in twelve emulated seconds:

```
W sio1 ACR   E0  (010409)
W sio1 CSRB  77  (010413)
W sio2 ACR   80  (010509)
W sio2 CSRA  77  (010503)
```

Two things worth having.

**The addresses are odd.** `010409`, `010413`, `010503`, `010509` -- every one.
The DUART sits on the **odd byte lane** of this 32-bit bus, which is what our
own `ap_sio_decode` already assumes when it shifts the offset right by one, and
this is the first direct confirmation of it from a running machine rather than
from a dump's shape.

**`CSRA`/`CSRB` are both `77`** -- the same clock-select value on both ports,
which is a baud rate the firmware picks for both. `ACR` differs between the two
parts, `E0` against `80`, and bit 7 of ACR is the baud-rate *set* selector, so
both are on set 1 and serial 1 additionally has its counter/timer source
configured. That is a concrete thing to check our own core against, and it is
the first serial configuration this project has read off the hardware rather
than inferred.

## C40 -- posting a character is wired, and has not yet produced output

`mdcapture.lua` now posts text through MAME's natural keyboard once the firmware
has settled -- `APOLLO_MD_POST`, defaulting to a newline at four emulated
seconds. Posted through the natural keyboard rather than bit-banged onto the
serial line, because the DUART's receiver takes bits and driving it directly
would mean getting the baud rate right before finding out whether the idea works
at all.

The post happens and nothing follows -- not in the eight emulated seconds first
tried, and not in the thirty-second run that followed. Both were needed, because
a short window and a post that never arrives look identical.

**It never arrives, and the reason is decisive.** `apollo_kbd.cpp` contains
**zero `PORT_CHAR` entries**. The natural keyboard translates a character to a
key press through exactly those mappings, so with none defined `natkeyboard:post`
has nothing to map to and does nothing at all -- silently, which is why a longer
window was worth running before concluding.

So the route is to drive the keyboard's own ioport fields: set the field for a
key, hold it, release it. `INPUT_PORTS_START( apollo_kbd )` defines them, and
that is where the next attempt starts. It is more work than posting text and it
does not depend on a translation layer this device never provided.


## C41 -- the oracle's firmware responds to a real key press

Driving the keyboard's ioport fields directly works where the natural keyboard
could not. Pressing `ESC` -- found by `PORT_NAME` across `:kbd:keyboard1..4`,
held 0.2 s and released -- produces this, where before there was nothing:

```
# pressed "ESC" on :kbd:keyboard1 at 5.0s
W sio1 CSRB  BB  (010413)
W sio1 ACR   E0  (010409)
W sio1 CSRB  77  (010413)
W sio2 ACR   80  (010509)
W sio2 CSRA  77  (010503)
# released at 6.0s
```

**The firmware reacted.** `CSRB` goes from its configured `77` to `BB` and back,
and both DUARTs are reconfigured afterwards. `CSRB` is clock select -- baud
rate -- so the machine changed the speed of serial 1 channel B on receiving a
keystroke and then restored it. That is the shape of a rate probe, not of a
console echo, and it is the first response to input this project has got out of
the oracle at all.

The key is released as well as pressed. A key that is never released is not a
keystroke: this keyboard is a scanning device reporting transitions, so a
permanently-down key gives one event and then reads as stuck.

### What is now known and what is not

Known: the input route works, the firmware is alive and responsive, and it
answers a keystroke by touching baud rates rather than by printing.

Not known: whether any keystroke produces console output, and if so which. `ESC`
was chosen because it is the first field in `keyboard1` and needed no knowledge
of the keyboard's encoding -- it was a test of the *route*, not a considered
choice of key. The next run should try the keys a boot PROM's console actually
watches for, and should hold the window open well past the response so a slow
banner is not cut off.


## C42 -- the firmware is autobauding channel B, and that is where the terminal is

`Numpad Enter` gives the same shape as `ESC`, and the repetition is what makes it
readable:

```
# pressed "Numpad Enter" at 6.0s
W sio1 CSRB  BB     <- clock select changed
W sio1 ACR   E0
W sio1 CSRB  77     <- and back
W sio2 ACR   80
W sio2 CSRA  77
W sio1 CSRB  BB     <- and again, on the release
```

`CSRB` is serial 1 **channel B**'s clock select. The firmware toggles it between
`77` and `BB` on every keyboard event, reconfiguring both DUARTs in between.
That is **baud-rate detection**: it is cycling the rate of channel B and waiting
for a character that decodes cleanly.

Two facts already recorded now meet. C36 established that MAME's stdio terminal
is wired to `apollo_sio::rx_b_w` -- **channel B**, the very port being probed.
And the keyboard is on channel A, so a keystroke is what *prompts* the probe
without ever being able to *answer* it.

So the machine has been asking a question this whole time, on a port nothing was
answering, and every earlier run watched it ask.

### The next step, and why it is now specific

Send a character into channel B at a rate the firmware accepts. The
`APOLLO_XXL` build from C36 -- which looked like a dead end when it produced no
output -- is what makes this possible, because `apollo_stdio_device` is the only
thing wired to that input. It reads the host's standard input, and `oracle.py`
runs MAME with nothing on stdin.

That is the experiment: pipe a byte in. Whether the rate matters is unknown --
the firmware may accept either of the two it cycles, or neither -- so a null
result needs both tried before it means anything.

Worth noting the earlier judgement this overturns. C36 recorded the
`APOLLO_XXL` rebuild as changing nothing, and it was right that no output
appeared. It was wrong to imply the build was beside the point. The terminal it
compiles in is the only route to the port the firmware is actually listening on.


## C43 -- piping stdin changes nothing, and three things could explain it

Ran `dn3500` with characters on standard input, service mode set and a keystroke
prompting the autobaud probe. The trace is byte-identical to the run without
stdin: the same ten writes, the same `CSRB` toggling, no transmit.

**Three explanations remain and one run cannot separate them.** Naming all three
rather than picking the likeliest, because the last several findings in this
file each cost a run by acting on a plausible one:

1. `oracle.py` may not forward standard input to MAME. It builds a subprocess
   command and nothing in it says stdin is inherited or piped.
2. `apollo_stdio_device` may not be *instantiated* even in the `APOLLO_XXL`
   build. It is added in `dn3500()` inside the guard, and the rebuild was
   confirmed only by the absence of compile errors -- never by observing the
   device exist.
3. The rate may be wrong. The firmware cycles `CSRB` between `77` and `BB`, and
   a character sent at neither rate decodes as noise and is discarded.

The cheapest of the three to settle is the second, and it settles part of the
first for free: list the machine's devices from lua and look for a stdio tag. If
the device is absent, the build did not do what C36 assumed, and nothing about
stdin matters yet. That check is one run and no new code -- `mdcapture.lua`
already enumerates ports when it cannot find one and can enumerate devices the
same way.

### The standing lesson, now with a count

Phase 1's MD item has consumed a long sequence of runs, and the pattern in the
failures is consistent: each closed route was a confident theory acted on before
the cheaper check that would have ranked it. The natural keyboard was tried
before checking for `PORT_CHAR`. The display configuration was reasoned from a
bitmask before reading the port. The `APOLLO_XXL` terminal was built before
finding which port the firmware listens on. Every one of those checks was a
single grep.


## C44 -- the stdio device exists and reads real stdin, so two of three are out

The cheap check, run first this time. Listing the machine's devices from lua:

```
# device :sio2  :sio2:cha  :sio2:chb
# device :sio   :sio:cha   :sio:chb
# device :stdio
# device :kbd   :kbd:mono  :kbd:beep
```

**`:stdio` is there.** The `APOLLO_XXL` rebuild did instantiate the terminal, so
C43's second explanation is out -- and it was out for the price of one two-second
run and four lines of lua, against the several-minute runs spent around it.

And `apollo_stdio_device::poll_timer` contains
`while (::read(STDIN_FILENO, &data, 1) == 1)`. The device reads the process's
real standard input directly, on a timer. So stdin is genuinely the input path;
there is no MAME-side abstraction in between that could be swallowing it.

That leaves C43's first and third, and they are now much narrower:

1. whether the bytes reach MAME's stdin at all -- `oracle.py` builds its
   subprocess without saying anything about stdin, and a pipe through a Python
   wrapper is one more place for it to be consumed or closed;
2. whether the rate is right -- the firmware cycles `CSRB` between `77` and
   `BB`, and a byte at neither decodes as noise.

The first is testable without MAME: check what `oracle.py` does with stdin. The
second needs a run per rate. Do them in that order.

### Two lines of lua would have saved this

The device listing is four lines and two seconds. It was available before the
`APOLLO_XXL` rebuild, before the natural-keyboard attempt, and before the stdin
pipe -- and it would have confirmed at each point what was actually in the
machine rather than what the source implied should be. C43 counted three
occasions where a single grep would have ranked the theories; this is a fourth,
and the first where the check was run before the theory instead of after.


## C45 -- MD talks, and the missing ingredient was *timing*, not rate

```
W sio1 CSRB  BB
W sio1 CRB   45
W sio1 THRB  0D
W sio1 THRB  0A
W sio1 THRB  4D    'M'
W sio1 THRB  44    'D'
W sio1 THRB  37    '7'
W sio1 THRB  0D
```

`THRB` is serial 1 channel B's **transmit** buffer, and the bytes are
`CR LF "MD7" CR`. That is the Mnemonic Debugger's banner. **The oracle is
printing**, on the port C42 identified, after the whole sequence of routes this
file has been closing.

### What made the difference

Not the rate. `subprocess.run` in `oracle.py` passes no `stdin` argument, so the
child inherits it and a pipe *does* reach MAME -- C43's first explanation was
wrong too. The difference is *when* the bytes arrive.

`apollo_stdio_device::poll_timer` reads in a `while` loop until `read` stops
returning 1. Given a pipe with 400 bytes already in it, the first timer callback
drains the lot and then sees EOF -- all of it delivered in one instant, seconds
before the firmware starts probing, and discarded. Feeding one character every
half second instead keeps stdin open and puts bytes in front of the autobaud
*while it is running*, which is the only moment they can be measured.

So the answer to "why is there no output" was never about the machine's
configuration, its build flags, its console choice, or its baud rate. It was that
a probe needs a signal *during* the probe, and a pipe is not a signal, it is a
lump.

Also visible: `CRB 45` immediately before the banner. Bit 2 of the command
register is transmitter enable, so the firmware turns the transmitter on only
once it has found a rate that works -- which is why every earlier run saw
`tx_enabled` clear and every write to `THRB` would have been dropped anyway.
Our own core drops such writes for the same reason (`ap_mc68681`), so the two
implementations agree on that too.

### The transcript

Phase 1's item wants this captured byte-exact into `docs/references/MD.md`. The
capture route now exists end to end and produces the first bytes of it. What
remains is mechanical: run long enough to get a full prompt and a command
response, and write the bytes out as a transcript rather than as a register
trace.

## C46 -- the boot PROM's keyboard table, as data

The keyboard item needs Apollo scan codes, and the PROM carries the table it
matches them against. Recorded here as bytes so the next attempt starts from
evidence rather than from a hex dump, with the structure separated from the
observations about it.

Found by the loop at `00002208` -- `CMP.B (d8,PC,Xn),D1`, `BEQ`, `DBF` -- whose
extension word's displacement resolves to `000021D2`. From there:

```
21D2   CB DB FB C8 D8 F8 C9 D9   ........
21DA   F9 5B 5D 7B 7D CA DA FA   .[]{}...
21E2   CC DC FC DE 0D 0D 1B 5C   .......\
21EA   5C 5C 7C 7C 7F 7B 7D 5B   \\||.{}[
21F2   5D 09 09 09 2F 3F 3F 08   ].../??.
21FA   08 ...                    ..
```

`21FB` onward reads as code rather than table -- `2E 00 01 01 C7 66 18` has no
pattern in common with what precedes it -- so the table is about 41 bytes.

### Observations, not conclusions

- The high bytes fall into **triples on a fixed spacing**: `CB DB FB`,
  `C8 D8 F8`, `C9 D9 F9`, `CA DA FA`, `CC DC FC`. Each is *X*, *X*+`10`,
  *X*+`30`. `DE` appears once without a triple.
- Between and after them are runs that read as ASCII: `[ ] { }`, `CR CR ESC`,
  `\ \ \ | |`, `DEL { } [ ]`, `TAB TAB TAB`, `/ ? ?`, `BS BS`.
- The repeats are the interesting part. `0D` twice, `5C` three times, `09`
  three times, `3F` twice, `08` twice -- several keys produce the same
  character, which is what a table indexed by *position* would look like when
  the parallel array holds the characters.

The loop searches this table **linearly** for the byte in `D1` and branches on a
match, so whatever is in it is what the firmware compares a received byte
against. That is as far as the evidence goes: whether the triples are
unshifted/shifted/control variants of one key, or three separate keys, is not
settled by the bytes alone and should not be guessed. MAME's `apollo_kbd.cpp`
carries the other side of the same conversation and is where to check next.

### The other side: make and break codes

`apollo_kbd.cpp` sends `push_scancode(x)` when a key goes down and
`push_scancode(0x80 + x)` when it comes up -- the classic make/break scheme,
with bit 7 as the release flag and `x` the key's index in the scanned matrix.

That refines the table above rather than settling it. **Every high byte in it
has bit 7 set**: `CB` is the release of key `4B`, `DB` of `5B`, `FB` of `7B`.
So the PROM's table is matching *release* codes, or bit 7 means something else
on this link -- and a table of key-up events is an odd thing for a character
translator to search.

The oddity is recorded rather than explained away. The obvious reading is that
the table holds break codes, and the obvious reading being odd is exactly when
to write down that it is odd instead of picking whichever interpretation makes
it ordinary.

The triple spacing survives the reading intact: `CB DB FB` become keys
`4B 5B 7B`, still *X*, *X*+`10`, *X*+`30`. Whether that is a matrix row stride
or three modifier variants now has a concrete next question -- what `x` values
`apollo_kbd`'s matrix assigns, which its `INPUT_PORTS_START` gives directly.

### The matrix index, and a tension it creates

`scan_keyboard` walks `x` from `0` to `0x7F`, reading
`m_io_keyboard[x / 32]` and testing bit `x % 32`. So a key's scan code is
**port index × 32 + bit position** across the four `keyboard1..4` ports, and
every `x` in that range is a distinct physical key.

Applied to the PROM table's triples, that gives:

| table bytes | keys after clearing bit 7 | port and bit |
| --- | --- | --- |
| `CB DB FB` | `4B 5B 7B` | kbd3 bit 11, kbd3 bit 27, kbd4 bit 27 |
| `C8 D8 F8` | `48 58 78` | kbd3 bit 8, kbd3 bit 24, kbd4 bit 24 |

**This is where the two readings pull apart.** `4B`, `5B` and `7B` differ only
in bits 4 and 5, which is exactly what a modifier encoding looks like -- shift
and control folded into the code. But the matrix says each `x` is its own key at
its own bit, so three separate physical keys that happen to sit at those
positions is equally consistent with everything measured.

Both readings fit. Nothing here distinguishes them, and the distinction matters:
one means a keyboard module synthesises modifiers into the code, the other means
it does not and the PROM's table simply lists three keys. Recorded unresolved,
with what would settle it -- reading which `PORT_NAME` sits at kbd3 bit 11, bit
27 and kbd4 bit 27. If they are the same key's plain, shifted and control forms,
the first reading holds; if they are three unrelated keys, the second does.

### Settled: three separate keys, not modifier variants

The check named above, run: kbd3 bit 11 is `PORT_NAME("Numpad 1")` and kbd3 bit
27 is `PORT_NAME("F10")`. Unrelated keys.

So `4B`, `5B` and `7B` are **three distinct physical keys**, and the triple
spacing in the PROM's table is a consequence of the matrix layout rather than a
modifier encoding. A keyboard module does *not* fold shift and control into the
scan code; it reports the key that moved and lets the firmware's table do the
rest.

That is the reading the arithmetic argued against. `4B`, `5B`, `7B` differing
only in bits 4 and 5 is a strong-looking pattern, and it means nothing here --
the keys simply sit at those matrix positions. Two `PORT_NAME`s settled in one
command what the bit pattern had made look obvious in the other direction.

Which is the whole reason the previous finding recorded both readings instead of
the likelier one. Had the modifier reading been adopted, a keyboard module built
on it would have synthesised codes the hardware never sends, and the PROM would
have matched them -- because the table contains those codes for entirely
different keys.

## C47 -- the "no bootable image" gate is a documented procedure, and we hold the media

Phase 1 records: "All Domain/OS media we hold is *installation* media ... Reaching
a login prompt therefore requires installing Domain/OS from tape onto a blank
disk image under the oracle first. That is a much larger task than this item's
wording implies, and it is the real gate on the first boot."

It is smaller than that, and the reason it looked larger is that nobody had
looked. MAMEDEV's own `Driver:Apollo` wiki page carries a step-by-step SR10.4
install for `dn3500`:

```
rm -f /tmp/dn3500_sr10.4.awd
mame dn3500 -mouse -disk1 /tmp/dn3500_sr10.4.awd \
     -ctape /tmp/019593-001.CRTG_STD_SFW_BOOT_1-REV.A.ct
```

The wiki says a **348 MB image is generated automatically** when `-disk1` names
a file that does not exist. **That is not true of this MAME.** Tried it: the
build refuses with

```
Unable to create image '.../dn3500.awd': No such file or directory (generic:2)
Fatal error: Device OMTI 8621 ESDI disk load (-winchester1 ...) failed
```

and creates nothing, with the output directories present and 166 GB free. The
auto-generation is older behaviour; a forum thread on the same install mentions
in passing that recent MAME needs the file made by hand.

Making it by hand works:

```
truncate -s 348M dn3500.awd
```

which is 364904448 bytes, and MAME then starts the machine with that image and
the boot tape and runs without complaint. **Verified**, not assumed -- the run
was made and reported `Average speed: 77.78%` with no errors.

So the disk image *is* a thing to build, in one command, and the procedure's
first step as published does not work.

Machine Configuration (`ScrLock` then `Tab`): **"25 Years Ago" On, everything
else Off**. Then from the MD prompt:

```
re
di c
ex invol
```

then the calendar, then Domain/OS and the `MINST` utility, taking each tape in
turn, then `shut`.

### We already have every tape it names

```
media/domainos/019593-001.CRTG_STD_SFW_BOOT_1-REV.A.ct.gz   boot
media/domainos/019594-001.CRTG_STD_SFW_1.ct.gz              install 1
media/domainos/019594-002.CRTG_STD_SFW_2.ct.gz              install 2
media/domainos/019594-003.CRTG_STD_SFW_3.ct.gz              install 3
media/domainos/019594-004.CRTG_STD_SFW_4.ct.gz              install 4
```

Exactly the five the wiki lists, filename for filename, gzipped -- and the
procedure says to gunzip them. Nothing needs downloading.

### Why this was missed

The item was written while assembling the media, concluded correctly that all of
it is *installation* media, and reasoned from there that an install was needed
and would be large. Both steps are right. What never happened is asking whether
anyone had already written the procedure down -- and the emulator's own wiki had.

This project's rule is to resolve behaviour from the manuals or the oracle
rather than by trial and error, and it has been read as *our* manuals and *our*
oracle binary. The oracle's **documentation** is a source too, and it answered a
gate estimated at weeks with a page.

### What it unblocks

It also lands on ground this session has just prepared. The procedure is driven
from the **MD prompt** -- and `docs/references/MD.md` now records MD's console
format byte-exact, captured through `mdcapture.lua`, which is what a scripted
rather than hand-driven install would need to parse. The item's verification
asks for exactly that: "produced by a recorded, repeatable install rather than
by hand."


## C48 -- the install is drivable, and the first command runs

Attempted the install from `C47`'s procedure, headless, with the disk image and
boot tape attached and the console captured through `mdcapture.lua`.

The machine boots to MD's `>` prompt with `-disk1` and `-ctape` present -- so the
348 MB image made by hand is accepted by the OMTI device and does not have to
be a real formatted disk to get that far.

Sending `re` produces a **second `MD7` sign-on** in the same capture:

```
..MD7....>....>...(many)...>....MD7....>....>...
```

`re` is *Reset System*. The machine reset and MD signed on again, which is the
command executing rather than being echoed -- the sign-on is output MD only
produces on entry.

So the chain works end to end: characters paced into stdin reach the DUART,
reach MD, are parsed, and a command takes effect. That was the open question
after `C45`, which had got as far as MD *talking* without anything it said
depending on what was sent.

### What was not reached

`di c` produced no visible output and the run ended in prompts. The full
sequence is `re`, `di c`, `ex invol`, the calendar, then Domain/OS and `MINST`
taking five tapes in turn, then `shut` -- each stage interactive and slow, with
tape swaps between them. Driving it needs a session that can hold the state
across stages and check each one's output before sending the next.

What is settled is that nothing structural blocks it. The media is unpacked, the
image command is known, the console is captured byte-exact, and MD executes what
it is sent.

## C49 -- the install session: a pty instead of a pacing loop

`C48` ended on a named obstacle: driving the install past `di c` "needs a
session that can hold state across stages and check each one's output before
sending the next". That session now exists as `tools/mame-oracle/mdsession.py`
and `mdsession.lua`, and building it replaced a workaround with a mechanism.

### The pacing loop was a symptom, and the pty is the cure

`C45` established why a burst of input is lost: `apollo_stdio_device::poll_timer`
reads stdin in a `while` loop until `read` stops returning 1, so a pipe with a
script already in it is drained in one callback and then hits EOF -- the whole
conversation delivered seconds before the autobaud runs, and discarded. The fix
recorded there was to feed one character every 0.4 s from a shell loop, which
keeps the pipe from ending.

That works, and it works by keeping a pipe from ending -- which is treating the
symptom. A **pty** removes the condition: `read` on an empty pty returns
`EAGAIN`, never EOF, so the session stays open with nothing in flight and a
command is written at the moment its prompt appears rather than trickled in
whether or not anything is waiting for it.

The 0.4 s rate does not go away, and should not: it is still exactly right for
*knocking*, where the machine is deaf and characters have to arrive during the
autobaud. What changes is its scope. It was doing two jobs -- keeping the pipe
open and hitting the autobaud window -- and only the second is real. Everything
after the first prompt is now answer-driven, so the pace of the conversation is
the machine's rather than a constant.

### Three defects the stub test found before the machine did

`test_mdsession.py` drives the same logic against a stub that behaves like the
device -- deaf until spoken to, repeating prompt, and able to die on command.
It found two things a real run would have hidden, and one it would have hidden
for hours:

- **A pty comes up in canonical mode with `ICRNL`**, which rewrites every `\r`
  into `\n` on the way to the machine. MD's terminator is `\r`, so the session
  was sending something other than what it said it sent. It worked anyway --
  `poll_timer` maps `\n` back to `\r` -- which is a property of one MAME device
  and not a thing to rest a harness on. The pty is now raw.
- **And with `ECHO` on**, which copies everything written to the master back
  into the master's own read buffer. Nothing reads that buffer here, because
  the console arrives on stdout, so it only fills. A long enough install would
  eventually block in `os.write` with the machine waiting for input the driver
  could no longer send -- a hang arriving hours in, looking like the machine's
  fault.
- **A send is not a delivery.** Closing the session straight after the last
  command kills the emulator before it has read it, and the script reports every
  step done while the last one never happened. The driver now lets the machine
  run on for a settle window. The test caught this twice, once per code path,
  and the second was the one that matters: the final command of an install is
  `shut`.

### The knock interval is load-bearing, and it was the one number picked rather than measured

The driver reaches a prompt by sending carriage returns until the machine
answers, and the first version sent one every **two seconds**. That reached the
power-on prompt every time, and got a sign-on out of `re` **once in four runs**
-- with the success arriving first, which is the worst possible order.

Three runs were then spent looking for a fault in the reset path: the activity
taps were suspected and cleared by an A/B pair, the disk image was checked and
found still virgin, the pty's mode was suspected. All of that was looking at
code that had changed, at a number that had not.

`C45` measured the interval that works: **one character every 0.4 s**. Set to
that, all four commands of the stage run and each `re` answers with a fresh
sign-on. The autobaud after a reset is evidently a narrower window than the one
the machine sits in at power-on -- the power-on case tolerates 2 s, and the
reset case does not.

Two things worth keeping:

- **A missed window does not degrade, it just misses**, and the result is
  indistinguishable from a machine that died in the reset. Nothing in the
  console says "too slow"; there is simply no output.
- The project's rule is that timing figures come from measurement. `C45` had
  already made this one, and the driver reimplemented it from scratch at a
  value that felt reasonable. The rule is not only about figures in
  `src/core` -- a harness that talks to hardware has timing constants too, and
  they are as capable of being wrong.

### MD's sign-on is longer than `MD.md` recorded

Held open on a pty rather than stopped at a fixed emulated second, the sign-on
is:

```
LF "MD7C REV 8.00, 1989/08/16.17:23:52" LF
```

`docs/references/MD.md` recorded "the sign-on is `MD7`, with no version suffix,
no banner text and no copyright line". The bytes it captured are not misread --
`0D 0A 4D 44 37` is the start of this same line -- but `APOLLO_MD_UNTIL=45`
stopped the machine partway through the banner, and the trailing `0D 0A` that
made it look finished is the truncation rather than a terminator.

The correction is recorded there; the general form is worth keeping here.
**A bounded capture proves what it contains and nothing about what follows.** A
capture that ends inside a line looks exactly like one that ends at the end of a
line, and nothing in the capture itself can tell them apart. `C45` even said as
much -- "run long enough to get a full prompt" -- and the conclusion was drawn
before that was done.

### What the documentation answered, since the manuals do not

Three procedural gaps closed from sources outside this project's shelf, which
`C47` established is a legitimate move and this confirms:

- **`DI` selects the device and `EX` executes**, so `di c` means *take standalone
  utilities from the cartridge* and `ex invol` runs one. From the Apollo
  Survival Guide, which lists both commands with their arguments.
- **The cartridge is slow on purpose.** Reaching INVOL's first menu took about
  ten minutes of wall clock with no console output at all. The MAME forum thread
  on this install says cartridge access is "very slow (same as the real
  cartridge tape)", so a silent ten minutes is the expected shape and not a
  hang. `mdsession.lua` now takes `APOLLO_MD_ACTIVITY` and counts accesses to
  the tape and disk controllers, because a silent console cannot distinguish
  those two and a count can. It does:

  ```
  #     60.0s  tape 58445112 r / 8922 w  disk 0 r / 0 w
  ```

  **58 million reads of the tape controller in sixty emulated seconds**, against
  nine thousand writes — a driver spinning on a status register while the
  cartridge moves, which is what a QIC-02 read looks like from the bus. And the
  disk at zero in the same window, correctly: INVOL has been loaded, not yet
  told to do anything. The instrument was added to tell "slow" from "stuck", and
  the first reading it produced did exactly that.

  It also priced itself, which is the second thing an instrument owes. 65
  million reads an emulated minute is 65 million Lua callbacks, and the run
  dropped from the **0.78x** MAME reports untapped to **0.55x** — 60 emulated
  seconds taking 110 wall seconds, measured across two consecutive reports. On a
  ten-minute stage that is four minutes added by the thing watching for whether
  anything is happening.

  So read counting is now **off by default**, and what is left is both cheaper
  and better aimed: writes, which are rare and are the half that matters — a
  stage writing the *disk* is doing the install, where reads only say the bus is
  busy — plus the **program counter** sampled once per report, which costs one
  read and separates a machine looping inside a driver from one stopped at a
  single address. That was the question all along; the read count was a proxy
  for it that happened to be affordable to answer directly.
- **MAME bug 07530**, "Resetting via typing RE crashes the emulator", covers
  `dn3000`, `dn3500` and `dn5500` -- and our stage types `re` twice. Checked
  rather than worried about: it is a regression in 0.216-0.217, fixed in 0.218,
  and `ext/mame` is pinned at v0.289. Both resets ran, each answering with a
  fresh sign-on.

### The machine settled a disagreement between two sources

The MAME wiki gives INVOL options **7, 1, 8**. A `comp.sys.apollo` thread gives
**7, 1, 8, 10** with "first 7 !!!". INVOL's own menu, printed by the machine:

```
 10            - OBSOLETE
```

So the wiki is right for this revision -- `invol (init_volume) - Offline(7),
revision 10.4, December 2, 1991` -- and the newsgroup's fourth step belongs to
an older one. Neither source could have settled it; the program did.

This is the same lesson as `C47` with the sign in the other direction. The
oracle's documentation is a source, and like every source it has a version.


## C50 -- the disk is initialised, and the autobaud wants a carriage return

The install's first stage is done. `INVOL` ran options **7**, **1** and **8**
against the blank image, driven entirely through `mdsession.py`'s command file,
and the 348 Mbyte file went from **every byte zero** to **347,471,186 non-zero
bytes** -- an Apollo physical volume named `dn3500` carrying one logical volume
of 329,399 kbyte and a 640 kbyte OS paging file.

INVOL's own confirmation, read back by option 8 after option 1 had run:

```
Volume built by Invol version "revision 10.4"  on  Nov. 27, 2002

Physical volume "dn3500".  Logical volumes:

 #  size (kB)    name
 1  329399(d)    dn3500
```

### The dialogue, which no manual we hold records

`001746-06` and `002398-04` both mention `invol` and neither prints a prompt of
it. The MAME wiki gives the option numbers and nothing between them. So this was
read from the machine and answered a turn at a time, which is what `--commands`
was built for. In order, with the answer given and why:

| Prompt | Answer | Why |
| --- | --- | --- |
| `Option:` | `7` | initialize physical badspot list, the wiki's first step |
| `Select disk: [w=Winch\|f=Floppy\|q=Quit][ctrl#:][unit#]` | `w` | the Winchester, controller and unit defaulted |
| `Use automated badspot entry?` | `n` | MAME's own driver notes list **Winchester bad-track formatting** as a known gap, so this asks the emulated controller for the one thing its author does not vouch for |
| `Enter badspots ... Terminate badspot entry with a blank line.` | *(blank)* | the medium is a file and cannot have defects |
| `Is the badspot information you entered correct?` | `y` | |
| `Option:` | `1` | initialize virgin physical volume |
| `Physical volume name:` | `dn3500` | |
| `Enter verification option:` | `1` | *no verification*. 2 and 3 write and re-read every block to find media defects; the medium is a file, so this is a long no-op rather than a check |
| `Expected average file size, in kB (CR for default, 5 kB):` | `5` | the stated default, typed rather than defaulted |
| `volume 1:` | `all, dn3500` | one logical volume over the whole disk |
| `Use pre-recorded badspot info?` | `y` | option 7 wrote it, and INVOL confirms: *"The pre-recorded badspot list is empty."* |
| `Option:` | `8` | create the OS paging file |
| `Enter logical volume number:` | `1` | |
| `Size in kB for the OS paging file (CR for default value = 640)` | `640` | the stated default |

Then `Formatting... % complete: 20 40 60 80 100`, `Initialization complete.`

Two prompts state their own default (`CR for default, 5 kB`, `default value =
640`). Both were answered by **typing the stated value** rather than sending an
empty line, which is exact and needs no directive. Worth noting because it is
the general move: a prompt that publishes its default does not need a blank
line to accept it.

### The autobaud wants a carriage return, not merely a character

`C45` records that the firmware's autobaud needs "a character" to arrive while
it is cycling clock-select rates. That is one word too general.

Continuing past INVOL needs `re`, and `re` leaves the machine deaf again. The
running session had no knock directive, so the knock was improvised out of the
one thing its command file could send: a line containing a space, which the
driver sends as `" "` followed by the carriage return it appends. A hundred of
them, paced at 0.4 s, produced **no sign-on at all**.

Measured rather than inferred, as an A/B pair on a fresh machine with
`--knock-char` -- which exists so that this claim is testable:

| Knock | Result |
| --- | --- |
| `\r` | reaches the prompt, `re` answers with a fresh sign-on, exit 0 |
| `" " \r` | **never reaches the prompt at all** -- not after a reset, not even at power-on |

So a leading space does not merely delay the autobaud, it defeats it, and it
defeats it at power-on too where the window is otherwise forgiving. The firmware
is matching a **known byte** to decide whether a candidate rate decoded
correctly, so the first byte it sees has to be the byte it is looking for. Any
other character is not a weaker signal; it is the wrong one.

`!knock` is now a directive of its own rather than a stage's private trick,
because every stage that resets needs it.

### What this cost, and what it did not

The session was lost at that `re` -- the machine sat deaf and had to be stopped.
**The disk did not go with it.** INVOL had already written and exited, the image
was checkpointed to `media/dn3500-invol-done.awd` the moment `Initialization
complete.` appeared, and the live image compares byte-identical to that
checkpoint. The next stage restarts from it rather than from zero.

Which is the argument for checkpointing at every stage boundary of this install:
each stage costs ten minutes of emulated cartridge scan to reach, and the thing
that ends a session is not the thing that was being attempted.


## C51 -- tape swapping, and two ways a harness loses a machine

`MINST` takes four cartridges in turn, so an install driver that can mount only
one at startup is a driver that stops after the first tape. `!swap NAME PATH`
changes a mounted medium while the machine runs.

**Safe on this device for a stated reason**, not because it appeared to work:
`sc499_ctape_image_device` derives from `microtape_image_device` and so from
`magtape_image_device`, whose `is_reset_on_load()` returns **false**. A device
that reset on load would throw the running install away at the exact moment it
asked for the next tape.

### The protocol, and why it carries a sequence number

The driver and `mdsession.lua` share nothing: MAME's stdout is the console and
its stderr is not readable from the driver. So the channel is two files -- a
request the script polls on emulated time, and an acknowledgement it writes
back.

The **sequence number** is what makes waiting mean anything. The same cartridge
can legitimately be asked for twice, and without a number the second request
would be satisfied by the first one's acknowledgement -- the driver would carry
on believing a swap had happened while the machine still held the old tape.

A refused load **fails the run**. A tape that did not mount looks exactly like a
tape that mounted and holds nothing the installer wants, and the second is much
harder to diagnose from a console transcript.

Verified against real MAME -- two swaps, both acknowledged, machine still
running -- and in `oracle_session` against a stub that implements the same
protocol, including the refusal path.

### The swap channel found the path bug a third time

It did not work first time, and the reason was the one this project has now hit
three times: **MAME runs from its own directory**. `--rundir` was passed
relative, the driver wrote the request where it was invoked, and the script
looked for it under `ext/mame`. The swap simply never happened, and the only
symptom available to the driver was a timeout.

`--disk` and `--ctape` were resolved after the first occurrence; `--rundir` was
not, because its other uses -- MAME's own `-nvram_directory` and friends -- were
merely *inconsistent* rather than broken, so nothing had failed loudly enough to
notice. Both it and `--roms` are resolved now.

The general form: when two processes with different working directories share a
**file** rather than a directory, a relative path is not a style question. It is
a channel that silently does not connect.

### Knock for the prompt, not for the sign-on

A stage that resets has to knock, and the obvious thing to knock for is the
sign-on -- it is the thing a reset produces. That is wrong, and it corrupts the
*next* command rather than failing itself.

`MD7C REV 8.00, ...` is printed on entry, which is before MD is ready to be
typed at. A knock that matches the banner returns while the machine is still
settling, and the command sent immediately afterwards arrives split: the console
showed `>r`, a line break, and MD's error code `E` -- the `re` had been broken in
half.

The **prompt** is the only synchronisation point that means ready, because MD
emits it in answer to a carriage return. The built-in `invol` stage always
knocked for `MD_PROMPT` and was never affected; the hand-written command file
did not, and was.

### An orphaned emulator corrupts the next run's transcript

`Session.close()` stops the emulator on every path the driver controls, and none
of those are the paths that matter. A driver killed from outside -- a timeout, a
`pkill`, a terminal going away -- skips `close()` and leaves MAME running with
nobody reading its console.

Observed, and it is worse than untidy. Two orphans held the same log file open
while a new run opened the same path, and the two wrote at different offsets:
the transcript filled with **runs of NUL** between interleaved fragments. The
new run's console looked like the machine was emitting garbage, which is a very
convincing wrong diagnosis.

The child now asks the kernel to kill it when its parent dies
(`PR_SET_PDEATHSIG`), and `oracle_session` checks it by killing a driver and
watching the child go. Two lessons rather than one:

- a cleanup path that only runs on the tidy exits is not a cleanup path;
- and the *symptom* appeared in a completely different subsystem from the cause.
  Nothing about "the console is emitting NUL bytes" points at process lifetime.


## C52 -- the calendar, and one answer chosen on this project's own grounds

`ex calendar` ran in the same live session that INVOL had finished in -- no
restart, because the machine never stopped. Its dialogue, which like INVOL's is
in none of the manuals here:

| Prompt | Answer |
| --- | --- |
| `Please select the disk [w=Winch\|s=Storage mod\|f=Floppy\|q=Quit][ctrl#:][unit#] [,lvno].` `If you do not have a disk, enter none (N):` | `w` |
| `The time-zone is set to 0:00 (UTC).  Would you like to reset it?` | `n` |
| `The calendar date/time is 2002/11/27 11:08:47 UTC.  Would you like to reset it?` | `n` |

**UTC is kept deliberately, not accepted lazily.** This project's rule is that
nothing may depend on the host, and a local time zone is a host fact -- the one
setting here that would make an image built on this machine differ from an image
built on another. UTC is the only zone that is not a property of where the build
happened.

The date is a weaker case and is recorded as such. `2002/11/27` is MAME's host
clock with the driver's "25 Years Ago" configuration applied, so it *is* a host
fact, and it is left alone because nothing observed so far depends on it. If a
Domain/OS licence check or a file timestamp ever does, this is the row to
revisit, and setting it explicitly is one more answer in this table rather than
a redesign.

Also worth noting for what it says about the earlier work: the calendar reports
`0:00 (UTC)` and a sane date **before** anything set them, which means the RTC
this core models as `MC146818A` -- caller-supplied time, never the host's, on
our side -- is being read correctly by real Apollo firmware on the oracle's.


## C53 -- the Domain/OS kernel stops on a clock that runs backwards between sessions

`ex domain_os` loaded the kernel from the boot cartridge -- `low: 01002000
high: 01111FFF start: 01002024`, about 1.1 Mbyte -- and it started:

```
Domain/OS kernel(7), revision 10.4, February 14, 1992  11:42:25 am

The calendar is more than a minute slow.

Switch to service mode, press reset and run CALENDAR.
```

and then **halted**: the program counter sat at `3C456B98` and the tape and
disk counters were identical across two consecutive activity reports. Not slow,
stopped -- which the instrument could say and a silent console could not. The
restore had written 1790 disk sectors and never reached "Restore complete".

### Why the calendar is slow -- measured, with the mechanism left open

The load-bearing fact is the comparison itself: **the volume carries a
timestamp, the RTC carries a time, and the kernel refuses to boot when the
second is behind the first.** The disk persists across sessions and the RTC does
not, so the two are on different clocks the moment an install spans more than
one process lifetime.

What is measured, from two runs of `CALENDAR` at the same point in the
procedure:

| Session | Console reading |
| --- | --- |
| 1 | `2002/11/27 11:08:47 UTC` |
| 2 | `2002/11/27 11:37:14 UTC` |

Those are about 28 minutes apart, and so were the two runs in **wall clock**. So
the RTC is not frozen between sessions and is not reset to a fixed instant --
something outside the emulated machine advances it.

**A first explanation is recorded and withdrawn**, because it is instructive.
The reading was "MAME seeds the RTC to the same instant on every power-on, so
the clock rewinds while the disk marches on". That predicts the two readings
above would be *identical*. They are not, and the second is 28 minutes later,
so the explanation is refuted by the measurement it was written before.

`mc146818_device::device_reset()` genuinely does not touch the time -- that part
holds and is checked in the source. What is **not** established is where the
initial value comes from, and the two readings do not pin it: they track host
wall clock in their *offsets* from each other while matching neither host UTC
nor a fixed constant in their absolute values. Establishing it means
instrumenting the seeding path, which is a separate campaign and is not needed
to finish the install.

What the install needs, and what was done: the calendar was read rather than
set, found to be **ahead** of anything session 1 could have stamped, and the
restore retried in the same session. Setting it forward was considered and
rejected -- a value chosen now becomes the thing the *next* session is behind,
which converts a one-off into a ratchet.

### What follows for the harness, beyond this install

Two things, and the second is the more general.

- **`--keep-rundir` from here on.** The driver wipes nvram, cfg and state at
  startup so that no run depends on what the last one left behind -- exactly
  right for an oracle *reading*, and wrong for an install, which is one long
  operation split across process lifetimes. The calendar lives in that NVRAM.
  A setting made in one session and wiped before the next is not a setting.
- **A reset is not free on this machine.** `apollo_state::machine_reset` reads
  the RTC year and shifts it -- `+75` when it is under 25 with "25 Years Ago"
  set, `-70` when it is 70 or over with neither shift configured. It runs on
  *every* reset, not once at power-on, so a session that resets repeatedly is
  moving its own clock around. The install procedure calls for `re` twice
  before each utility, which is three or four shifts before the OS ever starts.
  Stages after this one send no `re` at all: a fresh power-on already is a
  reset, and the fewer of them between setting the clock and using it, the
  better.

Recorded because it is a class, not an incident: **a clock the harness restarts,
compared against a disk the harness does not**, will fail this way in any
setting where a machine is run across more than one sitting. The symptom appears
in the guest, thousands of instructions from the cause, and reads as a fault in
the thing being installed.

And a second, about this file rather than the machine. The withdrawn
explanation above was written from reading MAME's source and was consistent with
every observation available *at the time* -- one reading. It took a second
reading to refute it, and the second reading cost nothing but was not taken
until the explanation had already been committed. A mechanism inferred from one
sample is a hypothesis however good the source-reading behind it looks.


## C54 -- "and risk volume" is an offer to decline, and reverting beats salvaging

With the calendar past, the kernel got one step further and stopped on:

```
BOOT VOLUME NEEDS SALVAGING.
Proceed to bring up OS (and risk volume)?
```

The volume was unclean because the *previous* attempt's kernel had written 1790
sectors and then been stopped -- the halt of `C53` left a mount marker behind.
So the second attempt inherited the first attempt's wreckage, which is worth
noting on its own: **a failed stage is not a no-op**, and on a machine with
persistent storage the next attempt does not start where the last one did.

Answered `n`, which returns to MD with a crash status and a prompt:

```
Crash_Status 00010005  PC 3C456A56 pid 0001
S   3C42BA58       2700         BC
3C42BA58: 4E4F
>
```

### Why decline

The prompt is honest about what it is offering, and the offer is bad here for
two reasons that are specific rather than cautious:

- The product of this work is a **reference artifact**. A filesystem that is
  subtly inconsistent is exactly the class of defect that survives into every
  later use, and that gets attributed to whatever is running at the time rather
  than to an install performed weeks earlier.
- A clean checkpoint existed, so accepting the risk bought nothing. Risk is
  worth taking when the alternative is losing work; here the alternative was
  copying a file.

### Why revert rather than salvage

`SALVOL` is the documented remedy and the Apollo Survival Guide names it
directly. It was not used, and the reasoning is the same one the checkpoints
exist for:

- salvaging costs a **ten-minute emulated tape load** to *repair* a volume, and
  reverting costs a file copy to *have the volume back*;
- and the results are not equivalent. A salvaged volume is one that was damaged
  and then mended; the checkpoint is one that was never mounted at all. For an
  artifact meant to be a known-good starting point, "never damaged" is a
  materially better property than "repaired", and it is available for less.

Recorded because the cheaper option is also the better one here, which is not
the usual shape and is easy to talk oneself out of. `SALVOL` remains the right
answer on real hardware, where there is no checkpoint to go back to.


## C55 -- Domain/OS runs, and `go` gives a server with no console

The fourth restore attempt reached `Restore complete.` and the Phase II prompt,
and `go` brought the operating system up:

```
)go
Loading Init.
... loading global libraries
... global libraries loaded.
*  Copyright (c)  Hewlett-Packard Co., 1986-1992.                          *
*****  Node startup on   *****

Looking for orphaned files
Preserving editor files
Clearing /tmp
Initializing /etc/mnttab
Starting standard daemons:.
Starting window system:.

SPM system init complete.
Node ID = 12345

    SERVER_PROCESS_MANAGER, Version 10.2, 89/07/31
 SPM Initialized on Monday, December 2, 2002 at 21:10:25
```

**Domain/OS SR10.4 runs on the oracle**, from a disk this project built: init,
global libraries, daemons, the window system, and the node's own ID. That is the
first time an Apollo *operating system* rather than an Apollo *utility* has come
up here.

### And then it stops being useful, correctly

`go` started the **Server Process Manager**, not the Display Manager. SPM is what
a headless node runs: it takes work from other nodes over the network and offers
**no login on the console**. Carriage returns sent to it come back as bare
newlines and nothing else -- measured, not assumed, because "the console is
silent" has already meant three different things in this campaign.

So the MAME wiki's next step, "login as `user`, then `/install/tools/minst`",
assumes the **Display Manager on a screen**. Headless, there is no screen to log
in on, and this is the console question that was flagged as open before the OS
ever booted: it is now answered, and the answer is that `go` is the wrong door.

The Apollo Survival Guide has the right one, in its list of Phase II commands:

> **SH** - Starts non-graphical, text-based single-user shell for command
> execution

which is exactly what an installer driven over a serial line needs.

### The cost, and what made it cheap

One restore -- about twenty-five minutes of emulated cartridge -- because
reaching `)` costs that and `go` cannot be undone from inside SPM. The *disk*
cost nothing: `dn3500-osclean.awd` was taken after the previous `shut`, so the
volume went back to a known-good cleanly-shut-down state with a file copy.

That is now three separate occasions where a checkpoint turned a lost session
into a lost twenty-five minutes. The rule earns its place: **checkpoint at every
stage boundary, before the irreversible step, not after it.**


## C56 -- the oracle does not model changing a cartridge while the machine runs

`sh` gave a real console: `login:`, `user` with an empty password, and a `$`
prompt as `user.none.none` on the local registry. `/install/tools/minst` started
-- RAI MINST 2.37, 09 Dec 91 -- and answered its way through to

```
Please put volume 1 of the media for Domain/OS into the drive.
Press the <RETURN> key when ready:
```

`!swap` mounted `019594-001.CRTG_STD_SFW_1.ct` and the script acknowledged it
`ok` with the machine still running. Then, on `RETURN`:

```
Retensioning cartridge tape... Please wait.

<NUL> 4ini<BS> <BS>   pHP i ... t+<TAB>dV_s i<NUL>5sd4wsyspo/_mBt<TAB>/o/lsrep ...
Crash_Status 000B0008  PC 3C423688 pid 0002
```

Binary on the console, then **Domain/OS crashed**.

### Why, from the oracle's source rather than from the symptom

`sc499_device::check_tape()` is what notices a new cartridge: it resets the tape
status, sets Beginning-of-Media, and recomputes `m_image_length` and
`m_ctape_block_count`. It is called from exactly three places -- `device_reset`,
and `read_block`/`write_block` **only when `m_tape_pos == 0`**.

And `sc499` registers **no media-change notifier at all**, though MAME's image
layer offers one (`add_media_change_notifier`, bound in the Lua engine). So
nothing in the device tells a *running driver* that the medium underneath it has
been replaced.

That explains both halves of what we have seen, which is the test of an
explanation:

- **At the MD prompt it works.** `ex <utility>` starts reading at block 0, so
  `check_tape()` runs on the first read and the device re-learns the cartridge.
  Two swaps were verified that way, and the driver's own test still passes.
- **Under Domain/OS it does not.** The OS driver holds its own state across the
  swap, and `m_ctape_data` has been resized to zero and refilled from a
  different-length image beneath it. What it reads afterwards is not what it
  thinks it is reading.

### What this blocks, and the one honest way round it

`MINST` takes **four** cartridges in turn, and every one of them is a change
made while the operating system holds the drive. So the install cannot be
finished on this oracle as it stands. This is not a defect in the driver, the
procedure, or the media -- it is a gap in the emulated device, and the symptom
appears three layers away from it as an OS crash.

The route is to fix the **oracle**, which this project already builds and
modifies (`APOLLO_XXL` is a local edit, and `ext/mame` carries three). Resetting
the tape position and re-running `check_tape()` when an image is loaded is a
small, local change in `sc499_ctape_image_device::call_load()`, and it makes the
emulated drive do what a physical one does when a cartridge is pushed in.

Recorded as its own finding because of what it cost to see: the swap reported
`ok`, the machine kept running, and the failure arrived one command later
wearing the costume of a filesystem problem.

### The obvious fix was tried, and it is not sufficient

`sc499_device` was given the media-change notifier it lacks -- MAME's image
layer fires one on every load and unload (`diimage.cpp`, both paths verified in
source), and the handler did what pushing a cartridge in does: forced
`check_tape()` to recompute length and block count, and reset the tape position,
block index and data index. Deliberately narrower than `device_reset()`, since
changing a cartridge does not reset the controller a driver is talking to.

It **changed the outcome without fixing it.** Domain/OS got measurably further --
twenty-one kilobytes of output rather than a prompt crash -- and then crashed
anyway. So the device's *geometry* was not the whole problem: something the OS
driver expects on a cartridge change is still not happening. The likely
candidate is the exception path -- `device_reset` carries a commented-out
`SC499_STAT_EXC`, and a real drive raises an exception so the host re-reads
status -- but that is a guess, and guessing at what a driver expects is the
trial-and-error this project's rules forbid.

**The patch has been reverted and the oracle rebuilt to stock.** Carrying an
unproven local modification is worse than carrying none: every reading taken
against it would need this caveat attached, for a change that did not achieve
what it was made for. `ext/mame` is back to its three `APOLLO_XXL` edits.

### Closed, and the answer was already in the oracle

The investigation above was done and it took two sources, neither of them a
guess.

**The SC-499 guide equates the two events in as many words.** Under the READ
command: *"A READ command following cartridge insertion or RESET shall commence
at BOT, otherwise the read command commences from the current tape position."*
It says the same for WRITE, WRITE-FILE-MARK and READ-FILE-MARK. So an insertion
is required to leave the drive in the state a RESET leaves it in -- that is the
standard's own wording, not an inference from behaviour.

**And `SC499_ST1_POR` is the mechanism.** QIC-02 status byte 1, bit 0, "power
on/reset occurred": how a drive tells the host to re-initialise. MAME defines it
and sets it in exactly one place -- `sc499_device::do_reset()`, which also
asserts EXCEPTION, rewinds to BOT and clears the readahead and pending-read
state.

So the whole behaviour was already written and simply **unreachable except
through the QIC-02 RESET command**. Nothing invoked it on a media change. The
fix is four lines: on a media change, recompute the geometry with `check_tape()`
-- first, because `do_reset()` sets POR and BOM only `if (m_has_cartridge)` --
and then call `do_reset()`.

This also explains why the first attempt failed, which is the test of the
explanation. It reset the position and geometry but never set POR and never
asserted EXCEPTION, so Domain/OS was never *told*: it read from a rewound tape it
still believed was the old one. "Got further and still crashed" was exactly the
right symptom for that cause.

**Verified on the machine.** With the patch in, swapping the cartridge at
MINST's prompt produces:

```
Rbak Command Line: .../rbak_sr10 -dev ct -f 1 install -as //node_12345/install ...

Label:
   Volume ID:     ST0194
   Owner ID:      apollo
   File ID:       force
   File written:  1992/03/06 19:13:21 UTC

Starting restore:
```

`ST0194` is the **install** cartridge; the boot cartridge is `SR10.4`. The drive
read the new medium, MINST ran `rbak_sr10` against it, and the restore began. No
binary on the console and no `Crash_Status`.

`ext/mame` now carries a fourth local edit, marked `APOLLO ORACLE EDIT` in
`sc499.cpp` and `sc499.h` and citing this finding. Unlike the reverted attempt,
this one is kept because it is *evidenced*: the standard says insertion behaves
as reset, the oracle already implements reset, and the machine agrees.
