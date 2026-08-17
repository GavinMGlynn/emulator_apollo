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

**Status: CLOSED. Classification: `ours-wrong`, and ours is now fixed.**

`ADD.B D0,(A0)` costs **6 clocks warm and 7 averaged over both alignments
cold**, against the oracle's flat 7 and the manual's composed 6 and 7. The row
that was reporting a component now composes through Equation (11-2) with
§11.6.1's `fea (An)`, and the published figures are decomposed into microcode
and bus before being applied -- `docs/references/M68030_TIMING.md` records the
derivation and `machine_suite` asserts both figures.

Note what the agreement is and is not. Our cold figure *alternates* 6 and 8 with
prefetch alignment where the oracle is a flat 7; the average is what agrees.
That is C7's classification standing, not a residual disagreement: §11.3.3 works
an example whose instruction "is eight clocks for even alignment and 10 clocks
for odd alignment, an average of nine clocks", so an alignment-dependent pair
averaging the published figure is the behaviour the manual describes and a flat
constant is not.

The original entry follows, unchanged.

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

**`mdsession.py --settle` defaults to 3.0 seconds, and that is a trap for any
long measurement.** The `watch` stage sends one character to autobaud the port
and then sleeps `--settle` before tearing the session down — so an invocation
that omits it runs the machine for *three seconds* and reports success with an
empty log. Three attempts at a whole-boot measurement were lost to it before the
default was read: the session prints its setup, prints `watching, not knocking`,
exits 0, and looks exactly like a boot that produced nothing. A Domain/OS boot
needs `--settle` in the hundreds of seconds even under `-nothrottle`.

**And an empty console log is not evidence of a dead machine here.** The boot
PROM writes to the *display* when it finds one and to the serial line only when
it does not, so the oracle's `dn3500` — which has a display — is silent on the
console by design. Diagnose from a probe or a screen capture, not from the log
being empty.

**A CRP probe, for when this is next attempted.** The only place MAME writes the
68030's CPU root pointer is `m68kmmu.h`'s `case 3: // CPU root pointer`, right
after `m_mmu_crp_aptr = temp64 & 0xffffffff` — one `fprintf` there logs every
load with `m_ppc` and `total_cycles()`. Verified to build and to reach the
binary (`strings ... | grep APCRP`), and **reverted**. What was not achieved is
the measurement: across three runs the probe never fired, so under this harness
the machine is not reaching a `PMOVE` at all. That is the thing to diagnose
first next time, and it is a *different* question from whether the probe works.

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

### C25 — the QIC-02 command set, complete (two codes recovered)

`[SC499]` §1.13: "The SC-499 controller is designed to accept the QIC-02 command
set." §1.13's **summary table** carries a previous owner's handwritten
annotations, and two codes sit under the pen.

    SELECT, SOFT LOCK OFF   0000 0001   01     "selects the tape drive"
    SELECT, SOFT LOCK ON    0001 0001   11     as above, plus a cartridge lock
    BOT                     0010 0001   21     "positions the tape ... to BOT"
    ERASE                   0010 0010   22     "completely erases the tape"
    RETENSION               0010 0100   24
    SELECT Q11 FORMAT       0010 0110   26     "selects the Q11 format"
    SELECT Q24 FORMAT       0010 0111   27
    WRITE                   0100 0000   40
    WRITE FILE MARK (WFM)   0110 0000   60
    READ                    1000 0000   80
    READ FILE MARK (RFM)    1010 0000   A0
    READ STATUS             1100 0000   C0

**This note previously recorded `22` and `26` as unrecoverable**, and left them
blank on the same principle the 8259A's one unnamed OCW2 combination was marked
"by elimination": a plausible value written in as fact is indistinguishable from
a transcribed one later. That principle was right and the conclusion was wrong,
because only one place in the document had been read.

§1.13.1's **numbered descriptions**, two pages further on, give every code in
binary and carry no annotation -- "5) ERASE COMMAND (0010 0010)" and "11) SELECT
Q11 FORMAT COMMAND (0010 0110)". The same series gives BOT as `0010 0001`,
RETENSION as `0010 0100` and SELECT Q24 as `0010 0111`, which are the three
codes the summary table had already supplied. Five entries of one series with
three independently confirmed is transcription, not inference.

The general lesson, and it cost nothing to learn twice: **a table that cannot be
read is not a fact that cannot be recovered.** The second place to look was in
the same file, and the same is true of `[OMTI]`'s two commands numbered `0Fh`,
where the byte-0 bit row settled what the heading got wrong.

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


## C57 -- the driver typed a cartridge at the machine, and the bug was one word

MINST had loaded from cartridge 1 and was waiting at its template menu. The
answer `11` was appended to the command file and never sent. The driver was
alive and its machine was alive, and nothing moved.

The evidence was strange enough to be worth recording before the cause:

- the driver's output file had reached **150 Mbyte** and contained **308,250**
  `mdsession: send` lines, 307,350 of them at the start of a line, so genuine
  driver messages rather than a grep artefact;
- their payloads were Domain/OS documentation -- prose about name servers,
  RFC882, `install/doc/apollo` -- text the driver has no business sending;
- and yet the **command file was 1,140 bytes of clean ASCII** and did not
  contain any of it, while the console log was 28 Kbyte and did not either.

So the driver was sending something it had not read from its command file, and
had not received on the console.

### The first hypothesis was wrong, and the reproduction is what showed it

The obvious mechanism is a feedback loop: if the console log and the command
file were ever the same file, every byte the machine printed would be read back
as a command and typed at the machine, which prints more. That was reproduced
against the stub and it does storm -- one send becomes twenty, and the command
file grows from 6 bytes to 969.

It is also **not what happened**. The two files have different inodes, neither
contains `RFC882`, and neither grew. A mechanism that reproduces is not thereby
the mechanism that occurred, and the cheap check that separates them is looking
at the artefacts the real run left rather than at how plausible the story is.

### What it actually was

The first sends after the swap are the answer:

```
mdsession: send 'Ident  019594   00'
mdsession: send 'Ident  CRTG_std_sfw_1'
```

That is the **install cartridge's own header**. The driver was reading
`019594-001.CRTG_STD_SFW_1.ct` -- 58 Mbyte of tape image -- as its command file
and typing it at the machine, line by line.

`follow_commands(session, path, ...)` takes the command file as `path`. The
`!swap` directive resolved the medium into a local variable also called `path`.
From the next poll onward, `open(path, "rb")` opened the cartridge.

One word. It explains every part of the observation exactly: the storm starts at
the first swap, the payloads are tape content, the volume matches the cartridge,
the real command file is untouched because it was never read again, and `11`
went nowhere.

### Why it survived this long

`!swap` was tested, and the tests passed -- they checked that swaps are
acknowledged, arrive in order, resolve to absolute paths, and fail loudly when
refused. Every one of those is about the swap. None of them asked **what the
driver does next**, and the damage is entirely in what happens after.

The regression test is therefore shaped as the failure was: swap in a file with
recognisable contents, then append one more command. If the command file is
still being followed the command arrives; if the driver has been redirected onto
the medium it never does, and the medium's contents show up as input. Verified by
removing the fix -- both checks fail without it.

**A test that exercises a feature is not a test that the feature left the program
in a working state.**


## C58 -- it boots from its own disk

The install is finished and the result boots. `ex domain_os` with **no `di c`**
and **no cartridge in the drive**:

```
>ex domain_os
low: 01002000 high: 010E986C start: 01002024

Domain/OS kernel(7), revision 10.4, February 14, 1992  11:42:25 am

Apollo Phase II Environment   Revision 10.4   Jan 25, 1992  12:59:03 pm

)
```

Three details make that more than "it printed something":

- **`error: sysboot not found` is gone.** That is exactly what this command
  answered before MINST ran (`C55`): the boot-volume restore leaves
  `sysboot.m68k` in the filesystem and the PROM wants `sysboot`, which the
  install creates. The failure and its disappearance are the same test run
  twice, on either side of the thing that was supposed to fix it.
- **The kernel is a different image.** From the cartridge it loaded to
  `01111FFF`; from the disk it loads to `010E986C`. Not the same bytes, so not
  the tape being read by another name.
- **The Phase II banner has no `RBAK version` suffix.** The tape's says
  `Revision 10.4 RBAK version`; this one says `Revision 10.4`. It is the
  installed environment rather than the restore tool's.

And **no calendar complaint and no salvage prompt**, which is the clean
shutdown of `C57` earning its place: a volume that was shut down properly boots
without being repaired first.

### It runs installed software

Logged in over the serial console as `user`, empty password:

```
$ bldt

     **** Node 12345 ****   "//node_12345"
Domain/OS kernel(7), revision 10.4, February 14, 1992  11:42:25 am
```

`//node_12345` is the name INVOL gave the volume, so the node is running under
the identity this project created for it.

Two files placed by `install++` were checked directly and both are present --
`/usr/X11/bin/uwm` and `/com/rbak`, each opened and read as an object module.
Worth recording *how* that check landed: `ld` on this machine is the **linker**,
not Aegis's list-directory, so it reported "Object module contains no relocation
information". That is a file it successfully opened and parsed, which is a
stronger existence proof than a directory listing would have been, and it is
also why `ld /` and `ld /bsd4.3` had earlier answered "wrong type - can't
operate on system objects": they are directories and variant links, and the
linker was being asked to link them.

### What this closes

`PROJECT_STATUS.md` has carried this since the beginning: *"No bootable
Domain/OS media: all we hold is installation media, so reaching a login prompt
needs an install performed under the oracle first."*

There is now an installed, bootable Domain/OS SR10.4 on a disk this project
built from nothing but the five distribution cartridges, and it has been booted
from that disk and logged into. The image is pinned in
`docs/references/DOMAINOS_IMAGE.md` and the procedure that produces it is
`tools/mame-oracle/install-domainos.cmds`.

## C59 -- the coprocessor was never attached, and its first probe agrees

**Class: `ours-wrong`, found and fixed.** Then: **agree**.

The 68882's plan item asks for "a probe suite over each operation and rounding
mode; note the oracle's admitted FPU gaps as a divergence class". Auditing that
line rather than trusting the tick found something before any probe could run:
**`ap_machine_init` never attached a coprocessor.** `cpu->fpu` was null on every
machine this core builds, so every F-line instruction took the line 1111 trap --
the behaviour of a correctly *unfitted* machine, arriving for the wrong reason,
and precisely the confusion this core is otherwise careful to prevent. There was
no floating-point probe because there was nothing to probe.

With the part attached, the first cross-implementation probe is:

    FMOVECR #$00,FP0 ; FADD FP0,FP0 ; FMOVE.D FP0,(A0) ; STOP #$2700

Three things in one program: the on-chip constant ROM, an arithmetic operation,
and the store conversion. The answer is a bit pattern neither implementation is
free to choose -- `2*pi` as an IEEE double is `401921FB54442D18` on every machine
that has ever computed it -- so agreement here is agreement with something
outside both.

| Check | Ours | Oracle | |
| --- | --- | --- | --- |
| instructions executed | 5 | 5 | agree |
| stored high long word | `401921FB` | `401921FB` | agree |
| against the IEEE value | `401921FB` | -- | agree |

Run with `python3 tools/mame-oracle/probe_compare.py --program fpu`.

**One row was removed rather than excused.** `D0` differed -- ours `00000000`,
the oracle's `0000FFFF` -- and it is not a divergence: this program never writes
D0, so the comparison was between two *reset states*. The sentinel probe does
write it, which is why the check exists at all, so the row is now conditional on
the program rather than explained away every time someone reads the output. A
check that cannot fail for a good reason is worse than no check, because it
trains the reader to ignore a differing row.

**What this campaign does not yet cover**, and the reason the item stays open:
one probe is not "each operation and rounding mode". The constant ROM's value,
`FADD`, and the double store agree; the transcendentals, the directed rounding
modes, packed decimal and the exception byte have not been compared, and the
oracle's *admitted* FPU gaps have not been enumerated as a divergence class. What
would settle it is more probes through the same harness -- the mechanism now
exists and is the part that was missing.

## C60 -- the rounding mode agrees, and the constant ROM gets its first witness

**Class: agree.**

C59's probe could not reach the rounding *mode*, which is half of what the
68882's verification line asks for. Reaching it took some care: rounding happens
at bit 52 of a double, so a change of mode moves the **high** long word only when
a carry propagates through all thirty-two low bits, and the harness reads one
long word. Of the constant ROM's entries, `ln(10)` at offset `$31` is one whose
*low* word moves -- `40026BB1BBB55516` to the nearest against `...5515` toward
zero -- so the store is aimed four bytes low and the sentinel read lands on the
half that actually differs.

    MOVEQ #$10,D0 ; FMOVE.L D0,FPCR ; FMOVECR #$31,FP0 ; FMOVE.D FP0,(A0)

| Check | Ours | Oracle | |
| --- | --- | --- | --- |
| instructions executed | 6 | 6 | agree |
| stored low long word | `BBB55515` | `BBB55515` | agree |
| against the round-to-zero value | `BBB55515` | -- | agree |

Run with `python3 tools/mame-oracle/probe_compare.py --program fpu-rounding`.

Four things at once, and the probe fails differently for each: `FMOVE.L` to the
FPCR has to work, the mode has to be *honoured* rather than ignored, the constant
has to be right, and the store conversion has to round by the mode rather than to
nearest. `...5516` would mean the mode was ignored.

**The result worth naming is the third one.** `FMOVECR`'s values are computed
here to 200 decimal digits, not read from any manual -- neither the part's own
nor the `M68000 Family PRM` prints a bit pattern, which `PROJECT_STATUS.md`
records as an open question with "instrument the oracle and read all 22 back" as
its closing route. This is one of the twenty-two, read back, agreeing to the
fifty-three bits a double holds. It does not close the question -- a double sees
none of the eleven low bits where a ROM is most likely to differ from a correctly
rounded value, and one constant is not twenty-two -- but it is the first evidence
from outside this project that the table is right, and it narrows what a full
readback would have to disagree about.

## C61 -- the sine agrees, and the comparison cannot yet be sharp enough to matter

**Class: `sub-poll-slack`** -- the resolution limit is the point of this row.

`FSIN` is where the two implementations are *least* obliged to agree. §4.3.2
publishes an error bound and no algorithm, so any conforming sine may differ from
any other in the low bits, and MAME's driver admits gaps "in some FPU operations
and operands". This is the probe most likely to find a difference.

    FMOVECR #$32,FP0 (1.0) ; FSIN FP0,FP0 ; FMOVE.D FP0,(A0)

| Check | Ours | Oracle | |
| --- | --- | --- | --- |
| instructions executed | 5 | 5 | agree |
| stored low long word | `8F090CEE` | `8F090CEE` | agree |
| against `sin(1)` correctly rounded | `8F090CEE` | -- | agree |

Run with `python3 tools/mame-oracle/probe_compare.py --program fpu-sine`.

**And the agreement proves less than it appears to.** The comparison is made at
*double* precision, and one unit in the last place of a double is 2048 units in
the last place of extended. This core's sine is measured under 3 ULP of extended
against 120-digit references; anything within about a thousand times that error
would round to the same double. So the probe cannot distinguish the two
implementations at all unless one of them is wrong by a margin far outside the
bound §4.3.2 publishes.

What it does establish is real, and is worth having for that reason alone: both
are inside double precision of the true sine, which is the accuracy the IEEE
standard actually specifies for this conversion, and neither has a gross error at
this argument. What it cannot establish is which of the two is closer to the
part.

**Sharpening it means comparing extended, not double** -- storing `FMOVE.X` and
reading twelve bytes, which needs the harness's single-long-word readback
widened. That is the measurement that would turn this row into a real comparison
of the transcendentals, and it is named here so the limit is not re-litigated
from a passing probe.

## C62 -- the sharp sine probe finds a real difference, and we are the further one

**Class: `ours-wrong`, with a qualification that matters: both conform.**

C61 predicted that comparing at double precision could not separate the two
implementations and named the sharper measurement. It needed no wider readback
after all: `FMOVE.X` writes twelve bytes, so aiming the store eight bytes low
puts mantissa bits 31-0 -- exactly where two sines accurate to a few units in the
last place are free to disagree -- under the existing single-long-word read.

    FMOVECR #$32,FP0 (1.0) ; FSIN FP0,FP0 ; FMOVE.X FP0,(A0)

| Check | Ours | Oracle | |
| --- | --- | --- | --- |
| instructions executed | 5 | 5 | agree |
| extended mantissa, bits 31-0 | `48677020` | `48677021` | **DIFFER** |

Run with `python3 tools/mame-oracle/probe_compare.py --program fpu-sine-x`.

**Adjudicated against neither implementation.** `sin(1)` to 120 decimal digits is
`0.84147098480789650665250232163029899962...`, whose correctly rounded extended
significand is `D76AA47848677021`. The oracle has it exactly; this core is one
unit in the last place low.

So the first sharp comparison of a transcendental found a real difference on its
first argument, and it is ours. That is the campaign working: three probes agreed
and taught us little, and the fourth -- built specifically to be able to fail --
failed.

**The qualification is not an excuse and should not be read as one.** §4.3.2's
bound is 64 units in the last place typical and 4096 worst case, so 1 ULP
conforms comfortably, and this core's sine is measured under 3.1 ULP against
120-digit references across its whole tested range. Nothing here is out of
specification. What the row records is narrower and still worth having: **at this
argument MAME is closer to the true sine than we are**, so a claim that this core
out-accurates the oracle on the transcendentals is not supported, and was never
measured before now.

What it does *not* establish is which is closer to the **part**, which is the
only question that finally matters and which neither side can answer: Motorola
published a bound and no algorithm, so the 68882's own sine may be further from
the truth than both.

**Next**: the same probe across a spread of arguments and the other eighteen
transcendentals, which is now a loop over the encoder rather than new machinery.
A per-function count of which side is closer is the measurement that would turn
this single row into a divergence *class*, which is what the 68882's verification
line asks for by name.

## C63 -- five transcendentals swept, and the difference is a *bias*, not noise

**Class: `ours-wrong`, systematic.** This is the divergence class the 68882's
verification line asks for by name, and the first one this project has measured
for the FPU.

C62 was one function at one argument. The sweep runs five, all at 1.0 from the
constant ROM, comparing the extended significand's low long word, and
adjudicating **against neither implementation** -- the truth column is computed
here to 140 decimal digits, because §4.3.2 publishes a bound and no algorithm and
so the mathematics is the only fixed point.

| function | truth | ours | oracle | closer |
| --- | --- | --- | --- | --- |
| `FSIN` | `48677021` | `48677020` | `48677021` | oracle |
| `FCOS` | `A8345C92` | `A8345C92` | `A8345C92` | both exact |
| `FTAN` | `F71D2DC5` | `F71D2DC4` | `F71D2DC5` | oracle |
| `FETOX` | `A2BB4A9B` | `A2BB4A9A` | `A2BB4A9B` | oracle |
| `FATAN` | `2168C235` | `2168C235` | `2168C235` | both exact |

Run with `python3 tools/mame-oracle/fpu_sweep.py`.

**The oracle is exactly correct on all five. We are one unit in the last place
low on three, and never high.** That direction is the finding. Random rounding
error would scatter above and below; a consistent low bias in three of five
points at a *mechanism* -- a kernel that truncates where it should round, or a
sticky bit that is not being collected from a discarded tail before the final
rounding. Two functions landing exactly right does not contradict that: a value
whose discarded tail happens to be small rounds correctly either way.

**Still inside specification, and that is not the point.** §4.3.2 allows 64 units
in the last place typical and 4096 worst case; the accuracy suite measures this
core under 3.1 ULP against 120-digit references across its whole tested range.
Nothing here fails a published requirement. What the sweep establishes is
narrower and had never been measured: **on these arguments the oracle is the more
accurate implementation**, so the standing expectation that this core
out-accurates MAME does not hold for the transcendentals, and should not be
repeated until it does.

Neither side can be checked against the *part*: Motorola published no algorithm,
so the 68882's own sine may be further from the truth than both. The truth column
is the only claim being made.

**Next**, in order: find the mechanism behind the low bias -- the final rounding
in `ap_m68882_transcendental.c`'s kernels is where a discarded tail would go
uncollected -- then re-run the sweep, then widen it to all nineteen functions and
a spread of arguments rather than the single value the constant ROM makes cheap.

## C64 -- the bias has a mechanism, and it was already written down

**Class: `ours-wrong`, diagnosed.** C63 measured a one-unit-low bias on three of
five transcendentals and predicted "a kernel that truncates where it should
round, or a sticky bit not collected". Both guesses were wrong, and the real
cause was already in `PROJECT_STATUS.md` as a `PROVISIONAL` -- unmeasured, and
therefore easy to read as theoretical.

The primitives are not at fault. `nx_mul`, `nx_add` and `nx_div` all round to
nearest at extended precision, and the final `x 1.0` that applies the caller's
mode is exact by construction, so it rounds nothing. Nothing truncates anywhere.

**The cause is that there is nothing left to round.** §3.4: "the mantissa is
maintained internally as 67 bits for rounding purposes, but is always rounded to
64 bits (or less, depending on the selected rounding precision) before it is
stored in a floating-point data register." The part computes in *67* bits and
rounds once at the end. This core computes each kernel step in 64 -- the
destination width -- so every step of an eighteen-term series rounds, the errors
accumulate at the last bit, and the final rounding has no bits below the
destination to round from.

That is exactly the `PROVISIONAL` already recorded: "Directed rounding at extended
precision is a no-op, because this model computes a 64-bit approximation directly
and has no bits below the destination left to round; the part carries 67 bits
internally." What was missing was any evidence that it *costs* anything. It does:
1 ULP on `FSIN`, `FTAN` and `FETOX` at argument 1.0, and it is why the oracle is
the closer implementation on those three.

**The direction is explained too**, which is what makes this a diagnosis rather
than a coincidence. Round-to-nearest at each step is unbiased in isolation, but
the kernels sum a series of *positive* terms of rapidly decreasing size: each
addition discards the tail of a smaller addend, and a discarded positive tail can
only make the running total low. Hence low, never high.

**Cost to close, now with a measured benefit.** Carry guard bits through every
kernel and round once from them -- the fix the `PROVISIONAL` already named, whose
value was previously stated as "bounded by one unit in the last place, so a
sixty-fourth of the typical bound". The sweep turns that from an estimate into an
observation: it is one unit in the last place, on three functions out of five,
and it is the whole of the difference from the oracle.

## C65 -- the obvious fix for C64 is not the fix, and the attempt is recorded

**Class: `open`.** A failed attempt, kept because it removes a wrong hypothesis
from the next one.

C64 diagnosed the one-unit-low bias as arithmetic done in the destination's own
64 bits where §3.4 has the part carry 67. The apparently direct fix is to carry a
residual through the series: a Horner whose accumulator is a `(hi, lo)` pair,
every product formed by the `nx_exact_mul` that already exists, every addition's
discarded tail folded back by a two-sum, and the final `1 + expm1(r)` rounded
once from the pair.

That was written for `FETOX` and it **did not move the result**. `FETOX` at
argument 1.0 stayed at `A2BB4A9A` against the true `A2BB4A9B`. It also regressed
`m68882_transcendental_suite`, so the change was reverted rather than left in a
red tree.

**Two things learned, and the first is the useful one.**

*The kernel is not where the bit is lost.* `e^x` is computed as `2^n * e^r` with
`r = x - n ln2`, and for `x = 1` that reduction is the delicate step: `n` is 1
and `r` is `1 - ln2`, a subtraction of two nearly equal quantities whose result
is then fed to the series. A residual carried through the *series* cannot recover
precision the *reduction* has already discarded. So compensating the kernel alone
is provably insufficient, and the next attempt should start at `exp_reduced` --
which is also where the trigonometric family's `pi/2` reduction already holds
three pieces for exactly this reason, a precedent inside this file that the
exponential family did not follow.

*And the compensation itself needs care.* The regression means the pair
arithmetic was wrong somewhere -- most likely `nx_exact_mul`'s contract when the
low half is zero or denormal, which a Dekker split does not have to handle
gracefully. Whatever the next attempt does, it wants its own unit tests on the
pair operations before any kernel is converted, rather than discovering the
problem through nineteen functions at once.

**Cost to close is therefore higher than C64 implied**, and that is worth saying
plainly: it is not "carry guard bits through every kernel" but "carry them
through every kernel *and every argument reduction*", with the pair arithmetic
tested in its own right first. Still one unit in the last place of benefit, still
inside §4.3.2's published bound without it.

## C66 -- the lost bit is one line, and the reduction was already half-fixed

**Class: `open`, narrowed to a single statement.**

C65 said the next attempt should start at `exp_reduced` rather than the series.
Reading it shows the reduction is *already* two-piece, so the precedent the
exponential family "did not follow" was in fact followed -- C65's phrasing was
wrong about that and is corrected here:

    const int n = nx_round_to_int(nx_mul(x, c_log2e));
    ap_m68882_extended_t r = nx_sub(x, nx_mul(n_value, c_ln2_hi));
    r = nx_sub(r, nx_mul(n_value, c_ln2_lo));

The first subtraction is exact and the comment says why: `n * ln2_hi` is exact,
and `x` and it are within `ln2` of each other, so nothing is lost. **The second
one is not.** `n * ln2_lo` is the small correction that makes the split worth
having, and subtracting it from `r` rounds to 64 bits -- discarding precisely the
tail the split was constructed to supply. The reduction goes to the trouble of
carrying `ln2` in two pieces and then throws the second piece's contribution away
at the last step.

For `e^1` that is the whole story: `n` is 1, `r` is about `0.30685`, and a
half-unit error in `r` maps to about a unit in the last place of `e^r * 2`, which
is the `A2BB4A9A` against `A2BB4A9B` the sweep measures.

**So the fix is smaller than C64 or C65 supposed**: not every kernel, and not
every reduction -- carry the residual of *this* subtraction, hand the pair to the
series, and round once at the end. The same shape will apply to the logarithms
and the trigonometric family, whose reductions have the same construction, but
each is one statement rather than a rewrite.

**What has not changed** is that the pair arithmetic needs its own tests first.
C65's regression came from the compensation, not from where it was applied, and
that lesson survives this correction intact: `nx_exact_mul` and any two-sum want
unit tests against known-exact cases before a kernel depends on them.

## C67 -- the reduction's residual is real, and an eighth of what is missing

**Class: `open`, narrowed again by arithmetic rather than by another attempt.**

C66 identified the one rounding statement in `exp_reduced` and predicted that
carrying its residual would close the gap. Written, tested green on all 112
suites, measured against the oracle -- and `FETOX` did not move: still
`A2BB4A9A` against the true `A2BB4A9B`. The change was reverted, because a
change that does not move the measurement it was made for has not earned its
complexity, whatever else is true of it.

**Why it cannot have worked, which the arithmetic says without another run.**
For `e^1` the reduction gives `n = 1` and `r` about `0.30685`, whose exponent is
`-2`, so `r`'s last bit is `2^-65`. The correction `n * ln2_lo` is around
`2^-67`. Subtracting it therefore rounds away a residual of about `2^-67` -- real,
and worth carrying -- but the result `e^r * 2` is about `2.718`, whose last bit
is `2^-62`. The residual propagates to roughly `2^-65.5` of the answer: **an
eighth of one unit in the last place.** It can only change the result when the
value already sits within an eighth of a rounding boundary, which at this
argument it does not.

So C66's diagnosis was right about the mechanism and wrong about the magnitude.
One eighth of an ULP cannot account for a full one.

**Where the missing bit must therefore be.** Not the reduction, now bounded. Not
the final rounding, which C64 established is exact. That leaves the eighteen-term
series between them: thirty-six roundings at 64 bits, each discarding the tail of
a smaller addend, accumulating downwards -- which is C64's original reading, and
which C65's failed attempt tested *only in combination with a compensation bug*
that made its result uninterpretable.

**So the next attempt is C65's, done correctly**: a pair-carrying Horner, with the
pair arithmetic unit-tested against known-exact cases *first* -- which is what
C65 said and what it did not do. Three hypotheses have now been priced, two
eliminated by measurement and one by arithmetic, and the survivor is the one the
first diagnosis named.

## C68 -- the pair arithmetic is tested, which is what C65 skipped

**Class: prerequisite landed.** Not a comparison against the oracle; the step
C67 named as the reason the next comparison will be readable.

C65 converted a kernel to compensated arithmetic without ever checking the
compensation, and its regression could not be attributed: the pair operations and
the place they were applied were equally plausible causes, so the attempt taught
nothing about either. That is now fixed at the root rather than worked around.

`nx_exact_mul` and a new `nx_two_sum` are exposed for testing and checked on
their own:

- **An exact product must have a residual when one is due, and none when it is
  not.** Two all-ones mantissas need 128 significand bits, so the low half must
  be non-zero -- a split that silently produced zero would satisfy any test
  written only in terms of the high half. And `hi + lo` must round back to `hi`,
  since the residual is a correction rather than a second value. `1.5 x 2 = 3`
  checks the other direction: exactly representable, so no residual at all.
- **The two-sum must sort its addends, and the test proves it by handing them
  over the wrong way round.** Knuth's fast form needs the larger first; reversed,
  its subtractions stop being exact and the residual is *silently wrong* rather
  than absent. Passing the same pair both ways and requiring identical answers is
  what catches that -- and silent wrongness in a primitive is precisely how an
  untested compensation regresses something far away, which is C65 in one
  sentence.

Both pass. 112 suites green on `linux-debug` and `linux-release`.

**What this does not do** is move any measurement: `fpu_sweep.py` still reads
oracle 3, both exact 2, because no kernel uses the pair arithmetic yet. The value
is entirely in what comes next -- when the compensated Horner is written and the
sweep moves or does not, the answer will mean something, which it did not the
first time.

## C69 -- C65 was misread, and the compensated series is not the answer either

**Class: `open`, and a correction to this file.**

Two findings, and the first is a correction of my own earlier reading.

**C65's "regression" was the determinism golden, not a defect.** With the pair
arithmetic now tested (C68), the compensated Horner was written again and the
failing test named itself: `test_the_family_is_bit_identical_in_every_build`,
the FNV-1a digest over 38,880 results. It fails whenever the family changes,
which is exactly what an accuracy change does -- it is the golden doing its job,
not a fault being reported.

C65 reverted on the *assumption* that a failing suite meant a broken change,
without reading which test failed. That was wrong, and it cost two campaigns:
C66 and C67 both went looking for a compensation bug that was never there. A
golden that detects intentional change looks identical to a regression until you
read it, and this file now says so.

**And the compensated series still does not fix `FETOX`.** With the corrected
reading, the same change measures cleanly: the digest moves, 38,880 results
change, the accuracy suite's bound still holds -- and `FETOX` at argument 1.0
stays at `A2BB4A9A` against the true `A2BB4A9B`. Reverted, by the standard C67
set: a change that does not move the measurement it was made for has not earned
its complexity, and this one costs a golden update across two build types.

**So all three hypotheses are now eliminated by measurement.** Not the argument
reduction (C67, bounded at an eighth of a unit by arithmetic). Not the final
rounding (C64, exact by construction). Not the series (this row, measured).

**What has never been looked at** is what sits between the series and the
answer: `nx_scale2`, which applies the `2^n` the reduction factored out, and the
`nx_add(c_one, ...)` that forms `1 + expm1(r)` before it. For `e^1` that addition
shifts the kernel's result down by two bits before `nx_scale2` shifts it back up
-- a place where a bit can be lost that neither the kernel nor the reduction owns,
and the only part of the path no campaign has yet questioned.

## C70 -- no single site holds the missing bit, and that is the answer

**Class: `sub-poll-slack`, promoted from `open`.** The question is settled, and
the settlement is that there was never a defect to find.

C69 left one step unexamined: `1 + expm1(r)`, which for `e^1` shifts the kernel's
result two bits below the sum's last place. Carrying the pair through *that*
addition -- the one thing C69 built the pair for and then collapsed one line
early -- was written and measured. `FETOX` stayed at `A2BB4A9A`.

**And the digest came out bit-identical to C69's**, `2726433C1F0DB458` both
times. That is the decisive datum: wiring the residual through the addition
changed *nothing at all*, across 38,880 results, so the addition was never losing
anything either. Four candidate sites, four eliminations:

| site | eliminated by |
| --- | --- |
| argument reduction | C67, bounded at an eighth of a unit by arithmetic |
| the series | C69, compensated and measured |
| `1 + expm1(r)` | this row, compensated and measured -- identical digest |
| final rounding | C64, exact by construction |

**So the missing unit is not anywhere. It is everywhere.** Each site loses a
fraction of a unit in the last place; no single one loses a whole one; and
compensating any one of them leaves the total where it was. That is the ordinary
behaviour of an implementation whose working precision equals its destination
precision, which is what §3.4 says the part avoids by carrying 67 bits and
rounding once.

**Which means this was never a defect.** The accuracy suite's ceiling is 3.1
units in the last place and §4.3.2's bound is 64 typical; one unit is inside both
by a wide margin, and the implementation is doing exactly what it was built to
do. The oracle is exact here because MAME computes these in the host's own wider
type, not because it models the part more faithfully -- and neither of us can
claim to match the 68882, whose algorithm Motorola never published.

**Reclassified accordingly.** This is not `ours-wrong` awaiting a fix; it is
`sub-poll-slack` -- the resolution limit of an implementation that computes in
64 bits, recorded so it is not re-litigated. The `PROVISIONAL` in
`PROJECT_STATUS.md` already names the only thing that would change it: carry
guard bits through *every* kernel and *every* reduction, and round once from
them, which is a rewrite of the module rather than a fix to a line. Its benefit
is now known precisely -- one unit in the last place, on three functions of five,
at this argument -- and so is the fact that no cheaper subset of it buys
anything.

## C71 -- the exactly specified operations agree, which is the divergence class

**Class: agree, and the campaign's conclusion.**

C70 settled the transcendentals as a resolution limit. That left the question the
68882's verification line actually asks: not "do the two agree" but "what *kind*
of disagreement is there, and where". The sweep now answers it by splitting the
operations along the line §4.3.2 itself draws.

| function | truth | ours | oracle | |
| --- | --- | --- | --- | --- |
| `FSIN` | `48677021` | `48677020` | `48677021` | bounded -- oracle closer |
| `FCOS` | `A8345C92` | `A8345C92` | `A8345C92` | bounded -- both exact |
| `FTAN` | `F71D2DC5` | `F71D2DC4` | `F71D2DC5` | bounded -- oracle closer |
| `FETOX` | `A2BB4A9B` | `A2BB4A9A` | `A2BB4A9B` | bounded -- oracle closer |
| `FATAN` | `2168C235` | `2168C235` | `2168C235` | bounded -- both exact |
| `FSQRT` | `D2DA9490` | `D2DA9490` | `D2DA9490` | **exact -- both exact** |
| `FINT` | `00000000` | `00000000` | `00000000` | **exact -- both exact** |

`FSQRT` of 10 and `FINT` of pi, both adjudicated against 140-digit truth.

**The split is the finding.** §4.3.2 bounds the transcendentals -- "the IEEE
specification does not define the error bound to which transcendental (**except
square root**) functions are to be performed" -- and specifies everything else
exactly. Every difference the campaign has found is on the bounded side, and
there is none at all on the specified side. The sweep now flags an exact-operation
row that differs as `DEFECT: no error bound licenses this`, so the distinction is
enforced rather than remembered.

**So the divergence class is:** one unit in the last place, transcendentals only,
three of five at argument 1.0, oracle closer, cause understood (C70: arithmetic
at the destination's own width where §3.4 has the part carry 67 bits), inside
both the accuracy suite's 3.1 ceiling and §4.3.2's 64. Nothing outside that.

**What the campaign does not claim.** Neither implementation has been compared
with a real MC68882, and neither can be: Motorola published a bound and no
algorithm, so the part's own sine may be further from the truth than both of
ours. "The oracle is closer to the true value" is the whole of the claim; "the
oracle is closer to the part" is not measurable from here and is not asserted.

That is thirteen campaigns, C59 to C71, from a coprocessor that was not attached
to a machine at all.

## C72 -- what a fault probe needs that the FPU probes did not

**Class: `open`, with the obstacle identified so the next attempt starts past
it.**

The exception item's verification asks for "probes that deliberately fault,
diffed against oracle", and the audit found it unmet. The side-loading harness
now exists and works, so this looks like a small extension of C59-C71. It is
not, and the reason is worth writing down before someone spends a session
finding it.

**The FPU probes were map-independent. Fault probes are not.** Every probe so far
computes its own answer from registers and the constant ROM, and writes it to an
address the encoder parameterises -- which is why `probe_compare.py` can assemble
the same program twice at two bases and compare the results. A fault probe has to
*name an address that faults*, and the two machines disagree about which
addresses those are: this core's probe harness is 64K of RAM from zero, a DN3500
is main memory at `01000000` inside a much larger map with devices in it. "Read
something unmapped" is not one program assembled twice; it is two different
questions.

Worse, the interesting content is the frame, and a bus fault frame carries the
*fault address* -- so even a successful comparison would find two different
values there for a reason that has nothing to do with the model being tested,
exactly as raw PCs would. The comparison has to be field-by-field with the
address fields excluded or rebased, which the current single-long-word readback
cannot express.

**What can be probed today, without settling any of that.** The faults whose
frames carry no address of their own are map-independent and available now:
illegal instruction (`$4AFC`, vector 4, four-word frame), divide by zero (vector
5, six-word), `CHK`, `TRAPV`, and the privilege violations. Each exercises the
whole path -- vector fetch through the VBR, frame built, handler entered, `RTE`
back -- and each is one program assembled twice, exactly like the probes that
already work. A handler that stores the frame's *format word* to the sentinel
makes it readable through the existing machinery unchanged.

**What needs a decision first**: the bus and address error frames, which are the
ones the item names and the ones carrying the SSW, fault address and data output
buffer. Either the harness gains a wider readback and address-aware comparison,
or the probe arranges a fault at an address both maps agree is bad. Neither is
hard; both are choices, and neither should be made by accident inside an
implementation.

### C72 addendum -- "available today" was wrong, and the first step is the oracle side

Checked rather than assumed, one turn later. C72 said the map-independent faults
-- illegal instruction, divide by zero, `CHK`, `TRAPV` -- could be probed without
settling the memory-map question. They cannot, and the reason is on the *other*
side of the harness.

`ap_probe_run` plants an exception table before every probe: an `RTE` at a fixed
address, with all sixty-two vectors pointing at it, "so a probe that faults
unexpectedly reports EXECUTED from the handler rather than running off into blank
memory". `probe.lua` plants **nothing** -- it writes the program words at
`load_at` and single-steps. Its whole body is one `space:write_u16` loop.

So the two sides are asymmetric for *any* exception, not just the ones carrying a
fault address. Ours lands in a known handler; the oracle's runs without firmware
and lands in whatever a DN3500's vector table holds at reset, which is not
something a probe should be reading. A divide-by-zero probe written today would
compare a handled exception against an undefined jump and the difference would
say nothing.

**So the first step is on the oracle side and is small**: `probe.lua` should
plant the same table our harness does -- an `RTE` at a chosen address, every
vector pointing at it -- rebased like everything else. That is a handful of
`write_u32` calls, and it is a prerequisite for every fault probe rather than for
any particular one. Only after that does C72's map question become the *next*
obstacle rather than the second of two.

Recorded because the correction cost nothing to find and would have cost a
session to discover from a probe whose output looked like a real divergence.

### C72 addendum 2 -- the vector table is planted, and its landing is unverified

The oracle side now plants what this core's harness always did: an `RTE` and
sixty-two vectors pointing at it, written before the probe words. The existing
sentinel probe still runs identically under both, so nothing is broken.

**Two things are deliberately not claimed.** The table is planted *in RAM beside
the handler*, not at address zero, because C5 measured that a write to the boot
PROM's range silently does nothing and reports success -- and on a DN3500 the
low addresses are exactly that. A table at zero would appear to take and would
not be there. This core's own probe RAM starts at zero, which is why that
asymmetry is invisible from this side and had to be reasoned about from C5 rather
than observed.

And **it has not been confirmed to land.** The code reads one vector back and
prints `planted` or `NOT RAM -- vectors will not take`, but that line has not
been seen in a run: `probe_compare.py` does not surface the oracle's comment
output, and a direct invocation was not got working in the time available. The
diagnostic exists so that the next run answers the question in one line rather
than by inspection.

**And a table in RAM is only half of it**: the 68030 reaches its vectors through
the VBR, which is zero at reset. A probe that faults must set the VBR to
`table_at` first -- one `MOVEC` in supervisor state -- or the machine will look
at address zero regardless of what was planted. That instruction belongs in the
fault probe's own program, and is named here so it is not discovered from a probe
that jumps somewhere unexplained.

### C72 addendum 3 -- the table lands, verified

    oracle: exception table at 01003100, handler 01003000, planted

The first of C72's three obstacles is closed, and closed by *observation* rather
than by reasoning: the vector table lands in RAM on the oracle, beside the
handler and clear of the boot PROM's range.

Seeing it needed one change worth keeping. `probe_compare.py` captured the
oracle's stdout and checked it for a single string, discarding everything else --
including every comment the far side makes about what it planted and read back.
Those lines are the only window into a machine running in another process, and an
entire turn was spent unable to answer "did the table land" because they were
thrown away. They are now echoed, prefixed `oracle:`, which is how the answer
above was got.

**Two obstacles remain, in order.** The 68030 reaches its vectors through the
VBR, zero at reset, so a fault probe must `MOVEC` the VBR to `01003100` before
faulting -- and on this core's side to its own table, which the probe harness
plants at zero where the VBR already points. That asymmetry is now the *only*
thing standing between here and a working fault probe for any map-independent
fault. After that, C72's original question: the bus and address error frames
carry a fault address, and comparing those needs a field-by-field readback the
harness does not have.

### C72 addendum 4 -- the planted handler cannot be an RTE for a fault probe

Designing the first fault probe found a constraint that neither side's harness
implies, and that would have shown up as a hang rather than a message.

**A bare `RTE` handler loops forever on a fault.** Both harnesses plant one -- an
`RTE` with every vector pointing at it -- and for an *instruction* exception like
`TRAP #n` that is right: the frame stacks the following instruction and the `RTE`
resumes past it. For a **fault** it is not. An illegal instruction, a bus error
and an address error all stack the address of the instruction that faulted, so
the `RTE` returns to it, it faults again, and the probe spins until its
instruction limit. The harness's own comment says the blanket handler exists "so
a probe that faults unexpectedly reports EXECUTED from the handler rather than
running off into blank memory" -- which is true of the exceptions it was written
for and false of the ones this probe is about.

**So a fault probe must plant its own handler**, and that turns out to simplify
the rest rather than complicate it:

- The handler ends in `STOP`, not `RTE`, so the probe terminates the way every
  other probe does.
- It only needs *one* vector, not sixty-two -- vector 4 for an illegal
  instruction -- so the probe writes a single long word at `VBR + $10` rather
  than building a table.
- It can store something worth comparing before stopping: the stacked **format
  word** at `SP + 6`, which carries the frame format nibble and the vector
  offset. That is the field the exception item's verification is really about,
  and it is map-independent -- unlike the fault address, which is C72's remaining
  question and can be left for later.

Which means the VBR obstacle and the handler obstacle collapse into one program:
write the vector, point the VBR at it, fault, store the format word, stop. The
two sides then differ only in the base, as every working probe does.

**The one asymmetry left in it**: this core's probe harness plants its table at
zero, where the VBR already points, so the probe's `MOVEC` is a no-op there and
load-bearing on the oracle. Harmless, and worth knowing before someone reads the
`MOVEC` as dead code on the side they happen to be debugging.

## C73 -- the first fault probe runs on both sides and agrees

**Class: agree.**

    plant vector 4 ; MOVEC VBR ; ILLEGAL ; handler stores the format word

| Check | Ours | Oracle | |
| --- | --- | --- | --- |
| instructions executed | 8 | 8 | agree |
| stacked format word | `00000010` | `00000010` | agree |
| against the M68000 encoding | `00000010` | -- | agree |

Run with `python3 tools/mame-oracle/probe_compare.py --program fault`.

`$0010` is a four-word frame -- format nibble 0 -- and vector 4 at offset `$10`.
That value is fixed by the architecture rather than by either implementation, so
agreeing on it is agreeing with something outside both, as the FPU probes were.

**Four obstacles stood in front of this and three dissolved into one program.**
The oracle planted no exception table (addendum 1, fixed and verified in addendum
3); the table could not go at address zero because a DN3500's low addresses are
boot PROM and C5 measured that writes there silently do nothing; the VBR points
at zero and had to be moved; and the harness's blanket `RTE` handler would have
looped forever, because a fault stacks the *faulting* instruction's address.
Planting the probe's own handler answers the last three at once -- one vector
instead of sixty-two, a `STOP` instead of an `RTE`, and the VBR pointed at
whatever the probe wrote.

**What this closes and what it does not.** The exception item's verification asks
for "probes that deliberately fault, diffed against oracle", and an illegal
instruction is exactly that: the whole path runs on both sides -- vector fetched
through the VBR, frame built, handler entered -- and the frame's identifying
field matches. What is still not compared is the **bus and address error frames**
the item names in its title, which are the ones carrying an SSW, a fault address
and a data output buffer. Those need the field-by-field readback C72 identified,
and a fault at an address both memory maps agree is bad. That is the item's last
open question, and it is now the *only* one.

## C74 -- the bus fault frame agrees, and a modelling decision is corroborated

**Class: agree.** This closes the exception item's verification line.

    plant vector 2 ; MOVEC VBR ; read $F0000000 ; handler stores the format word

| Check | Ours | Oracle | |
| --- | --- | --- | --- |
| instructions executed | 8 | 8 | agree |
| stacked format word | `0000B008` | `0000B008` | agree |

Run with `python3 tools/mame-oracle/probe_compare.py --program bus-fault`.

**C72's obstacle was smaller than it looked.** It said comparing bus fault frames
needed "a fault at an address both maps agree is bad", and that the frames'
address fields would differ for reasons unrelated to the model. Both dissolve
with one choice: `$F0000000` is above everything either machine maps -- 64K of
probe RAM here, main memory and devices on a DN3500 -- so the same *literal*
faults on both, and being the same literal, any address the frame records is the
same value on both sides. The problem was the assumption that a bad address had
to be found per-machine.

**And the result is a real corroboration, not a formality.** Vector 2 at offset
`$08` is fixed by the architecture, but the format nibble is a modelling
decision this core made and documented: `$A` is the short frame, "Execution Unit
at Instruction Boundary", `$B` the long one, "Instruction Execution in Progress",
and `ap_m68030_step.c` chooses `$B` for every data fault on the reasoning that an
operand access failing partway through an unfinished instruction is the second
case. That reading was taken from Table 8-6 and had never been checked against
anything. The oracle produces `$B` too.

**So the item's verification line is met.** "Probes that deliberately fault,
diffed against oracle" -- two of them now, an illegal instruction (C73) and a bus
error (here), covering both the four-word frame and the long bus fault frame the
item names in its title. Sixteen campaigns from C59, and the last four went from
"no probe has ever been run against the oracle" to a frame-format agreement on
the exception path.

## C75 -- the harness cannot yet run a probe that branches backwards

**Class: harness limitation. Not a divergence, and it would read as one.**

The first probe with a loop -- `MOVEQ #3,D0 ; DBRA D0,self` -- produced this:

| Check | Ours | Oracle | |
| --- | --- | --- | --- |
| instructions executed | 7 | **1** | DIFFER |
| sentinel in memory | `0000FFFF` | `55555555` | DIFFER |
| against the architecture | `0000FFFF` | -- | ours correct |

**Read carelessly this is a `DBcc` disagreement. It is not.** The oracle executed
*one* instruction and stopped with its PC at `load + 2`, so it never reached the
`DBRA` at all, let alone the store -- `55555555` is the untouched sentinel, not a
computed answer. Nothing was compared. Our side ran the loop to exhaustion and
left `$0000FFFF`, which is what the architecture requires: `DBcc` decrements the
**low word only** and terminates at `-1`, so `MOVEQ #3` sign-extended into
thirty-two bits leaves the high word untouched. A full-width decrement would have
left `$FFFFFFFF`.

So this row records a limitation of `probe.lua`, which every probe until now
avoided by being straight-line: the sentinel, the FPU programs and both fault
probes each run forwards and stop. A backward branch is the first thing to make
the oracle side's stepping give up after one instruction, and why it does has not
been diagnosed -- the step loop, its `at_seconds` window, or MAME's own handling
of a tight self-loop are all candidates.

**Recorded rather than fixed** because the diagnosis needs a MAME run to watch,
and recorded rather than dropped because the output is *dangerous*: a differing
instruction count and a differing sentinel is exactly the shape of a real
finding, and the only thing distinguishing it is noticing that one side's count
is 1. The next person to add a looping probe will see this first.

**And the probe is worth keeping**, both because it is right and because it is
the reproducer: `probe_compare.py --program dbcc` demonstrates the limitation in
one command.

## C76 -- C75 was too broad, and the loop probe agrees

**Class: agree, and a correction to the row above it.**

C75 called this "the harness cannot yet run a probe that branches backwards" and
left the cause undiagnosed. Reading `probe.lua` settles it in one line, and the
limitation is far narrower.

The harness detects a halt by watching the program counter: **two consecutive
steps with an unmoved PC** is how it tells a `STOP` from a running program
without asking the debugger for a halt reason. `DBRA D0,self` branches to its own
address, so the PC does not move -- and a self-loop is therefore
indistinguishable from a stopped machine from outside. Backward branching is
fine; a *degenerate* loop is not, and my probe was one.

One instruction of body fixes it, and the comparison then runs:

| Check | Ours | Oracle | |
| --- | --- | --- | --- |
| instructions executed | 9 | 9 | agree |
| `D0` after the loop | `0000FFFF` | `0000FFFF` | agree |
| against the architecture | `0000FFFF` | -- | agree |

`DBcc` decrements the **low word only** and terminates at `-1`, so `MOVEQ #3`'s
sign extension leaves the high word untouched. A full-width decrement would give
`$FFFFFFFF` and a terminate-at-zero loop would run one iteration short -- two
mistakes that nothing in a straight-line probe can catch, and both now excluded
on both implementations.

**The correction is the more useful half of this row.** C75 generalised from one
failure to a whole class of programs and would have deterred the next looping
probe entirely; the real constraint is a single degenerate case with a one-word
workaround. Diagnosing it cost one `sed` of the harness. Recording an
undiagnosed limitation as though it were a boundary is its own kind of error, and
this file now carries the example.

## C77 -- MOVEM's mask reversal agrees, on the integer core's version of an FPU trap

**Class: agree.**

    MOVEM.L D0-D2,-(A7) ; MOVEM.L (A7)+,D3-D5 ; store D5

| Check | Ours | Oracle | |
| --- | --- | --- | --- |
| instructions executed | 8 | 8 | agree |
| `D5` after the round trip | `00000003` | `00000003` | agree |

Run with `python3 tools/mame-oracle/probe_compare.py --program movem`.

The register list's bit order **reverses** between the two modes -- for `-(An)`
bit 15 is `D0`, for `(An)+` bit 0 is -- and a model using one ordering for both
reverses every transfer while still moving the stack pointer exactly the right
distance, which is the failure that looks correct from the outside. It is the
integer core's version of the trap `FMOVEM` carries on the floating-point side,
where the same reversal is printed as two rows of a table.

**Three registers out and three *different* ones back**, deliberately: a round
trip into the same registers survives a double reversal and proves nothing. `D0`,
`D1`, `D2` carry 1, 2, 3 out; `D3`, `D4`, `D5` receive them; `D5` is 3 only if
both orderings are right.

**Where the comparison now reaches.** Straight-line integer work (the sentinel),
a counted loop (`DBcc`, C76), a register list in both directions (here), the
whole exception path through two different frames (C73, C74), and the
floating-point unit across seven functions and a rounding mode (C60 to C71,
C63's sweep). What it does not yet reach: `BSR`/`RTS`, `MULU`/`DIVU`, and the MMU
instructions, each of which has a probe in `ap_probe.c` that has never been run
against the oracle -- the suite and the harness are still separate instruments,
which is the standing tail rather than a gap in any item.

## C78 -- DIVU packs its two answers the same way on both

**Class: agree.**

    MOVE.L #100,D0 ; DIVU.W #7,D0 ; store D0

| Check | Ours | Oracle | |
| --- | --- | --- | --- |
| `D0` after the divide | `0002000E` | `0002000E` | agree |
| against the M68000 convention | `0002000E` | -- | agree |

Run with `python3 tools/mame-oracle/probe_compare.py --program divide`.

A word divide puts the **quotient in the low half and the remainder in the high
half** of one register, which is a convention rather than a consequence and is as
easy to write backwards as forwards. Both halves are non-zero and different here
so a swap cannot hide: `$000E0002` is the same arithmetic and the wrong
instruction.

**One case is deliberately not probed**, and saying so is the point of the row.
`DIVU` is the only integer operation on this part that can leave its destination
*untouched*: a quotient too wide for sixteen bits sets `V` and writes nothing.
Probing that needs the condition codes, and the sentinel machinery reads memory
rather than the status register -- so a probe would see an unchanged `D0` and be
unable to tell "overflow, correctly declined" from "the instruction did nothing".
Closing it means the harness reporting `SR`, which is a change to both sides and
is not attempted here.

## C79 -- BSR and RTS agree, and the integer core's probe classes are covered

**Class: agree.**

    BSR.W sub ; MOVE.L D0,(sentinel) ; STOP ; sub: MOVEQ #$2A,D0 ; RTS

| Check | Ours | Oracle | |
| --- | --- | --- | --- |
| instructions executed | 5 | 5 | agree |
| sentinel | `0000002A` | `0000002A` | agree |

Run with `python3 tools/mame-oracle/probe_compare.py --program subroutine`.

The only probe here whose correctness depends on a value the program never names:
the return address, pushed by one instruction and consumed by another. Everything
else writes what it later reads.

**The sentinel is checked rather than the program counter, deliberately.** A
`BSR` with a wrong displacement lands somewhere wrong and the probe stops without
storing; an `RTS` popping the wrong width returns somewhere wrong and does the
same. Both failures look like *a probe that did not run* rather than one that ran
differently -- the shape C75 already caught once. `$2A` in memory means the
subroutine was entered **and** returned from, and nothing else produces it.

**That covers `ap_probe.c`'s integer classes.** Straight-line register and memory
work, a counted loop, a register list in both directions, a packed two-result
divide, and now a call and return -- each run on both implementations and each
agreeing. What remains uncompared is the MMU instruction (`PMOVE`), which is
privileged and reads a register the sentinel machinery cannot reach, and the
`DIVU` overflow case C78 named for the same reason: both need the harness to
report registers other than through memory, which is one change serving two
gaps.

## C80 -- the case C78 called unreachable took four words, and C79 was wrong too

**Class: agree, and a correction to the two rows above it.**

C78 said `DIVU`'s overflow case "needs the condition codes, and the sentinel
machinery reads memory rather than the status register", so a probe could not
tell "overflow, correctly declined" from "the instruction did nothing". C79
generalised that into a harness limitation covering `PMOVE` as well -- "both need
the harness to report registers other than through memory, which is one change
serving two gaps".

**Both were wrong, and wrong the same way as C75**: a limit was inferred from one
framing of the problem without checking whether another framing avoided it. The
*program* can move any register to memory itself. `MOVE.W SR,Dn` reads the status
register; `PMOVE TC,(mem)` writes an MMU register straight out. Nothing about the
harness stands in the way, and no change to either side was needed.

Nor were the condition codes needed at all. The documented behaviour is that an
overflowing divide **leaves its destination untouched**, so storing the
destination *is* the test:

    MOVE.L #$100003,D0 ; DIVU.W #1,D0 ; store D0

| Check | Ours | Oracle | |
| --- | --- | --- | --- |
| `D0` after the overflow | `00100003` | `00100003` | agree |
| against the dividend | `00100003` | -- | unchanged, as documented |

`$00100003` over 1 is a quotient too wide for sixteen bits. Both implementations
set `V` and wrote nothing; an implementation that stored a truncated quotient
would have returned something else, and that is exactly the failure the row was
worried about.

**Three corrections now in this file** -- C65's misread golden, C75's overbroad
harness limit, and this one -- and they share a shape: a conclusion drawn from a
failed or unattempted first approach, stated as a property of the system. The
pattern is worth naming because the cost is asymmetric. A wrong "this agrees"
gets caught by the next probe; a wrong "this cannot be probed" is never revisited.

## C81 -- PMOVE agrees, and every probe class now reaches the oracle

**Class: agree. The campaign's coverage goal is met.**

    MOVEA.L #sentinel,A0 ; PMOVE TC,(A0) ; STOP

| Check | Ours | Oracle | |
| --- | --- | --- | --- |
| instructions executed | 3 | 3 | agree |
| `TC` at reset | `00000000` | `00000000` | agree |

Run with `python3 tools/mame-oracle/probe_compare.py --program pmove`.

The last of `ap_probe.c`'s classes to reach the oracle, and the one C79 called
unreachable before C80 showed a register only has to be moved to memory *by the
program*. `PMOVE TC,(An)` does it in one instruction.

The value is a real claim rather than a formality: `TC` must come out of reset
with translation **disabled**, and a machine that powered up translating would
fault on its first instruction. It is also the only probe here that reads a
register the program never wrote, so a value arriving at all is evidence the
coprocessor answered rather than the memory being untouched -- the oracle's
sentinel defaults to `55555555`, not zero.

**Coverage, which was the point.** Every class in `ap_probe.c` now runs on both
implementations and agrees:

| class | probe |
| --- | --- |
| register write | `sentinel` |
| operand write-through and read-back | `sentinel` |
| counted loop, low-word decrement | `dbcc` |
| call and return through the stack | `subroutine` |
| 32-bit divide, packed two results | `divide` |
| divide overflow leaving the destination alone | `divide-overflow` |
| register list, both mask orderings | `movem` |
| MMU register read | `pmove` |
| exception path, four-word frame | `fault` |
| exception path, long bus fault frame | `bus-fault` |
| floating point: constant ROM, add, store | `fpu` |
| floating point: rounding mode honoured | `fpu-rounding` |
| floating point: seven functions adjudicated | `fpu_sweep.py` |

Twenty-three campaigns, C59 to C81. It began with a coprocessor that had never
been attached to a machine and a verification line that had never been run.

## C82 -- the campaign becomes a regression check

**Class: infrastructure.** The measurements of C59 to C81 are now one command.

    python3 tools/mame-oracle/probe_compare.py --program all

    known difference: fpu-sine-x -- C70, one ULP, settled sub-poll-slack
    12 of 13 probe programs ran identically; 1 differed as recorded

Thirteen separate invocations became one, which matters less for convenience than
for what it changes about the result: a campaign is a measurement taken once,
and this is a check someone can re-run after touching the core. The list itself
records what has been compared, so a class that was never covered is visible in
the source rather than only in this file.

**The known difference is declared rather than tolerated.** `fpu-sine-x` differs
by one unit in the last place and is *expected* to -- C70 settled it as a
resolution limit, not a defect. It is listed with its reason, so a green run
means nothing has changed and a red one means something new has. A suite that
went red every time for a known reason would train its reader to ignore the
colour, which is exactly the failure C75's output nearly caused when a stalled
harness produced the shape of a real finding.

**And the check runs both ways**: a program in the known-difference list that
*stops* differing is also reported. If the transcendental bias is ever closed --
the standing `PROVISIONAL` in `PROJECT_STATUS.md`, priced at one unit in the last
place across three functions -- this suite will say so on the next run rather
than quietly going green.

## C83 -- the comparison can name a machine, and a DN3000's RAM is not where a DN3500's is

**Class: `open`, with the obstacle measured rather than predicted.**

`probe_compare.py` now takes `--machine`, passing it to `apollo-headless --model`
on this side and using it as MAME's driver name on the other. That was the last
piece of plumbing the 68020 subset's verification line needed: it asks for an
oracle diff, and until this session nothing either side built was ever a 68020.

Running it says so immediately:

    python3 tools/mame-oracle/probe_compare.py --machine dn3000

    oracle: exception table at 01003100, handler 01003000,
            NOT RAM -- vectors will not take

**`ORACLE_BASE` is the DN3500's.** `01000000` is where a DN3500 puts main
memory, and the harness has hard-coded it since the first probe because every
probe until now ran on one. A DN3000 puts its memory somewhere else, and the
diagnostic added in C72's addendum -- the one that spent a turn unwritten --
caught it on the first run rather than leaving a probe to fail obscurely.

**What this needs is a base per machine**, which is a table rather than a
discovery: MAME's `dn3000` driver states its map, and `--listxml` or the driver
source gives it without guessing. The `sentinel` probe's own program is already
assembled twice at two bases, so nothing else in the harness has to change --
`ORACLE_BASE` simply stops being a constant.

Worth noting what *did* work: the model reached both sides, MAME started the
right machine, and the failure was reported by the harness rather than surfacing
as a mysterious disagreement. The plumbing is right and the address is wrong,
which is the better of the two ways for this to have gone.

## C84 -- the DN3000 comparison runs, and agrees

**Class: agree.** The 68020 subset's verification line is met.

    python3 tools/mame-oracle/probe_compare.py --machine dn3000

    instructions executed  3 / 3          agree
    D0                     0000005A       agree
    sentinel in memory     0000005A       agree

    not compared: pc (0000100C vs 0010100C) differs by the RAM base

C83's obstacle was one constant. MAME's driver states both maps outright --
`DN3500_RAM_BASE 0x1000000`, `DN3000_RAM_BASE 0x100000` -- so the base became a
per-machine lookup taken from the oracle's own statement of the fact rather than
measured. The probe programs were already assembled twice at two bases, so
nothing else moved; the exception table's address follows the base for the same
reason.

**The 68020 subset asked for "`dn3000` boots under both; oracle diff".** The boot
half moved to Phase 4 long ago -- a boot needs a board. The diff half is this,
and it could not be run at any earlier point in this session for three separate
reasons, each fixed in turn: `ap_machine_init` took no model, then the probe
runner did not pass one, then the harness knew only one machine's memory map.

What it establishes is narrow and worth stating precisely: a program runs
identically on a 68020 and a 68030 under both implementations. It does *not* yet
compare the 44 opcodes where the families differ -- `CALLM` needs a module
descriptor in memory, which the probe encoder does not build. That is the
natural next probe and it is now only a program: the machine, the base, the
model and the harness are all in place.

## C85 -- the family-difference probe exists, and finds our own plumbing

**Class: `ours-wrong`, open.** The first probe to compare the thing that actually
differs between the two CPU families, and it fails on this side before it can
compare anything.

    build a module descriptor ; CALLM into it ; the module stores D3

    instructions executed  3 / 5     DIFFER
    sentinel               00000000 / 55555555

Our side executes the three `MOVE.L`s that build the descriptor and stops at the
`CALLM`, program counter at `load + 30`, which is the instruction itself. The
oracle gets five instructions in. Neither reaches the store, so nothing about
`CALLM` has been compared -- what the probe found is nearer home.

**`step_suite` executes `CALLM` perfectly well**, in three tests, and it does so
by setting `cpu.has_module_calls = true` on the CPU directly. The probe reaches
the same flag through `--model dn3000` → `run_probe_file` → `ap_probe_run` →
`ap_machine_init_model` → `ap_cpu_features(model->cpu).has_module_calls`, and
somewhere along that chain it does not arrive. The instruction is not the
suspect; the four links between a command-line flag and a struct field are.

**Two things this establishes anyway.** The probe is written and both sides run
it, so once the flag arrives the comparison is one command. And the argument
count is the word *immediately after the opcode*, with the effective address's
extension words following it -- the first version put the address first, which
ran the count as an address and stopped the probe at the instruction on *both*
implementations at once. That symmetric failure is worth remembering: a probe
wrong in the same way on both sides looks like agreement on the instruction
count and disagreement only in the result.

**Next**: print `has_module_calls` from the probe-file path, or assert it in
`probe_suite.c` against a machine built as a DN3000. One of the four links is
dropping it, and the test that would have caught it does not exist -- every
existing test sets the field rather than deriving it.

### C85 addendum -- the probe cannot tell which of two failures it hit

Narrowing C85 ran into a limit of the comparison itself, which is worth more than
the narrowing was.

The chain checks out by inspection at every link: `ap_cpu_features(AP_CPU_M68020)`
sets `.has_module_calls = true`, the DN3000's table entry is `AP_CPU_M68020`,
`ap_machine_init_model` reads the feature into the CPU, and `ap_machine_reset`
does not clear it -- it sets named fields rather than blanking the struct. So the
flag *should* arrive, and the fault may instead be inside `execute_callm`.

**And the probe cannot distinguish those two.** A `CALLM` refused for want of the
family flag reports `ILLEGAL` and leaves the program counter at the instruction;
a `CALLM` this model declines reports `UNIMPLEMENTED` and leaves it in exactly
the same place. `probe_compare.py` compares the instruction count, `D0` and the
sentinel -- none of which separates them. The `--probe-file` output *does* carry
the status, and the comparison simply does not read it.

That is the fix, and it is worth making for its own sake rather than for this
bug: a probe that ends early currently reports "stopped somewhere" and the reason
is one field away. Every campaign from C59 has been reading around it -- C75's
stalled loop, C83's unlanded vector table, and now this -- each diagnosed by
noticing something *other* than the status the harness already had in hand.

**Next, in order**: compare the status field, then re-run this probe, and the
answer will name itself. That is one line of `probe_compare.py` and it retires a
class of ambiguity three campaigns have worked around.

## C86 -- the status field named the bug in one run, and it was mine

**Class: `ours-wrong`, found and fixed.**

C85's addendum predicted that comparing the status would make the answer name
itself. It did, on the first run:

    status    ILLEGAL    None    DIFFER

`ILLEGAL`, not `UNIMPLEMENTED`. So `CALLM` was being refused for want of the
family flag, not declined by `execute_callm` -- and the flag genuinely was not
arriving, despite a chain that checked out at every link by inspection.

**The bug was two lines apart in code I had written an hour earlier.**
`ap_machine_init_model` set `machine->model` and then, six lines later, blanked
the whole struct:

    machine->model = ap_model_by_id(model);
    ...
    *machine = (ap_machine_t){0};        /* every field, deliberately */

The blanking is right and its comment explains why -- a machine whose behaviour
depended on the caller's stack would not be reproducible. Setting the model
before it was simply the wrong side of the line. Every machine built as a DN3000
therefore had a null model, no module calls, and reported `CALLM` illegal.

With the assignment moved after the blanking, our side executes the probe and
stores `$00C0FFEE`: the descriptor read, the frame built, the entry word
honoured, execution continued past it.

**And the oracle does not.** MAME reaches five instructions, never stores, and
its sentinel is untouched. That is now a real question rather than a plumbing
fault, and the likely answer is that MAME's 68020 core does not implement `CALLM`
at all -- it is the rarest instruction in the family and emulators commonly skip
it. Left `open` and not asserted: proving it means reading the oracle's m68k core
or watching the instruction, and this project's rule is that a difference is not
automatically the oracle's fault either.

**What the campaign should take from this.** Three campaigns diagnosed their way
around a field both sides had always printed. The status was not a missing
measurement; it was an unread one. When a comparison cannot separate two
explanations, the first question is whether the harness already knows and is
being asked the wrong thing.

## C87 -- we implement CALLM and the oracle does not

**Class: `oracle-wrong`.** Settled from the oracle's own source, not inferred
from its behaviour.

C86 left the question open: our side executes the module-call probe and stores
`$00C0FFEE`, MAME reaches five instructions and never stores. Reading the core
answers it outright.

`ext/mame/src/devices/cpu/m68000/m68kops.cpp`, in every `callm` handler:

    logerror("%s at %08x: called unimplemented instruction %04x (callm)\\n", ...)

and Musashi's own instruction table, `ext/musashi/m68k_in.c` line 504, is blunter
still -- the `callm` row ends **"not properly emulated"**.

So this is the divergence class `FINDINGS.md`'s opening rule exists for: "MAME's
own driver notes admit gaps ... So every disagreement is *classified*, never
silently 'fixed' by moving our number to match." `CALLM` and `RTM` are the two
instructions unique to the 68020, they appear in no Apollo firmware this project
has read, and an emulator serving hundreds of drivers has no reason to carry
them. This core does, because the 68020 subset is a named plan item and the
instructions are in the part.

**Recorded as a known difference** in `probe_compare.py`, with its reason, so
`--program all` stays green when nothing has changed and goes red when something
new has. That is now two: `fpu-sine-x`, a resolution limit where the oracle is
the more accurate of two conforming implementations; and `module-call`, where
the oracle does not implement the instruction at all. **One in each direction**,
which is a fair picture of what an oracle is for.

It also closes the 68020 subset properly. C84 established that a program runs
identically on both as a DN3000; this establishes what happens at the 44 opcodes
where the families differ -- we execute them and the oracle declines, from its
source rather than from a guess.

## C88 -- running the whole suite caught a check that cried wolf

**Class: harness defect, mine, found and fixed.**

Adding `module-call` to `--program all` and running it as a DN3000 reported
**twelve unexpected differences**. None was real. The status check added two
campaigns earlier compared `ours["status"]` against `oracle["status"]`, and the
oracle prints the same fact under a different key -- `stopped WHY`, not
`status WHY`. Every probe therefore differed on a field neither implementation
disagreed about.

It had been in the tree since C86 and nothing noticed, because C86 ran the one
probe the check was written for and the difference there was genuine. Running
fourteen programs is what exposed it, which is the argument for `--program all`
existing at all: a check that is right for the case it was written for and wrong
everywhere else looks correct until something sweeps.

Fixed, and the run is now what a regression check should read like:

    known difference: fpu-sine-x  -- C70, one ULP, settled sub-poll-slack
    known difference: module-call -- C87, oracle-wrong: MAME does not
                                     implement CALLM
    12 of 14 probe programs ran identically; 2 differed as recorded

**`module-call` is registered only on a DN3000**, which is the machine where the
difference is real. A known-difference list that is wrong in the safe direction
still teaches its reader to distrust it.

> **Corrected by C89.** The sentence that stood here -- "on a DN3500 neither
> implementation executes `CALLM`, so the two agree by both refusing" -- was
> wrong, and was written without running it. The two did *not* agree: this core
> stopped where the hardware takes vector 4. Registering the entry on the DN3000
> only happened to be right, for a reason other than the one given. C89 has the
> defect, the fix and the third category the DN3500 case actually needed.

**Three harness defects now, each found by using the harness rather than
reading it**: C75's stalled loop, C83's DN3500-only memory map, and this. All
three produced output that looked like a finding about the emulator.

## C89 -- an illegal instruction has to take its trap, and C88's correction was wrong

**Class: defect in this core, found by disbelieving my own previous entry.**

C88 asserted that on a DN3500 "both sides refuse `CALLM` and agree by both
refusing", and registered the known difference on the DN3000 only. That claim
was never run. Running it showed **four checks differing**, and the reason was a
gap in this core rather than in the harness.

### What §8.1.5 requires

`[030]` §8.1.5, p. 8-9, read from the page image:

> An illegal instruction is an instruction that contains any bit pattern in its
> first word that does not correspond to the bit pattern of the first word of a
> valid MC68030 instruction or is a MOVEC instruction with an undefined register
> specification field in the first extension word. An illegal instruction
> exception corresponds to vector number 4 and occurs when the processor
> attempts to execute an illegal instruction.

p. 8-10 adds the two neighbouring families: bits [15:12] = `$A` takes vector 10,
and `$F` with bits [11:9] = 0 takes vector 11 in supervisor mode but a
**privilege violation** in user mode.

This core took none of them for an undecodable word. `$4AFC` -- the *deliberate*
`ILLEGAL` instruction -- vectored correctly, and a word the decoder simply
rejected returned a status and stopped the machine. `ap_m68030_opcode_emulator_vector`
and `ap_m68030_coproc_unsupported_vector` both existed, both documented, and
neither had a caller in `src/core`.

### Why the fix is narrow, and must be

Vectoring on *every* undecodable word would be wrong here, and wrong in the
expensive direction. This decoder is not the 68030's: a word it rejects may be an
instruction nobody has implemented yet, and raising vector 4 on it would turn
every unfinished corner into a machine that looks correct -- failing silently,
where the old behaviour failed loudly at the gap. That is the trap the existing
`CALLM` comment already warned about in the other direction.

So the trap is taken only where the word is positively identified as **another
family member's instruction that this model removed**. `CALLM`/`RTM` at
`$06C0`-`$06FF` is that case and, on this machine, the only one -- which
`ap_model.h` had already written down: "removed from the 68030 onward, where
their encodings take an F-line/illegal path instead". Every other undecodable
word still reports `ILLEGAL` and stops.

Vector 4 stacks the *faulting* instruction rather than the next one
(`ap_m68030_stacks_next_instruction`), so both stacked addresses are the PC as it
stands -- which is what lets the exception be raised before any instruction
length has been established.

### The test that nearly passed for the wrong reason

The new test's second half asserts that an unclaimed word still stops. It was
first written with `$FFFF`, which **failed** -- because `$FFFF` is F-line, and
F-line words vector to the line 1111 emulator entirely correctly. The word that
was chosen to prove "nothing else vectors" was itself a legitimate vector. It is
now `$003D`: `ORI.B` with effective address mode 111 register 101, a register
field mode 111 has never assigned on any member of the family and so cannot
become valid later.

### A third category the harness did not have

With the fix in, a DN3500 and MAME both fault on `CALLM` and both vector to the
planted handler -- which is an `RTE`, returning to the faulting instruction. Both
therefore spin in an illegal-instruction loop until the step limit, and the
counts then compare **each harness's bookkeeping for instructions during
exception processing**, not the machine.

That is neither agreement nor a known difference, and the harness had no way to
say so. `module-call` is now marked *not applicable* to a DN3500 and skipped
there, still running on the DN3000 it was written to interrogate:

    not applicable to dn3500: module-call -- CALLM is not a 68030 instruction;
        both sides fault and loop in the handler, so the run measures step
        accounting rather than the machine
    known difference: fpu-sine-x -- C70, one ULP, settled sub-poll-slack
    12 of 13 probe programs ran identically; 1 differed as recorded;
        1 not applicable to a dn3500

Filing it as a known difference instead would have recorded harness accounting
as a finding about the hardware -- the failure C88 named one entry earlier and
then committed.

**The lesson is about C88, not about the 68030.** C88's correction was itself
asserted without running, in the same entry that criticised checks which are
right for the one case they were written for. An unrun claim is unrun whether it
appears in code or in a findings document, and a findings file is the worse place
for one: the next reader has no failing test to catch it.

## C90 -- the timing model was documented as one that had been retired

**Class: documentation defect, three sites, no behaviour wrong.**

Found by sweeping for public functions the *product* never calls -- the category
that has now produced three findings, because in this codebase a declared-and-
unconsulted function usually means a rule stated in one place and implemented
differently in another.

`ap_m68030_schedule` is `max(microcode, bus)`, is tested, and **nothing in the
step calls it**. The step's own framing comment nonetheless said the cost was
"the two *scheduled* rather than summed", and `PROJECT_STATUS.md` said so twice
more -- once in the summary and once in the subsystem table, both naming
`max(microcode, bus)` as what the machine runs.

It is not, and `docs/references/M68030_TIMING.md` had already recorded why under
"`max(microcode, bus)` does not survive the effective address tables": `max` is
monotonic in both arguments, while warm and cold `ADD.B D0,(A0)` need 6 and 7
against bus times of 4 and 6 -- answers that move the opposite way to their
inputs, so no microcode figure reaches both.

What the step actually implements is the refinement that document proposed --
one question per bus cycle, *is the microcode waiting on this?* A prefetch is
not waited on and can hide; an operand read the operation is about to consume
cannot. So:

    exposed microcode + measured operand bus + prefetch exposure

The behaviour was never in doubt:
`test_every_transcribed_row_matches_both_published_columns` checks all 59 rows
against `CC` warm and `NCC` cold **on a running machine**, and passes. Only the
description was wrong.

**Why a wrong model description is a real defect here and not a nitpick.**
`CLAUDE.md` makes `PROJECT_STATUS.md` the document "read by whoever has to trust
or change a subsystem". Someone changing timing would have reasoned from `max`,
found the code disagreeing with the document, and had to guess which was
authoritative -- and the plausible guess is that the document is right and the
code has drifted, which is backwards. A stale model description is worse than no
description: it is confidently wrong at exactly the moment someone needs it.

Also corrected: `ap_m68020_decode.h` described
`ap_cpu_instruction_is_illegal` as "the question the step actually asks". The
step has never called it -- it carries `has_module_calls`, not an `ap_cpu_t`,
and reads the module decoder directly. The sweep is what the predicate is for.

**The sweep itself is worth keeping as a technique.** `nm` alone is not enough:
it cannot see within-translation-unit calls, so its first answer flagged
`ap_m68030_take_interrupt` (called inside `ap_m68030_step`) and the twelve
`ap_board_hash_*` helpers (aggregated by `ap_board_hash` in their own file) as
dead. Counting occurrences across `src/**.c` instead separates "the product
never calls this" from "nothing calls this", and it is the first of those two
that finds rules with two implementations.

## C91 -- an enabled floating-point exception never trapped

**Class: missing behaviour in this core, found by the C90 sweep.**

The same sweep that produced C90 -- public functions the *product* never calls
-- showed `ap_m68882_exception_enabled`, `ap_m68882_inexact_trap` and
`ap_m68882_raise_exception` reachable only from tests, and nothing anywhere
taking a vector in 48-54. `PROJECT_STATUS.md` had recorded the consequence
honestly ("there is no 68030-side FPU trap path yet to call them") but it was
neither an open plan item nor a deferred tail, and `CLAUDE.md` says nothing is
deferred silently. So it was a hole rather than a decision.

**Nothing failed, and nothing could have.** A trap that is never delivered
breaks no test that does not ask for one, and no probe asks: the FPCR resets
with every trap disabled, so every existing FPU probe exercises precisely the
path where the gap is invisible. This is the argument for auditing by *coverage
of the specification* and not only by running what exists.

### Three orderings, none of them the bit order

The shape of the implementation is dictated by the fact that the same seven
conditions are ordered three different ways:

| | order |
|---|---|
| FPSR `EXC` bits | BSUN 15, SNAN 14, OPERR 13, OVFL 12, UNFL 11, DZ 10, INEX2 9, INEX1 8 |
| trap priority, §6.1.9 | BSUN, SNAN, OPERR, OVFL, UNFL, DZ, INEX2/INEX1 |
| vector, `[030]` Table 8-1 | 48 BSUN, 49 Inexact, 50 DZ, 51 UNFL, 52 OPERR, 53 OVFL, 54 SNAN |

`48 + bit` is wrong and `48 + position in the priority list` is also wrong. Both
mappings are transcribed, and split so that the FPCP answers *which exception*
and the MPU answers *which vector* -- which is also what keeps the module
dependency one-way, `m68030` including `m68882` and never the reverse.

### The trap is not taken by the instruction that caused it

§6.4.2, p. 6-33, is the passage that decides the design: with `EXC PEND` true and
"an attempt ... made to initiate an FPCP instruction (other than an FMOVEM,
FMOVE control register, FSAVE, or FRESTORE), the response CIR is encoded to the
take pre-instruction exception primitive".

So the part runs concurrently and the trap waits for the *next* non-exempt
floating-point instruction, arriving before it executes. Being a pre-instruction
exception it stacks that instruction's own address, so `RTE` re-attempts it --
`ap_m68030_stacks_next_instruction` gained the FPCP range, without which every
handler would have returned one instruction too far on.

The exempt four are exactly what a handler needs. §6.1.9 tells handlers to move
data with `FMOVEM` because it "cannot generate further exceptions"; if `FMOVEM`
reported the pending trap, the first instruction of every handler would re-enter
the handler.

### `EXC PEND` derived, not latched -- and the reading that costs

Derived as `EXC & ENABLE`. The manual's account of *clearing* is what makes that
truer rather than merely simpler: this part does not clear it on acknowledge at
all -- "the MC68881 detects the exception acknowledge, [and] clears EXC PEND.
However, the MC68882 does not clear the EXC PEND bit. It is the responsibility of
the exception handler to clear EXC PEND" -- and what a handler clears it *with*
is a write to the FPSR. Deriving makes that write do the job by construction,
where a second latch would have to be cleared in step with a register it
duplicates.

**Stated cost:** enabling a trap in the FPCR *after* an exception was recorded
arms it here, where a latch set at the moment of occurrence would not. §6.4.2
leans this way -- "a programmer can make exceptions pending in the FPCP under
software control. Or, conversely, a pending exception type may be changed or
cleared if necessary" -- but it is a reading and is recorded as one rather than
presented as settled.

### Verification

Three tests, each catching what the other two would let through: the
divide-by-zero traps on the *following* `FADD` and not on the divide; `FMOVEM`
runs to completion with the trap still pending; and a **disabled** exception sets
its FPSR bit and traps nothing -- the last mattering most, since it is the case
nearly every real program is in and the one an over-eager implementation breaks.

112/112 on debug and release, and the `fpu`, `fpu-rounding` and `fpu-sine`
probes still run identically against the oracle -- expected, since they leave
every trap disabled, and checked rather than assumed.

**Not yet measured against the oracle.** No probe drives an enabled FPU trap,
because doing it properly means planting vectors 48-54 on both sides and having
the handler report which arrived. That is a real gap in the *verification* and
is now a named plan item rather than an unstated one -- the whole lesson of this
entry being that a gap nothing exercises is a gap nothing reports.

## C92 -- the FPU trap probe, and MAME has no FPCP exception vectors

**Class: oracle-wrong, settled from the oracle's source rather than its
behaviour.**

C91 closed the missing trap path and left the verification open, as a named plan
item: no probe drove an *enabled* floating-point trap, so the whole mechanism
rested on the manual and three unit tests. This closes it.

### One value, three claims

`fpu-trap` enables `DZ` alone, divides 1.0 by 0.0, and executes an `FADD` that
must never run. Its own handler stores the stacked format word, and `$000000C8`
is the answer:

* **that it trapped at all** -- the sentinel is written only from the handler;
* **through vector 50** -- offset `$C8`. That number is derivable from nothing
  else in the encoding: `DZ` is FPSR bit 10 and sixth in §6.1.9's priority
  order, so neither `48 + bit` nor `48 + priority` reaches 50. A wrong mapping
  plants the live handler on a different vector, and the probe then reports "no
  trap" *exactly as an unimplemented trap path would* -- which is why
  `test_encoder` pins the planted offset, the enabled bit and the handler entry
  separately;
* **in a four-word frame** -- the format-0 nibble, which is what a
  pre-instruction exception takes.

The handler cannot be the harness's bare `RTE`. A pre-instruction exception
stacks the address of the instruction that was *attempted*, so an `RTE` returns
to the `FADD` and traps again -- forever, because a 68882 does not clear
`EXC PEND` on acknowledge. That loop is correct hardware behaviour, and it is
the reason this probe carries its own handler ending in `STOP`.

### The oracle does not implement this at all

MAME runs all nine instructions, including the `STOP` this core never reaches,
and leaves the sentinel at its `55555555` fill.

**Settled by reading `ext/mame`, not by inferring from that.** `m68kfpu.cpp`
contains exactly two exception raises in its whole length: `m68ki_exception_1111`
for an unimplemented encoding, and `m68ki_exception_trap(EXCEPTION_TRAPV)` for
`FTRAPcc`. There is no path to any vector in 48-54. MAME's FPCP has no exception
traps, so there is no version of this probe it could pass, and no instrumentation
run would have told me more than the grep did.

That distinction matters for how the entry should be trusted: "MAME did not trap"
is an observation and could have a dozen causes -- a rejected FPCR write, a
missing `DZ` in the divide, a vector planted somewhere MAME does not read.
"MAME contains no code that could trap" is a fact about the program, and it
disposes of all of them at once.

    known difference: fpu-trap -- C92, oracle-wrong: MAME implements no FPCP
                                  exception vectors
    12 of 14 probe programs ran identically; 2 differed as recorded;
        1 not applicable to a dn3500

### Why this one is worth the entry

C91 was found by reading rather than running, because every existing FPU probe
leaves the FPCR at reset and therefore exercises precisely the path where a
missing trap is invisible. The suite could not have caught it. It can now, and
the thing it will catch is a *regression* -- because the probe is registered as
an expected difference, a run where `fpu-trap` suddenly agrees with MAME means
this core stopped trapping.

## C93 -- two reset sequences, and the machine used the wrong one

**Class: defect in this core, found by continuing the C90 sweep.**

`ap_m68030_take_reset` implements `[030]` §8.1.1's ten steps carefully, with a
header comment naming the four "a plausible implementation drops" and the two
explicit negatives. It has **no caller anywhere in `src`** -- not even in its own
translation unit.

What the machine actually runs is `ap_machine_reset`, which had written its own
shorter sequence: supervisor, mask 7, trace clear, and then straight to the
program counter. Set aside were steps 4, 5 and 7 -- the vector base register,
the cache control register, and the enable bits in the translation control
register and *both* transparent translation registers. Added was a flush of the
ATC.

§8.1.1, p. 8-5, read from the page image, closes with:

> After the initial instruction prefetches, program execution begins at the
> address in the program counter. The reset exception **does not flush the
> address translation cache (ATC)**, nor does it save the value of either the
> program counter or the status register.

So the machine's reset did something reset never does, and omitted three things
it always does. The header of the *unused* function had already said as much --
"a model that flushed it would be tidier and wrong" -- which is the same shape as
C90: the rule written down in one place and contradicted by the code that runs.

### Why nothing caught it

**The omission is invisible exactly once.** `ap_machine_init` zeroes the whole
struct, so on a cold start VBR, CACR and every enable bit are already what reset
would have made them: the short sequence and the full one agree, and every test
in the tree resets a freshly constructed machine. Every *later* reset is on a
machine that has been running, and there they diverge -- a warm reset would keep
the old VBR and the old translation tree and fetch its first instruction through
them.

The fix is structural rather than a patch: §8.1.1's steps 1-7 are now
`ap_m68030_reset_state`, called by both `ap_m68030_take_reset` (which then reads
the vector for steps 8-10) and by `ap_machine_reset` (which is *told* its program
counter, because a board's PROM supplies it rather than a vector at zero). One
sequence, one place.

### The test was checked against the defect, not only against the fix

`test_a_warm_reset_restores_the_documented_state_but_not_the_atc` resets a
machine that has been dirtied. Both halves were confirmed live by putting the old
behaviour back and watching them fail -- the VBR assertion against the partial
sequence, and the ATC assertion against a re-added flush, separately, because
Unity stops at the first failure and a single run would only ever have proved
one of them. The instrumentation was reverted from a kept copy rather than by
`git checkout`.

That check is the point of the entry as much as the defect is: a test written
*after* a fix passes against the fix by construction, and says nothing about
whether it would have caught the bug.

## C94 -- the PROVISIONAL audit claimed both directions and had never held one

**Class: documentation-discipline defect, no behaviour wrong.**

`CLAUDE.md` requires three things of a figure that was chosen rather than
transcribed: mark it `PROVISIONAL` **in code**, record it in
`PROJECT_STATUS.md`, and **make it a named plan item**. The status document's
`PROVISIONAL figures` table opened by asserting the invariant had been checked:

> Every entry is also a named item in `docs/COMPLETION_PLAN.md`, and every
> `PROVISIONAL` in the source is one of these. Audited in both directions.

Running that audit -- the same move that produced C89 -- found the reverse
direction had three holes:

* the **68882's microcode version number**, `PROVISIONAL` in two source files;
* the **68882 idle state frame's internal words** (CU internal registers,
  operand register, BIU flags), written as zeros;
* the **Apollo AT map's entry indexing**, `(address - base) / 2`.

None was a table row. The second was not a named plan item either, only prose in
the status document -- which is precisely the silent deferral the discipline
exists to prevent, and `CLAUDE.md` says nothing is deferred silently.

All three are now rows, the idle-frame words are named in the `FSAVE`/`FRESTORE`
plan item, and the claim is repaired rather than deleted: the invariant is worth
having, it had simply never been run.

**Two things about how the check went are worth keeping.**

The claim said "every `PROVISIONAL` in the source", and that can never be true
as written: three files say the word without naming a figure -- `ap_time.h`
points at the model clocks, and `ap_frontend.c/.h` *print* the marker for
whichever models carry one. A grep returns more sites than the table has rows
and always will. The claim now says "every `PROVISIONAL` **figure**", and says
why the word is doing work.

And one apparent fourth hole was not one. The row "68030 `+` rows not yet
transcribed" returned nothing for any keyword I tried, and the honest reading
was a second missing item -- but the plan names it as "the `+` rows that are
**not**" inside an item about data-dependent timings, which is a real named item
and my searches simply missed. It is written down here because *nearly* filing a
false defect is the same error as filing one, caught one step later: the table's
own preamble had warned "check the concept, not the phrase", and I checked the
phrase four times before the concept.

The repaired claim now carries a **count** -- 17 rows, each with a plan item --
rather than an adjective. An audit assertion is worth exactly as much as the
last time someone ran it, and a number invites the next reader to re-run it in a
way that "audited in both directions" does not.

## C95 -- 791 encodings the processor refuses were reported as our own gap

**Class: defect in this core, found by sweeping the specification rather than
running the tests.**

All 65536 opcodes, stepped on a real machine, counting outcomes. 2621 came back
`UNIMPLEMENTED` -- "this model has not got to it". A large share were nothing of
the kind.

`MOVE`'s destination must be data alterable; `LEA`'s source must be control; the
MMU's operand must be control alterable. Those are rules transcribed from the
manual's own category tables, and a word failing one **is not a valid MC68030
instruction** -- so §8.1.5's answer is the illegal instruction exception, not a
report that this core is unfinished. The code already knew: the check by `MOVE`
reads "an instruction the processor refuses, running here", and then returned
the status that says the opposite.

The consequence was not cosmetic. `UNIMPLEMENTED` stops the machine; the
hardware enters a handler and carries on. **791 words** now take their trap.

### The vector is not always 4, and a flag would have hidden that

The refusal is carried as a *vector*, not a boolean. An F-line word with cpID 0
-- the MMU's own encodings -- is p. 8-10's "unimplemented instruction with an
F-line opcode" and takes **vector 11**. A single flag would have sent every
`PMOVE` refusal to the illegal-instruction handler: the wrong handler, reached
plausibly, with nothing to say so. The privilege check that precedes every MMU
instruction already handles the other half of that same paragraph, a user-mode
attempt taking the privilege violation.

### Four tests were asserting the wrong verdict

`test_lea_refuses_an_increment_mode_it_decodes_perfectly_well`,
`test_a_move_cannot_write_through_the_program_counter` and the two `PMOVE`
refusals all failed, each expecting `UNIMPLEMENTED`. Their *names* and their
comments were right -- the instruction is refused -- and only the asserted status
was wrong, which is what a test written against the implementation rather than
against the manual looks like from the outside. They now assert the exception and
the vector it arrived through.

### Two things this sweep found that are not this fix

**`NEGX.B #imm` is still `UNIMPLEMENTED`, and for a worse reason than it looks.**
The single-operand, immediate and shift groups enforce **no** categories at all:
that instruction is refused today only because writing to an immediate happens to
fail somewhere downstream. It is the same defect one layer earlier -- a missing
check rather than a mislabelled one -- and is now an open plan item.

**`MOVEP` is genuinely absent**, not misclassified. It is in the remaining count
as a real missing instruction.

1830 words remain `UNIMPLEMENTED`. That is now a *measured* number with a named
largest cause, where before it was an impression -- and the sweep that produced
it is repeatable in a few seconds, which is the part worth keeping.

**Method note.** I misread `$15C0` as `MOVE.B D0,(d16,A2)` and was two minutes
from reporting that this core could not move to a displacement destination --
one of the commonest instructions in 68k code. It is mode 111 register 2,
`(d16,PC)`, and testing every destination mode is what caught it. Reading bit
fields off a hex literal by eye failed here in the same way the phrase-versus-
concept search failed in C94, and one step from the same outcome.

## C96 -- 578 words were executing instructions the hardware refuses

**Class: defect in this core, the expensive direction. Closes C95's tail.**

C95 enforced the effective-address category tables where checks existed. The
single-operand, immediate and shift groups had **none**, so `NEGX.B #imm` was
refused only because writing to an immediate happens to fail somewhere
downstream, and a great many invalid encodings simply *ran*.

C95 moved 791 words from *our gap* to *the machine's refusal*. This moves **578
words from executing to refusing**, and those are the ones that matter:
`ap_m68030_category.h` has always said that accepting words the processor
refuses is "the wrong direction to be wrong in, because a real program never
contains them and only a broken one benefits". It was happening in three whole
instruction groups.

### The rules came from the pages, not from a neighbour

The `[PRM]` states the category in prose on every instruction page -- "Only data
alterable addressing modes can be used" -- so the extraction is that sentence
per instruction rather than one rule generalised across a group. **Three
instructions would have been got wrong by generalising**, and each is a
different kind of exception:

* **`TST` reaches every addressing mode**, immediate and PC-relative included --
  a 68020 widening, its PC rows footnoted "PC relative addressing modes do not
  apply to MC68000, MC680008, or MC68010". Its neighbours are data alterable, so
  the group's rule would have refused three forms this processor runs. Its one
  restriction is a **size** rule that no category can express: "Address register
  direct allowed only for word and long". `TST.B An` is the single illegal
  `TST`, and this core executed it.
* **`CMPI` is data, not data alterable.** Its table also dashes the immediate --
  but the *decoder* already owns that row, because `mode 111 register 100`
  carries the `CCR`/`SR` forms and `CMPI` has none. A check here would have been
  a second copy of the rule that never runs, which is the shape C90 was about.
* **`BTST`'s two forms disagree about the immediate.** `BTST Dn,<ea>` lists
  `#<data>`; the static `BTST #n,<ea>` on the facing page dashes it, having
  already spent the immediate on its bit number. This one cannot live in the
  decoder, because the bit-operation rows decode their effective address
  directly instead of through that escape. The same operand, legal in one form
  and not the other, ten bits apart.

### Method, and why the extraction was trusted

The category sentence and the mode tables were read with `pdftotext -layout`
rather than as page images, against `CLAUDE.md`'s rule -- **after** validating
the method on `TST`'s page, which had been read as an image first and which the
extraction reproduced exactly, footnotes included. The rule exists because OCR
mangles numeric tables; demonstrating fidelity on the very page in question is
what made the cheaper route legitimate here, and it is recorded so the next
person does not take the shortcut without the check.

### What the sweep named on the way out

`BTST Dn,#<data>` is **legal and unimplemented** -- the category check now
allows it and the executor reports `UNIMPLEMENTED`, which is honest. Its test
asserts the form is *not refused* rather than that it executes, so closing the
gap will not falsify it.

Family 5 -- `ADDQ`, `SUBQ`, `Scc` -- still enforces no categories, 240 words.
With `MOVEP`, both are open plan items. 1772 words remain `UNIMPLEMENTED`, from
2621 before C95.

## C97 -- the last two groups, and a legal instruction decoded as illegal

**Class: defect in this core, twice. Closes C96's tail.**

The quick group (family 5) and the ALU group (families 8, 9, B, C, D) were the
last two enforcing no effective-address categories. A further **1040 words**
stopped executing instructions the hardware refuses.

Two rule shapes that had not appeared in C96:

* **`ADDQ` and `SUBQ` are "alterable", not "data alterable".** One word of
  difference from their neighbours, and it is what lets them reach an address
  register -- refusing `ADDQ #1,A0` would break code that is everywhere. They
  carry the same size restriction `TST` does, and the *Description* states it
  more plainly than the footnote: "Word and long operations are also allowed on
  the address registers". Third instruction now where a **size** rule decides
  legality for one addressing mode, and no category expresses any of them.
* **The six ALU instructions state their category per direction.** "a. If the
  location specified is a source operand ... b. If the location specified is a
  destination operand, only memory alterable addressing modes can be used" --
  and the source half differs: `ADD`/`SUB` say "all addressing modes",
  `AND`/`OR` say "only data", because an address register holds an address and
  ANDing one is not an operation the part offers.

### `Scc` has no check, and that is the finding

Its page says data alterable, and **no encoding can violate it**: `DBcc` takes
mode 001, `TRAPcc` takes mode 111 registers 010, 011 and 100, and between them
that is every non-data-alterable mode in the group. All 800 words reaching the
executor already satisfy the rule -- *enumerated*, not argued, because that is
the difference between knowing and assuming. The check I first wrote was
deleted: it would have been a second copy that never runs, which is what C96
said about the row `CMPI`'s decoder owns.

### `EOR Dn,Dn` was illegal here, and is a common instruction

Found while checking that the ALU change had not refused anything legal -- a
fifteen-case table of forms that must still run, one of which did not.

Four of the five memory-direction families require a *memory* alterable
destination, and that is precisely what leaves the register-destination hole for
`SBCD`, `SUBX`, `ADDX` and `ABCD`. `EOR`'s destination is **data** alterable, so
its mode-000 encoding is an ordinary instruction rather than a hole to be
filled. The decoder treated all five alike.

`arith_suite` had a test asserting exactly the wrong verdict, and its comment
gave the reasoning that produced it: "mode 000 in family 1011 is not an
instruction, because CMP has no memory-destination form to fall back on". The
fallback is not `CMP`. It is `EOR`, and the category difference on its page is
the whole reason the hole is not there.

**This is the second time in two campaigns that verifying a change found a
defect the change did not cause** -- C96's `TST.B An`, and now this. Both were
reached by writing down what must still work and running it, rather than by
checking that the new refusals were correct.

1468 words remain `UNIMPLEMENTED`, from 2621 before C95. The largest named
remainder is the bit-field group -- 488 words in family E -- then the
coprocessor gaps, `MOVEP` and `BTST Dn,#<data>`. None is a category question,
and all are open plan items.

## C98 -- MOVEP and BTST Dn,#<data>, the two the sweep named as absent

**Class: missing instructions, implemented.**

C95's opcode sweep separated words this core *misclassified* from words it
genuinely could not execute. The category campaigns (C95-C97) closed the first
kind entirely. These are the second kind, and there were only two.

### `MOVEP`

`[PRM]` p. 4-131: "Moves data between a data register and alternate bytes within
the address space starting at the location specified and incrementing by two.
The high-order byte of the data register is transferred first, and the low-order
byte is transferred last."

It exists for 8-bit peripherals on a 16-bit bus, whose registers appear on one
half of the data bus and so occupy every *other* byte address. The manual is
unusually candid that it outlived its purpose -- "although supported by the
MC68020, MC68030, and MC68040, this instruction is not useful for those
processors with an external 32-bit bus" -- but supported is supported, and a
driver written for the earlier part still runs on this one.

Its addressing mode is fixed rather than decoded ("the address register indirect
plus 16-bit displacement addressing mode"), so the displacement is read directly
and no effective address is gathered.

**Two details a plausible implementation gets wrong, neither of which faults:**

* The **word form replaces bits 15-0 and leaves 31-16 alone**. Assembling the
  two bytes into a long and storing it would silently clear the register's upper
  half -- invisible until something depended on what was there.
* **"Condition Codes: Not affected"**, all of them. That is unusual enough among
  the moves that setting `Z` would look right to anyone reading the code.

### `BTST Dn,#<data>`

The single bit operation whose operand can be an immediate: the dynamic form's
table lists `#<data>` at mode 111 register 100 where the static form's dashes it
(C96), and none of the three that *write* can reach one at all. It is handled
ahead of the address path, because there is no address to gather -- an immediate
is a value in the instruction stream, not somewhere a pointer points.

### The test that got stronger by failing

`MOVEP`'s test first asserted the skipped odd bytes were zero, and failed: the
step harness fills all RAM with `NOP`, so an untouched odd byte holds `$71`.
Asserting `$71` is the better check and not merely the passing one -- a zero
could equally mean the byte *was* written, with zero. The distinction is the
whole point of a test about *alternate* bytes.

**1204 words remain `UNIMPLEMENTED`**, from 2621 before C95. The bit-field group
-- `BFTST`, `BFEXTU`, `BFCHG` and the rest, 488 words in family E -- is now the
largest single remainder by a wide margin, and is the one open item left.

## C99 -- the eight bit field instructions

**Class: missing instructions, implemented. The last block the sweep named.**

488 words, all of family E's remainder and the largest single block of
unimplemented opcodes left. The decoder had recognised them for a long time --
`ap_m68030_shift_decode` produces `AP_M68030_SHIFT_BITFIELD` with the operation
in bits 11-8 -- and the executor returned `false`. Decode without semantics is
the shape that reports UNIMPLEMENTED honestly, which is why this was visible as
a number rather than as a mystery.

### A field is a span in a bit stream, not a mask on a word

§1.7.2 defines it in one sentence that decides all the arithmetic: "The MSB of
the base byte is bit field offset 0; the LSB of the base byte is bit field
offset 7; and **the LSB of the previous byte in memory is bit field offset -1**."

Three consequences, each of which a word-shaped implementation gets wrong while
looking right on the easy cases:

* **A 32-bit field at a non-zero offset spans five bytes.** Reading a long word
  and shifting gives the right answer for every field that fits inside four, and
  the wrong one here.
* **A register-supplied offset is signed** across the full 32-bit range, so the
  effective address is not a lower bound. The byte arithmetic must *floor*:
  `-8/8` is `-1` and so is `-1/8`, where C truncation gives `0` and puts the
  field one byte too high for every negative offset that is not a multiple of
  eight.
* **A data register wraps and memory does not.** §1.7.1: "the address of the MSB
  is zero ... If the width of the register plus the offset is greater than 32,
  the bit field wraps around within the register." One model cannot serve both
  spaces, and picking either for both is wrong in the other.

### Details that do not fault when missed

The condition codes come from the field **as found**, before modification --
these are "test bit field and ..." instructions, and `BFINS` sets them from the
field it is about to overwrite. `BFEXTS` sign-extends from the *field's* width
rather than from a byte, so a 32-bit field needs its own case because shifting
by 32 is undefined. `BFFFO` returns "the bit offset in the instruction plus the
offset of the first one bit" -- a position in the field's own space, not a bit
number, and the field offset plus the width when no bit is set.

### The tests were computed, not observed

Every expectation comes from the byte pattern `12 34 56 78 9A` worked out by
hand as a bit stream, not from running the implementation and recording what it
said. The four writing forms produce **four different results over the same
field** -- `$1FF4`, `$1004`, `$1DC4`, `$1A54` -- so a write that touched the
wrong bits could not pass more than one of them. The register-wrap case is `$81`
where clamping instead of wrapping gives `$80`.

One case did fail on the first run, and it was the test that was wrong: `BFSET`
is `$EE` and I had written `$EF`, which is `BFINS`, and with the source register
zero it cleared the field instead. The value returned was consistent with the
instruction actually executed.

**716 words remain `UNIMPLEMENTED`**, from 2621 before C95. What is left is the
coprocessor and MMU corners of family F, and `CAS`/`CAS2` -- which decline for a
stated reason rather than for want of work: their read and write are
indivisible, and honouring that needs the bus to assert `RMC`.

## C100 -- the last three category holes, found by naming the remainder

**Class: defects in this core, three. Ends the category campaign.**

716 words still reported `UNIMPLEMENTED` and I had been describing them as "the
coprocessor corners and `CAS`" -- an impression, not a measurement. A sweep that
reports each word's *decoded kind* turned it into five named groups in one run:

    immediate kind 6 (MOVES)   180 words
    misc kind 1/2 (CHK)        176 words
    misc kind 10/11 (MOVEM)     80 words
    control kind 15/16 (JSR/JMP) 40 words
    quick kind 4 (TRAPcc)       48 words
    coproc                     192 words

Three of those were categories, and each had failed differently:

* **`CHK` had the check and no verdict.** It tested `ap_m68030_ea_is_data` and
  returned `false` without setting the refusal, so a bound in an address
  register reported this core's gap instead of the machine's illegal
  instruction. The rule was right and the *classification* was missing -- a
  half-conversion left behind by C95.
* **`JMP`/`JSR` had no check, and resolved first.** Resolving an effective
  address applies the increment and decrement side effects, so `JMP (A0)+` moved
  `A0` and *then* reported a gap. `LEA`'s executor carries that exact reasoning
  in a comment -- "a refusal that happened afterwards would already have moved
  the register" -- and the jump did not follow it.
* **`MOVEM` checked the pairing and never the category.** It verified that
  predecrement goes with register-to-memory and postincrement with
  memory-to-register, which is the *interesting* half of the rule, and skipped
  the ordinary half entirely.

`MOVEM`'s two directions differ in more than the increment mode:
register-to-memory is "control alterable ... or the predecrement",
memory-to-register is "control ... or the postincrement". So
`MOVEM.W (d16,PC),D0` is legal and `MOVEM.W D0,(d16,PC)` is not -- the same
addressing mode, legal reading and illegal writing, which is what the test pins.

### Two test cases were wrong before the code was

`MOVEM.W D0,D0` looked like the obvious illegal case and it executed. `$4880` is
**`EXT.W D0`**: mode 000 is `EXT` and 001 is `EXTB`, so `MOVEM` cannot encode a
data-register operand at all and the cases that reach its check are the
immediate and PC-relative modes. That is the third time this campaign that a
hand-picked "obviously illegal" encoding turned out to be a different
instruction -- after `Scc A0` (`DBcc`) and `$15C0` (`MOVE` to `(d16,PC)`, not
`(d16,A2)`). Reading an encoding off a hex literal by eye has now been wrong
more often than the code under test.

**420 words remain, all named**: `MOVES` (180), `TRAPcc`'s operand forms (48),
and family F's coprocessor and MMU corners (192). None is a category question.
The category campaign that began at C95 is finished: every instruction group in
the 68030 now enforces the effective-address rules its own page states.

## C101 -- MOVES, and a privileged instruction that was not privileged

**Class: missing instruction implemented, plus a defect its test exposed.**

`MOVES` is the one instruction that reaches an **arbitrary** address space.
Every other access this core makes carries a function code fixed by what it is;
`MOVES` carries whatever the program last wrote into `SFC` or `DFC`. `[PRM]`
p. 6-24: it moves an operand "to a location within the address space specified
by the destination function code (DFC) register", or from one "within the
address space specified by the source function code (SFC) register". An
operating system uses it to read a *user* program's memory while running in
supervisor state, which no ordinary `MOVE` can do because a `MOVE`'s function
code follows the processor's own privilege.

Modelling it as an ordinary move would work perfectly on this machine today and
be wrong the moment anything distinguishes the spaces -- which is exactly what
the MMU's function-code fields and the transparent translation registers'
`FC BASE`/`FC MASK` exist to do.

### The test had to watch the function code, not a value

Flat RAM answers every function code alike, so a check on *what was read* cannot
tell a correct `MOVES` from an ordinary move. The harness now records the code
each fill carried, and the assertion is that a supervisor-state `MOVES` with
`SFC = user data` reads **as user data** while the `MOVE` beside it, through the
identical address, reads as supervisor data.

Two attempts preceded that. The first tried to make the difference *behavioural*
by seeding two ATC entries for one logical page under different function codes
-- which is the right idea and will be the right test later, but the lookup
takes its page size from the `TC` and the entry did not match one I had left
zeroed. Recorded because the failure looked like a `MOVES` defect and was a test
that had not finished setting up its machine.

### The defect the test found

`ap_m68030_immediate_privileged` had **no caller anywhere in `src`**. The three
`to SR` forms were checked by a condition written out again inside
`execute_immediate_to_status`, and `MOVES` -- which the helper also names, with
the comment "MOVES reaches an arbitrary address space through SFC/DFC" -- was
checked **nowhere**. A user program could have read supervisor memory with it.

The helper is now asked once, at the top of `execute_immediate`. That is the
fourth declared-and-unconsulted function this campaign to mark a real gap, after
C90's timing rule, C91's FPU trap predicates and C93's reset sequence -- and the
first found by writing a test from the instruction's own page ("If Supervisor
State ... Else TRAP") rather than by sweeping.

**240 words remain `UNIMPLEMENTED`**: `TRAPcc`'s operand forms (48) and family
F's coprocessor and MMU corners (192).

## C102 -- TRAPcc, reserved coprocessor types, and an MMU pass reverted

**Class: two missing instructions implemented; one attempted change withdrawn.**

### `TRAPcc`, all three forms

Not "the operand forms" as the plan had it -- **all 48 words**, the no-operand
form included. `[PRM]` p. 4-189: the immediate "should be placed in the next
word(s) following the operation word and is available to the trap handler", and
the instruction never uses it. That is what makes dropping it easy and
invisible: skipping the words only when the condition is false runs the operand
as an instruction after every *taken* trap, and skipping them only when true
does so after every untaken one. Both polarities are tested across all three
forms, and the taken case checks the stacked program counter -- which is how a
handler reaches the data at all.

### Reserved coprocessor instruction types

128 words, cpID 1 type 110/111. §10.2: "The M68000 coprocessor interface
supports **four categories** of coprocessor instructions: general, conditional,
context save, and context restore." Types 110 and 111 are none of them.

Recorded as a **reading**, not a transcription: the manual defines what the four
categories do and is silent on what a *fitted* coprocessor does with a fifth.
Vector 11 is inferred from the two neighbouring cases -- an absent coprocessor
takes the line 1111 emulator exception, and Table 4-13's footnote 2 has the FPCP
ask for the same trap on an undefined *command* word.

Before this they fell through to the general path, which fetches no command word
for a non-general type -- so the FPU was asked to execute **command zero** and
answered about an instruction the program had not written.

### The MMU pass, attempted and withdrawn

64 words remain, all cpID 0. Table 3-10 gives the 68030 five MMU operation codes
(`PMOVE` in three forms, `PFLUSH`/`PLOAD`, `PTEST`), so codes 5-7 are undefined
-- and p. 8-10 makes "undefined patterns in subsequent words" the F-line
exception rather than this core's gap. The one unambiguous arm was implemented
and **reverted**, for two reasons worth recording rather than pushing through:

1. It broke three tests that use extension `$A000` -- operation code **5** -- as
   their example of "an instruction the hardware executes that we have not
   implemented". The manual contradicts that premise, so those tests need a new
   subject, and choosing one requires knowing which patterns *are* defined.
2. Looking for a replacement subject, `CAS.L` was observed **executing**, where
   `PROJECT_STATUS.md` records `CAS`/`CAS2` as declining for want of `RMC`. One
   of the two is wrong, and a sweep whose classification rests on an unsettled
   fact is not worth trusting.

Landing half a classification would have been the plausible-looking wrong answer
this core spends most of its care avoiding -- it would have converted an unknown
number of genuine gaps into a machine that *looks* correct. So the item is named
with its scope instead: about two dozen `return false` sites across three MMU
executors, several already carrying comments that say which kind they are (one
reads "a register this part does not have: F-line, not a no-op"), and the two
questions above to settle first.

**64 words remain `UNIMPLEMENTED`**, from 2621 when the sweep began.

## C103 -- the last 64, and a stale comment that had been believed twice

**Class: defects in this core, five; plus a documentation correction.**
**Ends the opcode campaign: 2621 unimplemented words to zero.**

### `CAS` executes, and the comment said it did not

Settled first, because the previous entry had refused to classify anything while
it stood. `execute_bounds` dispatches to `execute_cas` and `execute_cas2`, and
two lines above that dispatch sat:

> "CMP2 and CHK2 execute. CAS and CAS2 still decline: their read and write are
> indivisible ... `execute_bounds` refuses them rather than running them without
> it."

Both had landed long before -- `55c85e0` "CAS executes, with RMC asserted across
the pair" and `f6ad4a8` "CAS2 executes: the reason it was declined was a
misreading". The comment outlived them, and **I repeated it into
`PROJECT_STATUS.md` two campaigns ago** by reading it instead of the function it
described. A stale comment beside working code is worse than none: it is the
version a reader trusts, and it was trusted twice.

### The MMU rule is transcribed, not inferred

p. 9-51 lists what a 68030 lacks against the 68020/68851 pair -- `PVALID`,
`PFLUSHR`, `PFLUSHS`, `PBcc`, `PDBcc`, `PScc`, `PTRAPcc`, `PSAVE`, `PRESTORE`,
and "**PMOVE for unsupported registers** (CAL, VAL, SCC, BAD, BACx, DRP, and
AC)" -- and says they "must be avoided or emulated in the exception routine for
**F-line unimplemented instructions**".

That sentence classifies every remaining site. Five were reclassified across
`execute_pmove`, `execute_pflush_or_pload` and `execute_ptest`.

**Two of the five already had the verdict in a comment and returned the other
one.** `execute_pmove`'s read "a register this part does not have: F-line, not a
no-op"; `execute_ptest`'s quoted the manual outright -- "The instruction takes an
F-line exception when the level field is 0 and the A field is not 0". Both
returned `UNIMPLEMENTED`. Same shape as `CHK`'s missing verdict in C100, and the
fourth time this campaign that a rule was written down correctly beside code
that did not act on it.

### One test could not be kept, and its principle got stronger

Three `step_suite` tests failed. `test_a_level_zero_ptest_cannot_ask_for_a_descriptor_address`
had asserted `UNIMPLEMENTED` while quoting the F-line sentence **in its own
comment** -- it contradicted itself and the contradiction had never mattered.
`test_a_fault_does_not_leak_into_the_following_instruction` was about the fault
flag and merely used an unimplemented instruction as its vehicle; it now asserts
the property (no `FAULT`, flag clear) rather than the vehicle's status.

`test_an_unimplemented_mmu_instruction_is_not_dressed_up_as_f_line` could not be
repaired: its subject, `F000` with a zero extension word, names a register the
part does not have and was **never** this core's gap. Its principle is now
asserted over the whole instruction set instead -- `machine_suite` steps all
65536 opcodes in about two seconds and fails, naming the word, if any reports
`UNIMPLEMENTED`. One example became every one of them.

### Where the number went

    2621  before the effective-address category work (C95)
    1830  after MOVE, the misc group and the MMU categories
    1204  after the single-operand, immediate, shift, quick and ALU groups
     716  after the bit field instructions
     420  after MOVES
     240  after TRAPcc and the reserved coprocessor types
       0  after the MMU classification

Phase 2 and Phase 2b have no open items.

## C104 -- the verification counts had all gone stale, and nothing checked them

**Class: documentation defect, twenty-three at once. Found by auditing the
completion claim rather than the code.**

Phase 2 and Phase 2b reached zero open items, and the natural next question is
whether "complete" means anything -- the evidence for it is the Verification
column of `PROJECT_STATUS.md`'s subsystem table, which names its proof as, for
example, "`step_suite`, 175 tests".

`step_suite` registers **270**. Twenty-three of the table's counts were wrong,
some by a factor of two: `ea_timing_suite` said 8 against 26, `mc68681_suite` 15
against 34, `graphics_suite` 6 against 14.

**Every one understated**, which is why it survived. Suites only grow, so the
drift is always in the harmless direction -- the document claims *less* evidence
than exists. Nothing ever failed, nobody was misled into trusting an unverified
subsystem, and ninety numbers quietly stopped being true. A claim that is wrong
in the safe direction still teaches its reader that the numbers are decorative.

## C109 -- the boot PROM's console state machine, mapped from its own code

**Class: our core, one defect; and the firmware's structure, recovered.**

The SIO item's verification is "console byte stream identical to the oracle's".
The oracle's half is captured byte-exact in `docs/references/MD.md`; ours does
not exist, and the plan has said "the PROM never transmits" without saying what
it does instead. It does a great deal, and all of it is now legible.

**First, a defect that made every previous firmware run meaningless as a
measurement.** The headless boot path stepped `ap_m68030_step` directly -- the
processor, with no interrupt sampling, no bus tick and no device advanced -- so
the machine's own `elapsed` line read `0 base units` on every run this project
has ever taken. Fixed by stepping `ap_machine_run` with a limit of one; the same
run now reports 3,370,481,136 units over 1.5 million instructions. It does not
move the boot, but everything below is measured on a machine with its clock
running.

**The console-selection poll**, at `00078E`-`0007AE`, is three `BTST #0` --
RxRDY -- on three status registers, branching when any is set:

| tested | port | on RxRDY |
| --- | --- | --- |
| `($0002,A0)` | serial 1 channel A, the keyboard | branch to `00080E` |
| `($0012,A0)` | serial 1 channel B | branch to `0007E6` |
| `($0102,A0)` | serial 2 channel A | fall through to `0007B0` |

A0 is `010400`. So the firmware waits for a byte on any of the three and takes a
different path for each -- it is choosing a console, which is why an idle
machine sits here forever.

**The status-post routine at `00251A`** takes its argument *inline*, reading a
word from the return address and stepping over it, and ends `MOVE.B D0,(A1)`
with A1 from `($015A,A6)`. Watching that pointer gives `00010100` -- the CPU
control register -- and the byte is written **complemented**. This is the
diagnostic code display, not console output. Codes observed in sequence: `03`,
`04`, `07`, `08`, `0A`.

**`0A` is "a byte arrived on the console channel".** `0007E6` posts it and the
next instruction, `1228 0016`, reads `($0016,A0)` -- serial 1 channel B's
receive buffer. So the code and the read are one step, and a machine posting
`0A` repeatedly is one receiving console bytes repeatedly.

**The character dispatcher** compares the received byte against `FF`, `FE`,
`C7`, `72` -- `'r'` -- `C0`, and `0D`. Reaching those comparisons means the
autobaud has completed and bytes are decoding: with a keyboard press first,
serial 1 channel B's clock select is written twice rather than 27,365 times.

**And the keyboard press is what starts it**, which `MD.md` recorded and this
confirms from the other side: its capture recipe is "a key press on the Apollo
keyboard to prompt the firmware's autobaud" and then "one carriage return every
0.4 s". Sending the carriage returns without the key -- which is what every
attempt here had done -- leaves the firmware cycling rates against a channel it
has not been told to listen to.

**And then the reason it never transmits: with a screen fitted, it does not
want to.** Adding `--screen 19i` to the same run turns 0 display writes into
**263,376**, and the firmware stops sitting in the poll. It is writing its
console output to the frame buffer. The serial transmit holding registers stay
untouched because the machine has a console already and it is not the serial
line.

That is measured across three configurations of one otherwise identical run:

| display | display writes | where it ends |
| --- | --- | --- |
| none (default) | 0 | the console-selection poll, `0007AE` |
| `19i` mono | 263,376 | the status poster, `002536` |
| `c4p` colour | 10 | `0045E6` |

So "the PROM never transmits" was never a defect in the serial path. It is a
machine with no console being asked to choose one, and the byte stream this item
wants to compare against the oracle exists -- it is going to a device this phase
does not render. The colour controller taking 10 writes where the mono takes a
quarter of a million is its own thread: the firmware probes both and only one
answers the way it expects.

**The MD sign-on is in the PROM at `0008F4`**, and so is the branch that prints
it: `0008D0` is `CMP.B #$0D,D1` and, on a match, `0008D8` loads the string
address and calls the print routine. The bytes read
`0D 0A 4D 44 37 43 20 52 45 56 20 38 2E 30 30 2C 20 31 39 38 39 2F 30 38 2F 31 36 2E`
-- "\r\nMD7C REV 8.00, 1989/08/16." -- against `MD.md`'s captured
`0D 0A 4D 44 37 0D 0A`. So the two sides share a prefix and the comparison this
item wants is a diff of two known strings rather than a search for one.

The comparison at `0008D0` is reached only after `0008C8` calls `0021FA` -- the
routine immediately after the scan-code table -- and masks the result with
`AND.W #$7F`. So the byte the console dispatcher matches is a **scan code
translated to ASCII**, not a character off the wire.

**And `--boot-key` had never delivered a byte.** Three conditions had to hold
and the frontend waited on one:

- `MR1` resets to a **five-bit** link, so a make code loses its top three bits
  and a release code -- the make code with bit 7 set -- cannot arrive at all.
  Both became `00`.
- A **disabled receiver drops** what arrives. Measured directly: the byte went
  in at step 1223 and the FIFO stayed empty.
- The receiver must also be free, which is the only thing it had checked.

With all three, the firmware reads serial 1 channel A's receive buffer for the
first time in this project and reaches `00220E`, the scan-code search. Every
`--boot-key` run ever taken here delivered nothing and looked exactly like a
machine ignoring its keyboard.

A stale claim goes with it: the plan recorded that "the firmware has not enabled
channel A's receiver at that point". It has. Measured after 400,000
instructions, **all four receivers are enabled**, channel A carries eight bits,
and its clock select is `66` where the other three are `77` and `BB`.

**The table is now proved rather than read**, and from the addressing modes that
index it rather than from the bytes. The search at `0021FA` compares with
`CMP.B (-$38,PC,D0.W),D1` from an extension word at `00220A` -- `0021D2` -- and
answers with `MOVE.B (-$30,PC,D0.W),D1` from `002216` -- `0021E6`. Twenty bytes
apart, `MOVE.W #$0013,D0` counting nineteen down to zero: **two parallel
twenty-entry tables**, scan codes and their characters at the same index.

    CB DB -> 0D    FB -> 1B    C8 D8 F8 -> 5C    C9 D9 -> 7C    F9 -> 7F
    5B -> 7B       5D -> 7D    7B -> 5B          7D -> 5D
    CA DA FA -> 09 CC -> 2F    DC FC -> 3F       DE -> 08

C46 read the table as 41 bytes with "triples on a fixed spacing interleaved with
ASCII runs". It is 40, and the triples are three tables' worth of *release*
codes sitting above four make codes in the first half; the "interleaving" was the
second table beginning at index 20. Recorded in `device/ap_kbd.h`, since it is
the only place a caller can learn which index to press to send a character.

**Where the trail stops.** `0008D0`'s `CMP.B #$0D,D1` is reached and does not
match, so the byte arriving there is not a translated `CB`. The translate's own
guard is not the reason -- `BTST #1,($01C7,A6)` reads `21` during a boot, bit 1
clear, translation running. And the dispatcher chain that handles a keyboard byte
ends at `0008A4` with `BNE` back to the poll, so `CB` cannot reach `0008C8` by
that path at all: something else reaches it. Which path, and with what byte, is
the next thing to settle, and it is firmware disassembly rather than emulator
work.

## C110 -- the dispatcher is the autobaud, and a rate mismatch must corrupt

**Class: our core, one defect of substance and two of timing; and the firmware's
console negotiation, understood.**

C109 read `0008xx`'s chain of `CMP.B` against `FF`, `FE`, `C7`, `72`, `C0` as a
*command* dispatcher and `72` as `'r'`. It is not. Disassembling what each arm
does settles it:

| received | writes to the port's `CSR` | meaning |
| --- | --- | --- |
| `FF` | `BB` -- 9600 | the sender is at 9600 |
| `FE` | `99` -- 4800 | the sender is at 4800 |
| `C7` | `88` -- 2400 | the sender is at 2400 |
| `72` | stores `66` -- 1200 | candidate, confirmed by the next byte |
| `C0` | stores `44` -- 300 | candidate, confirmed by the next byte |

Those five values are **the shapes a carriage return takes at five wrong rates**,
and each arm reprograms the port to the rate that shape implies. `72` was never
`'r'`.

**So a rate mismatch has to corrupt the byte, and ours did not.** The model set
`SR[6]` and delivered the character intact -- a note saying something went wrong
rather than the thing that went wrong. A UART finds the start edge and samples at
the bit centres its *own* clock predicts, so at the wrong rate it reads the
sender's waveform at the wrong instants and returns a different value. With an
intact `0D` the firmware matches none of the five arms, learns nothing, and loops
forever, which is exactly what this core did.

`ap_mc68681_resample` models it: start bit, data least significant first, stop
bit, each a sender bit-time wide; the receiver samples bit `i` at `(i + 1.5)`
receiver bit-times from the start edge; past the sender's stop bit is the idle
line, high. Equal rates return the byte unchanged, so every correctly configured
link is exactly as it was.

**Two of the three fixed arms match the model exactly.** A carriage return sent
at 9600 into a port receiving at 1050 resamples to `FF`, and at 4800 to `FE` --
and the firmware's answer to each is to switch to that very rate. The 2400 case
gives `F9` where the firmware expects `C7`, so the mechanism is right and a
detail is not: a receiver that resynchronises on edges, or a different assumed
character length, would move it. Recorded as a disagreement rather than tuned
away.

**Two smaller defects, both the same shape as `--boot-key`'s.** Scripted input
was sent as soon as the FIFO was free -- before `MR1` leaves its five-bit reset
state and before the receiver is enabled -- so the byte arrived truncated or was
dropped. That matters more here than it looks: the autobaud identifies a rate
*from what the wrong rate did to the character*, so a byte truncated first
arrives as a shape it has no case for and the negotiation cannot begin.

And `rate_matches` compared the receiver's upper nibble against the **sender's
upper nibble**, judging a sender by the rate it was listening on rather than the
rate it was transmitting at. Its own comment had said the right rule since it was
written. Now compared as *rates* rather than codes, so the four codes that are
not a fixed rate -- the timer and the two external clocks -- match rather than
inventing a disagreement this core cannot know about.

**Where it stands, measured rather than guessed.** Instrumenting the port at the
instant each scripted byte lands gives the sequence:

| step | channel B `CSR` | what happened |
| --- | --- | --- |
| 1234 | `BB` (9600), `MR1` 8-bit, receiver off | the firmware's first configuration |
| 1235 | `BB` | first byte sent -- **rates matched**, so it arrived as an intact `0D` and no arm has a case for that |
| 49763 | `77` (1050), receiver on | the firmware moves the port |
| 49790 | `77` | second byte sent at 9600 -- a genuine mismatch |

So the firmware **sweeps** the port's rate, and a scripted sender at one fixed
rate will sometimes coincide with it. A coincidence wastes the byte: a correctly
framed `0D` matches none of the five arms, which is the firmware relying on the
rate being wrong.

After the second byte the port stays at `77` and the FIFO stays at one byte with
`RxRDY` and the framing flag both set -- `SR` reads `4D` -- and the firmware
**never reads it**, sitting in the dispatcher at `000886`. So the byte it is
dispatching is not the one we delivered, and something else is feeding the chain
while channel B's character waits. Which channel that is, and why the poll passes
over a set `RxRDY` on channel B, is the next thing to establish and it is
firmware disassembly rather than emulator work.

### Checked from now on

`tools/check_doc_counts.py` compares every "`X_suite`, N tests" claim in the
table against the `RUN_TEST` registrations in `tests/X_suite.c`, and is a CTest
entry. Adding a test without touching the table now reddens the tree and names
the row.

It counts `RUN_TEST` in the source rather than running the binaries: Unity
reports exactly that number -- verified against four suites before relying on it
-- so the check needs no build and takes a second. The gap is a test defined and
never registered, which is invisible to this *and* to the suite itself, and
which `-Wunused-function` already catches.

### Why this is the right shape of finding to end on

The whole campaign from C88 has been one failure repeated: a claim true when
written, never re-run, and believed afterwards. C88's own correction was
unverified; C90's timing rule described a retired model; C94's audit had never
been run in the direction it asserted; C103 found a comment that had outlived
the code by two commits and had been copied into this very document.

Counts are the cheapest instance of that failure and the easiest to automate
away. The rest still need someone to go and look -- which is the argument for
`--program all`, for the 65536-opcode sweep, and now for this.

## C105 -- the rest of the documents' claims, and a manual left in the tree

**Class: verification tooling, plus a repository-hygiene defect I created.**

C104 automated one of the three things the living documents assert about the
tree. This does the other two and settles them: **78 source paths and 97 `ap_*`
symbols**, all of which checked out. Worth a check anyway -- they are clean
*now*, and the counts were clean once too.

`tools/check_docs.py` (renamed from `check_doc_counts.py`, since the old name
had stopped describing it) verifies all three and is the `doc_claims` CTest
entry. **All three were proved to fail before being trusted**: a count changed
to 999, a path changed to one that does not exist, and a symbol changed to one
that is nowhere in the tree -- caught, named, and the document restored each
time. A checker that has only ever passed is not evidence of anything.

One allowance: a path named solely by an *unticked* plan item may not exist.
Those items are often "write this document", and requiring the artefact first
would stop the plan describing its own future. `docs/references/TEST_SHELF.md`
is the live example.

### A copyrighted manual was sitting in the working tree, and I put it there

`git status` showed `docs/references/motorola/` untracked. Inside was a
plain-text extraction of the M68000 Programmer's Reference Manual -- **created
by me**, earlier in this same campaign, by a `pdftotext` invocation that omitted
the `-` for stdout. Without it the tool writes `file.txt` *beside the input*
rather than to the pipe. The command appeared to produce no output, I added the
`-` and moved on, and the file stayed.

`.gitignore` covered `docs/references/**/*.pdf` and `**/*.txt.gz` and **not
`*.txt`** -- so the one extension a slip actually produces was the one not
covered. This repository is public and the manuals are vendor copyright; only
nobody having run `git add -A` stood between that file and a redistribution.

Deleted, and the rule extended. The fix was then verified the way the checker
was: the slip was **reproduced** -- the same malformed `pdftotext` re-run, the
same file created -- and `git status` confirmed it is now ignored.

`CLAUDE.md` says temporary instrumentation is always reverted before commit.
This was not instrumentation, which is exactly why it survived a dozen commits:
it was a by-product nobody was looking for, in a directory nobody stages.

## C106 -- the citations hold, and they cannot be checked the way the counts are

**Class: audit result, negative; and a bound on what tooling can do.**

C104 and C105 automated the two claim-classes in the living documents that are
mechanical: test counts, and cited paths and symbols. The third class is the one
the whole project rests on -- the manual citations, `§8.1.5`, `p. 8-10`,
`Table 3-10` -- and this is why it is *not* automated.

**Sampled and sound.** Three page citations made during this campaign were
checked by locating the PDF page whose printed footer carries the cited number
and confirming the passage is on it: `[030]` p. 8-10 (A-line and F-line
emulator vectors), p. 8-5 (the reset exception's ten steps), and p. 9-51 (the
68851 instructions a 68030 lacks). All three correct.

### The third one appeared to fail, and that is the finding

The checker reported `PVALID` absent from the page it cites. It is there. The
manual's text layer renders it **`PVALlD`** -- capital I as lowercase L -- so a
literal search misses it.

That is exactly the damage `CLAUDE.md` mandates page images for, met in the
wild:

> "read the page image, not a text extraction: OCR mangles precisely what timing
> and register tables are made of, and `4(1/1/0)` arriving as `4(1/010)` reads
> as plausible data."

A sweep of the same extraction finds `PHAlT`, `PDlNTS` and `PVALlD` among the
instruction and signal names. Rare -- three in a 600-page manual -- and that
rarity is the problem: a citation checker would be right often enough to be
believed and would then report a correct citation as broken, or match a mangled
name against a mangled document and agree with itself.

**So the citations stay checked by reading, and the tooling stops here.** The
line is not laziness: counts and symbols are *facts about this repository*,
which a script can see in full; a citation is a claim about a scanned book,
where the script's view of the book is itself the unreliable thing. Recording
the boundary matters more than the three passing samples, because the obvious
next step from C105 is to automate this too, and it would produce false alarms
that train their reader to ignore the check.

Phase 2 and Phase 2b have no open items. The evidence for that is machine-
checked where the evidence is machine-checkable, and read where it is not.

## C107 -- the synchroniser: the oracle could never have answered, and the spec did

**Class: `PROVISIONAL` narrowed from a document, plus a closing route that did
not exist.**

The synchroniser is modelled at two clocks because `[030]` §7.7.4 publishes a
bound and not a value -- "all asynchronous inputs to the MC68030 are internally
synchronized in a maximum of two cycles of the processor clock". The recorded
cost to close was "measure grant latency against the oracle across many request
phases; small once a second master exists".

**That measurement was never possible.** MAME's 68000 family models no bus
arbitration whatever: no `BR`, `BG` or `BGACK` anywhere in
`ext/mame/src/devices/cpu/m68000/`, and nothing in the Apollo driver either. A
second master in that emulator would have had no grant to time. The route had
been written down as small and cheap, and it was infinite.

### What did answer it

`CLAUDE.md`'s order is reference → sibling manuals → **web** → oracle, and the
web step is the one that paid. The sibling manuals corroborate without adding:
the 68020's §5.2.7.4 gives the identical "maximum of two cycles" and the same
`R`/`A` synchronised inputs feeding the same state machine.

The user's manual defers to a separate document for timing -- "The timing
parameters referred to are described in MC68030EC/D, MC68030 Electrical
Specifications" -- which was not on disk. Fetched, and it states the quantity
directly:

> **35. BR Asserted to BG Asserted (RMC Not Asserted): 1.5 min, 3.5 max Clks**

Identical at 20, 25, 33.33, 40 and 50 MHz; parameter 37 gives BGACK-asserted to
BG-negated the same window. Read from the page image, since it is a numeric
table.

### What that buys, and what it does not

A two-clock spread between min and max is exactly one synchroniser's worth of
uncertainty -- the specification agreeing that this is a genuine range rather
than a figure someone declined to print. Both plausible models sit inside it: a
two-clock synchroniser plus an edge gives three clocks, a one-clock synchroniser
gives two, and `1.5 <= 2 < 3 <= 3.5`.

So the synchroniser itself is still not pinned, and no document will pin it. But
the thing that matters is now checked: `arb_suite` asserts the grant latency
stays inside the manufacturer's published envelope, which a change to the
synchroniser could leave. The figure moved from "the published maximum, unbacked"
to "inside the published measurement, asserted".

**The remaining uncertainty is sub-clock phase**, which nothing clock-stepped
represents -- so this is closable only from hardware, or by accepting the
envelope as the answer. That is now what the row says, instead of naming an
emulator that cannot be asked.

## C108 -- probes can run on a board, which is what every device verification needed

**Class: capability, and a divergence class it surfaced within minutes.**

Every Phase 3 device item carries a verification line of the same shape --
"probe-driven interrupt ordering vs oracle", "transfer probes", "self-timing
probes", "console byte stream" -- and not one of them had a route. The probe
harness runs "a 68030 on flat RAM and nothing else", which is what made
side-loading cheap in Phase 1 and what made every one of those lines
unreachable: a device register is *unmapped* on flat RAM, so our side faults
exactly where the oracle's `dn3500` answers.

`--probe-file` now takes `board 1` and builds a whole core board. No boot PROM:
`ap_board_init` does not need one, and a probe is side-loaded precisely so that
no firmware runs.

Two details that were wrong first and are worth keeping:

* The probe must be written **through the board**, not with `ap_machine_write`.
  That is the operator's view of flat RAM and knows nothing of where a model
  puts its memory, so it refused a load at `01001000` on a machine whose board
  maps RAM there.
* Which is the consequence worth having: a board probe loads at the *model's*
  RAM base, which is where the oracle's loads. Both sides then run the same
  addresses, and the diff stops needing the base offset every existing probe
  carries.

Measured rather than asserted. The same probe, reading a DMA register:

    board 1   3 instructions, STOPPED, 0 bus errors
    flat RAM  50 instructions, EXECUTED, 25 bus errors, never terminated

### The divergence it found immediately

The probe read the 8237A's all-mask register and got zero, where C13 had
recorded MAME returning `0F` and had used that as the placement fingerprint for
the whole controller.

Neither is wrong. `[8237]` Figure 6 marks reads of that register **"Illegal"**,
so the datasheet defines no value; this core returns zero deliberately -- "the
part drives nothing, and a caller reading here has a bug this core should not
paper over" -- and MAME returns `0F`. Our reset mask *is* `0x0F`, so the value
is not the disagreement; the read decode is.

Recorded now, before the first board-backed diff runs, so that an
undefined-behaviour difference is not read as a defect. This is the third
divergence class in the log, after `fpu-sine-x`'s ULP and MAME's absent FPCP
vectors -- and the first that is neither side being wrong.

## C111 -- the first board-backed diffs, and the state asymmetry they run under

**Class: agreement, both rows.** C108 built the road; this is the first traffic
on it. `probe_compare.py` now has a `BOARD_PROGRAMS` set, and a program in it
runs with `board 1` on our side and at the model's RAM base on both.

| probe | what it pins | ours | oracle |
| --- | --- | --- | --- |
| `dma-register` | 8237A byte-pointer flip-flop, both directions | `00003412` | `00003412` |
| `intr-mask` | both 8259As' `ICW1`-`ICW4` then `OCW1` read back | `00005AA5` | `00005AA5` |

Instruction counts (10 and 17), stop reasons and the **program counter** agree
too. The PC is new: both sides load at `01001000`, so for the first time the
printed word lists are byte-identical and the PC is the same quantity on both.
It is compared for board probes and only for them.

### The constraint a board probe lives under

The two machines' devices are **not** in the same state, and cannot be made so.
Our board is at reset with nothing programmed; the oracle's has been booting for
three emulated seconds and has configured its controllers for real work. A probe
that only *read* a register would compare a reset part against a booted one and
report a difference that says nothing about the part.

So a board probe is self-contained: it resets or re-initialises the device and
reads back only what it wrote. The 8237A takes its master clear at register
`$0D`; the 8259As take a full `ICW1`-`ICW4`, which restarts the state machine
whatever the firmware left behind. This is a rule about how to write the probe,
not a fact about the hardware, and it is the thing to remember before adding the
timer and SIO probes behind these two.

The second half of the same asymmetry is the *processor's* interrupt mask, and
it cost nothing only because it was noticed before the first run. Our board can
have nothing to deliver; the oracle's is mid-boot with the firmware's mask in
the SR. A probe that unmasked a controller line would be interrupted on one
machine and not the other -- a harness asymmetry that would have read as a
finding about the priority encoder. Every board probe therefore opens with
`MOVE.W #$2700,SR`.

### What `intr-mask` deliberately does not compare

Not an ordering. Which of two simultaneous requests wins is resolved on each
machine's own sampling schedule and MAME advances its devices on a different one,
so a side-by-side ordering diff compares two quantisations rather than two
priority encoders. That reasoning is the 8259 item's own and is unchanged by
board probes existing; what board probes reach is the **programming model**, and
that is schedule-free.

`--program all` now runs 17 programs: 14 identical, 2 differing as recorded
(C70's ULP, C92's absent FPCP vectors), 1 not applicable to a DN3500.

## C112 -- the poll loop is the raster, and the pixel clock does not divide the base

**Class: ours-wrong, and the fix is a unit change rather than a device.**

A `--screen c8p` boot draws nothing. The counters added with `CR0`'s mode
dispatch turned that from a guess into a measurement, and then into a
diagnosis:

    instructions   register writes   register reads   blit cycles
       400,000               803          175,350             0
     4,000,000               803        1,975,350             0

Every register write happens before the 400,000 mark. After that the firmware
does nothing but *read* the controller -- 1.8 million times in three and a half
million instructions, one read every two. That is a poll loop, not a self-test.

### What it is polling

The only register in the block this core does not model is the **status**
register at offset 0, which returns a constant `FF`. Its bits are not a busy
flag. From the oracle's own definitions:

    0x80 BLANK      0x40 V_BLANK    0x20 H_SYNC (mono) / DONE (colour)
    0x10 R_M_W      0x08 ALT        0x04 V_SYNC (mono) / SYNC (colour)
    0x02 H_CK       0x01 V_DATA (mono) / V_FLAG (4p) / LUT_OK (8p)

Five of those -- `BLANK`, `V_BLANK`, `H_SYNC`, `V_SYNC`, `H_CK` -- are **display
timing**, driven from the raster. The firmware is waiting for a sync or blank
edge. Against a constant `FF` no edge ever arrives, so it waits forever, and it
never reaches the code that would draw. The blank screen and the poll loop are
one fault, not two.

### The figures, from the manual first

`008778-03` Table 11-3, colour monitor performance:

    resolution            1024 x 800 noninterlaced
    horizontal            50.2 kHz +/- 500 Hz
    vertical              47 to 80 Hz
    horizontal blanking   4.713 us maximum
    vertical blanking     828.83 us max (15-inch), 831 us max (19-inch)
    horizontal retrace    3.713 us maximum
    video bandwidth       50 Hz to 70 MHz minimum

The oracle's `set_raw(68000000, 1346, 0, 1024, 841, 0, 800)` agrees inside every
tolerance the manual states: 68 MHz / 1346 is 50.52 kHz, within the +/- 500 Hz;
50520 / 841 is 60.07 Hz, inside 47-80 and matching §1.5.3's "60-Hz,
noninterlaced". Horizontal blanking works out at 4.735 us against a stated
maximum of 4.713 -- 0.5% over, and inside the tolerance already allowed on the
horizontal frequency. Vertical blanking is 811.6 us against 828.83.

So the manual bounds the timing and the oracle supplies the point values inside
those bounds. That is the resolution order working as intended, and it is worth
saying plainly: the manual alone could not have given a raster.

### Why this is not a device item

**68 MHz does not divide `AP_TIME_BASE_HZ`.** 19,800,000,000 / 68,000,000 is
291.18, so `ap_clock_init` would refuse it -- correctly, and by design. Adding
the video clock domain means recomputing the base, which is what `CLAUDE.md`
says a derived constant costs:

    LCM(19,800,000,000, 68,000,000) = 336,600,000,000   (17x)

At that base a pixel is exactly 4950 units, a line 6,662,700 and a frame
5,603,330,700, so every raster boundary lands on an integer and nothing is
rounded. `uint64` still holds 634 days of emulated time.

The cost is smaller than it sounds and was measured rather than assumed: one
golden line carries the base (`model_table.txt`), `timing.txt` is in *clocks*
and not base units, and `probes.txt` has no time in it at all. Every other user
of the constant derives from it.

Filed as its own plan item, because it changes the project's unit of account and
does not belong inside a display-controller change.

## C113 -- the console speaks, and the rate was the terminal's and not the machine's

**Class: ours-wrong, in the harness rather than the core.** The boot PROM's
console output is byte-identical to the oracle's record on the first run that
got it.

    ours   0D 0A "MD7C REV 8.00, 1989/08/16.17:23:52" 0D 0A 3E
    MD.md  CR LF "MD7C REV 8.00, 1989/08/16.17:23:52" CR LF '>'

Every carriage return after it is echoed and answered with a fresh prompt
(`0D 0D 0A 3E`), so the Mnemonic Debugger is not merely printing a banner, it is
running.

### What was wrong, and it was one number

`--boot-input-rate` defaulted to `77`, on the recorded reasoning that `77` is
"what the DN3500's own firmware configures both ports to at reset, measured off
the oracle -- so a scripted terminal that used anything else would be modelling
a misconfigured cable rather than a console".

The measurement was right and the inference was backwards. The firmware
**autobauds**: the terminal sends at the *terminal's* rate and the PROM works out
which it was. Setting the scripted terminal to the machine's own rate is not
modelling a matched cable, it is removing the thing the negotiation exists to
measure. `77` is 1050 baud, which is not a rate any terminal sends at.

Swept with `--boot-input-rate`, four carriage returns at 1.5 M instructions,
reading the resting PC:

    00 11 22 33 44 55 66 88 AA CC    0000079x-0007AE   inside C109's poll
    99                               0000267E          out of it
    BB                               00002670          out of it

`9` is 4800 baud and `B` is 9600 in `[68681]`'s set one, and both reach the
banner; `C` is 38400 and does not. So the PROM's autobaud has a set of rates it
will accept and the default was outside it.

### Why nothing before this found it

Three things had to be true at once and each was fixed for its own reasons in a
different campaign: the machine had to advance time at all (C109's defect, the
frontend stepping the CPU with no devices), the receiver had to be enabled and
programmed to eight bits before a byte was delivered, and the sender's rate had
to be one the autobaud recognises. The first two were closed months apart and
neither moved the boot, because the third was still wrong -- and a silent
machine looks the same whichever of the three is at fault.

The instrument that separated them was the input report: "12 of 12 characters
delivered, all four channels 8-bit with receivers enabled" excluded the port and
the delivery in one line, which left only the rate.

## C114 -- Normal/Service is bit 0, and every boot so far ran in Service mode

**Class: ours-wrong, and the measurement was right about the wrong machine.**

The boot PROM runs its self-test, draws its display diagnostic and then waits in
C109's console poll for ever. Sixty million instructions -- 9.6 emulated seconds
-- with a disk fitted, a screen fitted and the raster running changes nothing:
it is not waiting on the display, the disk, the calendar or a timeout.

### What it is waiting on

`APOLLO_CSR_SR_SERVICE` is `0001`, bit 0 of the CPU status register at `010000`,
and the oracle drives it from a machine configuration named "Normal/Service".
Its two settings are **inverted from the obvious reading**:

    PORT_CONFSETTING(0x00,                     "Service")
    PORT_CONFSETTING(APOLLO_CONF_SERVICE_MODE, "Normal")

So the bit reads **1 for normal operation** and 0 for service, and the constant
is named for the level it is *not*.

### Why this core had it clear

`CPU_STATUS_RESET` was `8100` -- bit 0 clear -- and it was *measured*. The
measurement was correct and was of the wrong thing: MAME's default for this
configuration is **Service**, so what was captured is the oracle's shipping
setting rather than a workstation's power-on state.

This project had already written the fact down and not connected it.
`mdsession.lua`, driving the Domain/OS install, sets `Normal` explicitly and
says why: *"its default is Service, so leaving it alone is a choice too."* The
note was about an install procedure; it is also the answer to why the machine
never boots.

### What the bit does

Setting it takes the PROM down a completely different path. With `8101`:

    final PC        0000658C   against 000007A2 in service mode
    boot PROM       34,356 reads   against 39,644
    display work    none           against 66,138 blit cycles
    serial          9,982,874 reads at sio1 register 4

It stops running the diagnostics -- which is what service mode is *for* -- and
polls the DUART's input-port change register instead. Whatever it is looking for
there is the next question, and it is a smaller one than "why does the machine
never boot".

### How it is modelled

As a **switch**, not a constant, because that is what it is: an input a machine
is configured with. `ap_boardreg_set_normal_mode` sets it and the default is
*normal*, since a workstation that boots is the machine this core is for. The
oracle's `8100` is still reachable and still asserted, as the service setting.

The general lesson is the one worth keeping: a measured power-on value is only
as good as the configuration it was measured under, and nothing in a captured
register says which knobs were where.

## C115 -- serial 1's input port is the RAM configuration, and it read zero

**Class: ours-wrong, and necessary but not sufficient.**

C114 put the machine into normal mode, where it stops running diagnostics and
polls `sio1` register 4 -- the input port change register -- 9,982,874 times in
a 30,000,000 instruction run.

`IP0`-`IP6` of the first DUART are not handshake lines. `apollo_sio::device_reset`
drives all seven from `apollo_get_ram_config_byte()`: the input port is strapped
to a **RAM configuration byte** describing which of four memory banks are
populated and how large, and the boot PROM reads it to size memory before it
does anything else. A machine whose input port answers zero is a machine with no
memory fitted, which is what this core was.

### The encoding is a table, and saying so is the finding

Four points, with the oracle's own bank comments:

    64   "4-4-0-0"    DN3500,  8 MB
    60   "4-4-4-4"    DN3500, 16 MB
    20   "8-8-8-8"    DN3500, 32 MB
    20   "2-2-2-2"    DN3000,  8 MB
    14   "8-8-0-0"    DN5500, 16 MB

`20` is "8-8-8-8" on one machine and "2-2-2-2" on another -- the same byte, four
times the memory. So the field is not a plain per-bank size and the *model* is
part of the decode. Four points do not determine a scheme and no manual in
`docs/references/` describes one, so this is modelled as a table with the pairs
the oracle records and a **refusal** for anything else. A computed byte would be
inventing the rule that makes the four work.

That refusal found something immediately: the headless frontend built its
machine with **4 MB**, which is not a configuration a DN3500 can be built in at
all -- four banks of 4 MB is the smallest the byte describes. It now builds
16 MB, which is a size the table covers, and a run says which byte it strapped
or that it strapped none.

### Necessary but not sufficient

With `60` strapped the poll continues. The reason is visible in the register
layout rather than mysterious: `IPCR`'s low nibble is the current level of
`IP0`-`IP3` and its high nibble is which of those four *changed*. `60` is
`0110 0000`, so `IP0`-`IP3` are all zero and the four upper pins carrying the
configuration are not in this register at all -- they are read at register 13,
"input port".

So the next question is narrow and well posed: what the firmware is waiting for
on `IPCR` specifically, when the byte it wants is not there. Either it polls for
a change on the low four pins that something else should drive, or the poll is
a timing loop against the counter it programmed at registers 6 and 4 one
instruction earlier.

## C116 -- OP3 is wired back to IP0, and the PROM counts the refresh square wave

**Class: ours-wrong, one missing wire.** The normal-mode poll that read `sio1`
register 4 nine and a half million times is closed, and the boot moves on.

### What the poll is

Disassembling the PROM at the resting PC gives the whole thing in twelve
instructions:

    006564  lea.l   $10400.l,a3
    00656A  move.b  #$4,$1a(a3)     ; OPCR = 04: OP3 is the counter/timer output
    006570  move.b  #$60,$8(a3)     ; ACR = 60: timer mode, clock X1
    006576  move.b  #$0,$c(a3)      ; CTUR = 00
    00657C  move.b  #$15,$e(a3)     ; CTLR = 15
    006582  move.b  $1d(a3),d0      ; register 14 read: START COUNTER
    00658C  move.b  $8(a3),d0       ; poll IPCR
    006590  btst.b  #$4,d0          ; delta-IP0
    006594  beq.b   $658c
    006596  move.b  $8(a3),d0
    00659A  btst.b  #$0,d0          ; IP0 high
    00659E  beq.b   $6596
    0065A0  ...     btst #$0 ; bne  ; IP0 low
    0065AA  addq.b  #$1,d1 ; cmp #5 ; bne $6596

It programs the timer, routes it to OP3, starts it, and then counts **five whole
cycles of IP0**. That is only meaningful if OP3 reaches IP0.

### It does, and the oracle says why in a comment

`apollo_state::sio_output` drives `ip0_w` from output bit 3, with:

    // The counter/timer on the SIO chip is used for the RAM refresh count.
    // This is set up in the timer mode to produce a square wave output on
    // output OP3. The period of the output is 15 microseconds.

So the board loops OP3 back to IP0, and the boot PROM measures the **memory
refresh** square wave to satisfy itself the timer runs at the rate it expects.

### Why this core had everything but the wire

`ap_sio` already had the refresh square wave, at exactly the right period --
`AP_SIO_REFRESH_PERIOD`, 15 µs, §3.9 cited, with a test asserting it inverts
every half period and returns after a whole one. It was implemented, correct,
and **connected to nothing**. The one line that returns it to the part's own
input port was missing, and with it the only program that ever looks at the
refresh could not see it.

That is the third time in this campaign that the missing piece was a connection
rather than a model: the Bt458 was complete and unwired, the disk controller was
complete and unwired, and now the refresh output. A subsystem that passes its own
tests and reaches nothing is the shape to watch for.

### What it unblocked

    final PC        00007026    against 0000658C
    blit cycles     655,368     against 0
    plane writes    1,572,872   against 0

Ten times the drawing of the service-mode diagnostic, and the frame buffer ends
*cleared* -- which is what a boot does before it draws anything of its own.

## C117 -- the A/D is a video monitor, and the diagnostic is a DAC check

**Class: ours-wrong, and partly still open.**

The boot posts a diagnostic code and flashes for ever. C109's post routine and
`008778-03` §3.7 -- "nine LED indicators ... written to the upper byte of the
control register" -- make the sequence readable, and it ends in an alternation:
a steady code with one bit toggling.

### Where the failure is decided

    007082  move.b  #$4,d0 ; bsr $708c
    007088  move.b  #$6,d0
    00708C  bsr.w   $6f3c            ; read an A/D channel
    007090  cmpi.b  #$70,d0 ; bcc.w $5eb6   ; too high -> post and flash
    007098  cmpi.b  #$52,d0 ; bcs.w $5eb6   ; too low  -> post and flash

Two channels, `04` and `06`, each range-checked into `[52, 70)`. This core
counted exactly **two** A/D accesses in that run, which is what pointed at it.

### What the converter is

Not a sensor. `c8p_read_adc` measures the controller's **own video output** --
the analogue level on one gun, at the pixel under the beam, through the lookup
table:

    drawing     red 10 + R/2   green 70 + G/2   blue 10 + B/2
    blanking    red 5          green 60         blue 5
    sync        red 5          green 5          blue 5

Channel bits 3-2 select a video measurement and bits 1-0 the gun, so `04` is red
and `06` is blue. Green's floor of 60 against the others' 5 is composite sync
riding on the green gun.

It also **confirms the blanking polarity** corrected two commits earlier: the
oracle's test for "drawing" is `SR_BLANK` being *set*, which is only sensible if
the bit is active low.

This could not have been modelled before the palette was wired and the raster
ran, because it reads both.

### What is still open, and it is a modelling gap rather than a mystery

With the A/D answered the failure *moves* -- the flashing pair changes from
`8D 0D` to `8D 7D 0D` -- so the reading now reaches the firmware and is used.
It is still out of range, and the reason is known:

**MAME measures at the position the firmware *stepped* the raster to, not where
a free-running beam happens to be.** `c8p_read_adc` indexes
`m_v_clock * m_buffer_width / 16 + m_h_clock`, and those are the counters
`DH_CK`, `DV_CK` and `DP_CK` advance one step per `CR1` write. This core models
the free-running raster and left the stepped counters out, with a note at the
time that "a model that free-ran the horizontal counter as well would answer the
diagnostic's questions before it asked them". That was right, and the other half
-- modelling the stepped counters so the diagnostic can ask -- is now the next
piece.

## C118 -- the keyboard is not write-only, and it powers up echoing

**Class: ours-incomplete.** `ap_kbd` sent scan codes and received nothing. The
real part has a command channel, and a machine with a display console asks it to
identify itself before believing there is one.

### The protocol

Every command begins `FF`, and the bytes after it accumulate until one matches.
That the accumulator is *wider than a byte* is the point: `FF12` is a prefix and
`FF1221` is a command, so a model matching one byte at a time cannot tell them
apart, and a prefix that cleared the message would make the identification
unreachable.

    FF        echo, and (re-)enter loopback
    00        in loopback: leave it, select the compatibility set. Not echoed.
    FF00      echo, compatibility set, leave loopback
    FF01      echo, keystate set
    FF11      echo (prefix)
    FF1116    send 00 FF 00, leave loopback
    FF1117    silent, stays in loopback
    FF12      echo (prefix)
    FF1221    identify: leave loopback, echo, then "3-@\r2-0\rSD-03863-MS\r"
    FF2181    beeper on, 300 ms
    FF2182    beeper off
    otherwise in loopback, echo; outside it, ignore

**It powers up in loopback**, which is the state a real one comes up in: until
told otherwise it echoes what it is sent rather than acting on it, and that is
how a host discovers a keyboard is there at all. `memset` would have made that
flag false, and false is a claim.

The beeper is *acknowledged* though the sound is not modelled -- this core has no
audio -- because a driver waiting for the acknowledgement would otherwise wait
for ever. That is a different decision from not modelling it at all.

### The wire

The board drains serial 1 channel A's transmitter every advance, hands each byte
to the keyboard, and puts what comes back into the same port's receiver at the
keyboard's own framing. Without that the command channel is a channel in name
only -- which is the fourth time this campaign that the piece that was missing
was a connection.

### What it did not change, and that is the honest part

The normal-mode boot is **unchanged**: same resting PC, same posted codes, same
blit count. The firmware does not reach the keyboard in that window. The
protocol is modelled because it is the machine's, not because it moved the boot,
and saying which is which is the difference between a measurement and a hope.

## C119 -- the OMTI status register was missing the two bits the protocol runs on

**Class: ours-wrong, from the manual's own page image.** `[OMTI]` §4.2's Table
4-2 gives the fixed-disk status register eight bits and this core had six.

    7,6  Not Used (Set to 1)      modelled
    5    IREQ, Command Complete   modelled
    4    DREQ, DMA Request        modelled
    3    BSY, Controller Selected modelled
    2    C/D, command or data     modelled
    1    I/O, direction           **absent**
    0    REQ, transfer one item   **absent**

`REQ` is the handshake every phase in §4.3 turns on: the controller sets it to
ask for a byte, the host's read or write clears it, and the pair repeats. A model
without it has no handshake at all, and a driver polling for it waits for ever.

### What §4.3 actually specifies

Six states -- RESET, IDLE, SELECTION, COMMAND, DATA, STATUS -- and this core had
the first two and a fragment of the third:

* **SELECTION**: the controller asserts `BSY`, *enters the command state*, sets
  `C/D`, then sets `REQ` "asking for the first command byte". This core asserted
  `BSY` and stopped, which is why a `FORCE LOAD` timed out.
* **COMMAND**: "When the command byte is written, the controller de-asserts the
  REQ bit ... This handshaking is repeated until all command bytes are
  transferred. C/D is then de-asserted."
* **IDLE** "is the only time the controller will respond to a select request",
  so a stray select mid-command is ignored rather than restarting it.
* **DATA**: `DREQ` is gated on the MASK's DMA ENABLE -- "if the DMA ENABLE bit
  ... has been previously set" -- and this core asserted it on every read, which
  asks for a DMA cycle nobody arranged in programmed I/O.

### One reading rather than a quotation

§4.3's status state says "If the INTERRUPT ENABLE bit was previously set in the
MASK register, the REQ bit is set in the STATUS byte, along with IRQ14". Taken
literally a polled driver would have no request to wait on and could never
collect the status byte -- and §4.2 describes programmed I/O as a supported mode.
The reading taken is that `REQ` is the status state's own handshake and the
*interrupt* is what the enable bit gates. Marked as a reading.

### What it moved

    before   DATA  5,279,663 read  1 write   STATUS 1,048,577 read
    after    DATA          2 read  6 write   STATUS 2,097,156 read

Six command bytes go out where one did, the status byte comes back, and the
machine returns to the **MD prompt** at `00002670` instead of resting in a
timeout. The command cycle completes.

`omti_suite`'s byte-for-byte comparison of a reset controller against a fresh
one caught a second defect on the way: `ap_omti_disk_reset` cleared the status
register and left the *phase*, which did not show while a SELECT only set `BSY`
and does the moment it enters the command state. §4.3: "It will then enter the
idle state."

## C120 -- the oracle hangs in normal mode, and `KEYBOARD TEST # 0` passes there

**Class: oracle-wrong, measured by patching it.** The keyboard test our display
boot fails has never been seen to succeed anywhere, and the reason is that the
oracle in normal mode **never reaches it**.

### The two machines are the same until `0067A2`

Booting `dn3500` in normal mode with the taps in from reset, and our own core
with `--screen c8p`, the diagnostic LED sequences agree exactly:

    FF EF DF FE EE DE CF BF AF        both, from the same program counters

Then the oracle stops for ever at `0067A2`, `btst.b #0,$b(a0)` -- ISR bit 0,
`TxRDY A`. It writes nothing further to the transmit buffer, makes **zero**
writes to graphics memory in six emulated seconds, and its screen stays blank.
Our core posts `9F ED DD 9D 8D 7D 6D 5D FC` and draws.

### Why: enable-transmitter is edge-triggered in `mc68681.cpp`

    if (!BIT(CR, 2) && BIT(data, 2))     // duart_channel::write_CR
    {
        SR |= STATUS_TRANSMITTER_READY | STATUS_TRANSMITTER_EMPTY;
        m_tx_enabled = true;

The condition is an edge against the *previous* command register value. The
firmware writes `CRA = 45`, `35`, `25` back to back at `006768`-`006774` --
reset error status, reset transmitter, reset receiver, each with bit 2 set. `45`
enables the transmitter; `35` resets and disables it; and because the previous
write also had bit 2 set, the enables in `35` and `25` are **ignored**. `TxRDY`
never comes back and the poll at `0067A2` never ends.

§4.2.7.3 gives it as a command and not an edge: "Enable Transmitter ... The
transmitter-ready status bit will be asserted." This core applies the enable
unconditionally after the miscellaneous command, which is why it gets past.

### Measured, not read

`!BIT(CR, 2) &&` was deleted from the oracle, `SUBTARGET=apollo` rebuilt, and
the same run repeated. The edit is reverted and the binary rebuilt from the
reverted source; the hang returns, which is the other half of the A/B.

    with the edge      10 LED codes   0 graphics writes   blank screen   PC 0067A2
    without it         15 LED codes   254,830 writes      the screen below

    SELF TESTS IN PROGRESS.
       KEYBOARD        TEST # 0 STARTED.
       CPU             TEST # 7 STARTED.
       MEMORY MODULE 1 TEST # 0 STARTED.
       MEMORY MODULE 2 TEST # 0 STARTED.
       MEMORY MODULE 3 TEST # 0 STARTED.

**`KEYBOARD TEST # 0` passes on the oracle**, and it goes on to the CPU and the
memory modules. Ours prints `SELF TEST FAILED. EXPECTED= 00000002, ACTUAL=
0000FF00, ADDRESS= 0001040B` in its place. So the failure Phase 5 has been
chasing for eight commits is **ours**, and it now has a known-good reference
rather than an assumption -- which is what the whole investigation lacked.

The eight-byte exchange the oracle completes is `01 02 04 08 05 0A 0C 0F 42`,
written one at a time to serial 1 register 3 from `0067B8`, each echoed by the
keyboard and compared at `006792`. That is the byte stream to diff against.

## C121 -- three ways an oracle script reports silence instead of an error

**Class: harness.** All three were hit in one session, each produced a
plausible-looking reading, and two of them invalidate earlier work.

1. **`machine.time.seconds` is the attotime's integer seconds field**, not the
   instant. A callback comparing it against `0.4` waits until second 1; one
   comparing against `0.05`, `0.2` and `0.4` fires all three at that instant.
   The first screen capture wrote three byte-identical PNGs and they read as a
   screen that never changes. `as_double()` is the instant. This is C34's
   attosecond error in a different disguise: an unreadable unit believed without
   dividing.

2. **The autoboot script does not run until the first timeslice after reset** --
   about 17 ms of emulated time, something like 100,000 instructions of boot
   PROM. Taps installed from the first `register_periodic` callback miss the
   whole reset path. This produced "the oracle makes three CSR writes and none
   of them is a diagnostic code", which is false: it makes the same first two
   writes we do, from `00006344`.

3. **`:apollo_config` is read at `MACHINE_RESET`**, so a script setting
   Normal/Service from a periodic callback changes nothing -- the machine was
   reset with the old value. `mdcapture.lua` sets it there, so its
   `APOLLO_MD_SERVICE=0` control run was **not a control**: both arms ran in
   MAME's default. That default is `PORT_CONFSETTING(0x00, "Service")`, so the
   service arm was right by accident and the normal arm was never run.

(2) and (3) have one fix: set the configuration, then `soft_reset()` so the
firmware runs again with the instrument in place. The reset re-runs the autoboot
script with fresh locals, so the guard has to live in `_G` or the machine resets
for ever -- which it did, four times, before that was noticed.

### What it bought

`screencap.lua` and `pngcmp.py`, and the first screen-level comparison against
the oracle this project has been able to make. In **service mode**, where both
machines drop to the `>` prompt without drawing:

    ink ours=102  oracle=102  differing pixels=0 of 819200

102 lit pixels each, pixel for pixel, decoded from our framebuffer on one side
and rendered by MAME on the other.

## C122 -- C120's hang is fixed in our checkout, and the datasheet says why

**Class: oracle-wrong, corrected in `ext/mame`.** C120 diagnosed the normal-mode
hang and stopped at the diagnosis. Three things have changed it into a fix.

### It reproduces on a pristine tree

All eight local edits stashed, `apollo` relinked from an unmodified checkout:
normal mode is blank at 0.1 s and at 60 s (byte-identical PNGs), service mode is
**102 lit pixels** at 0.1 s, 3 s and 10 s -- the figure already recorded for the
`>` prompt. So the display path is sound and the hang is upstream MAME's, not
something this project's instrumentation introduced. `stash show --stat`
confirms no edit touches `mc68681.cpp` either way.

### The datasheet settles what the firmware is asking for

`MC68681 ... Sep 1985` §4.2.7, read from the page image:

> Multiple commands can be specified in a single write to CRA as long as the
> commands are non-conflicting; e.g., the "enable transmitter" and "reset
> transmitter" commands cannot be specified in a single command word.

CRA is `[6:4]` miscellaneous, `[3:2]` transmitter, `[1:0]` receiver. So of the
firmware's three writes:

| write | misc | Tx field | verdict |
| --- | --- | --- | --- |
| `45` | 4, reset error status | enable | legal |
| `35` | 3, **reset transmitter** | enable | **the conflict §4.2.7 forbids** -- undefined |
| `25` | 2, reset receiver | enable | **legal, unambiguous "enable transmitter"** |

**The undefined case does not have to be resolved.** `25` is a plain enable with
a non-conflicting miscellaneous command beside it, and MAME drops it for the
sole reason that the *previous* command word happened to carry bit 2. That is
the bug, and it is provable without deciding what real silicon does with `35`.

### The fix, and why it is not the unconditional form

    - if (!BIT(CR, 2) && BIT(data, 2))
    + if (!m_tx_enabled && BIT(data, 2))

`CR` is a **command** register: its last value is not state, and says nothing
about whether the transmitter is enabled. Gating on the transmitter's own state
is idempotent, cannot re-assert `TXEMT` under a character being shifted out --
which a bare `if (BIT(data, 2))` would, and that is presumably what the edge was
reaching for -- and fixes the sequence, because `35`'s reset clears
`m_tx_enabled` and the following `25` therefore enables.

Kept as `tools/mame-oracle/duart-tx-enable.patch`. This is a **correction** to
the oracle rather than instrumentation, so it is not reverted before commit the
way a probe is; it is one hunk, and it is upstreamable.

*Any normal-mode oracle reading taken before this is not comparable to one
taken after: the machine now runs where it previously stopped at `0067A2`.*

## C123 -- the state dumper reported SR = 0 for ever, and nearly proved it

**Class: our-instrument-wrong, caught before it produced a finding.**
`apollo_dump_text` walks `m_entry_list` directly. Musashi keeps the status
register **decomposed** into flag variables and reassembles it only in
`device_pre_save()`:

    void m68000_musashi_device::device_pre_save()
    {
        m_save_sr = m68ki_get_sr();
        m_save_stopped = ...; m_save_halted = ...;
    }

A walk that never dispatches presave therefore reads `m_save_sr` as whatever
`device_reset` left -- **zero** -- on a machine whose SR is nothing of the sort.
`m_save_stopped` and `m_save_halted` have the same shape.

Left alone, the first CPU diff would have shown the oracle's SR permanently
zero and permanently different from ours. That reads as a real and dramatic
finding; it is an artefact of the instrument. The fix is one line --
`dispatch_presave()` before the walk, which is what an actual save does -- and
the dumper is no longer `const` because of it.

**The general rule this is an instance of:** a dump that claims to be "the save
state as text" must do everything a save does, or it is a different thing with
a misleading name. The registry is not all live state.

## C124 -- the oracle wrote on its own boot cartridge, and destroyed the one bootable SR10.3

**Class: our-harness-wrong, after six emulator-side hypotheses.**
`019439-001.CRTG_PSKQ3_91_BOOT_1` loaded the SR10.3 kernel twice and then
reported `error: sysboot not found` for every run afterwards. The volume, the
run directory, the era config, the drive-settling pacing, the `re` sequence and
the `sc499` media-change remedy were each eliminated; the volume was `cmp`-ed
before and after a run and found byte-identical. The conclusion recorded at the
time was that the run "is not yet a reproducible experiment".

**It was the cartridge, and the cartridge was an input.** Re-fetched from
bitsavers and compared: of 50,727,936 bytes, **exactly one 512-byte block
differed** -- block 0, which carries the descriptor the boot PROM validates:

    pristine  00 13 d8 00  00 13 d8 2a  00 13 f6 b4 ... "SYSBOOT REV "
    after     00 00 00 00  00 00 00 00  00 00 00 00 ... (a wbak header)

`0013D800` is the address the PROM demands, so a cartridge without that block is
`sysboot not found` by definition. Two further facts fix the mechanism: the
file's mtime was **6 ms after** the successful run's own log, and the block
written is **nowhere in the pristine tape**, so it was not a read-back of the
same medium.

`sc499_device::write_block` is an `fseek`/`fwrite` straight into the image file
and MAME opens a cartridge `flags=00000003`. So `-ctape media/...` hands the
guest a pen and our only copy of the medium. On the evidence the write happened
on the `Crash_Status 00010005` decline path rather than the calendar gate --
that was the run whose mtime matches -- but the mechanism does not depend on
which guest path did it.

**Fixed in the harness, not in MAME**: `mdsession.py` stages every cartridge
into the run directory and mounts the copy, on `--ctape` and on `!swap ctape`
alike, re-copying on each run so `--keep-rundir` cannot inherit damage. The
disk is deliberately *not* staged -- an install's writes to its volume are the
product. The asymmetry is the hardware's: a tape is read, a disk is written.

Then three runs of one invocation, one at a time: **byte-identical consoles**,
`Domain/OS kernel(7), revision 10.3.5` each time, source cartridge md5
unchanged. The load is reproducible and always was; the medium was being
consumed by looking at it.

**The rule:** an input that a run can open read/write is not an input. Check the
artefact before the sixth hypothesis -- and hash it before *and* after any run
whose result you may want to explain.

## C125 -- the "25 Years Ago" config does nothing in 2026, and the gate is a terminal wait

Two findings from one measured run, and the first retires a claim this project
had recorded as measured.

### The era config is a no-op at this host date

`apollo_m.cpp:1213` shifts the RTC year on **every** reset, but only inside
three windows:

    if      (year < 25  && APOLLO_CONF_25_YEARS_AGO) year += 75;
    else if (year < 30  && APOLLO_CONF_30_YEARS_AGO) year += 70;
    else if (year >= 70 && !30_YEARS_AGO && !25_YEARS_AGO) year -= 70;

A 2026 host presents `26`, which is **not** `< 25`, not `>= 70`, and only
`< 30` for the 30-year setting. So with "25 Years Ago" on or off the machine
gets the *same* clock, and an A/B between them compares two identical
configurations.

**This refutes a recorded measurement.** The plan said the calendar gate
"clears, measured: with the harness's `25 Years Ago` off the '14 days' message
is gone and the kernel proceeds". Re-run with the shift off: the message is
still there, byte for byte. The earlier reading came from a run that also
differed in its volume -- the confound that ran through that whole thread.

`--era 30` is the one setting that still does something here: `26 + 70` puts the
guest in **1996**, which is C53's other failure mode rather than a fix.

### The gate is a terminal wait, and the machine is not halted

Measured with `APOLLO_MD_ACTIVITY=15` over a 1,200-emulated-second run, which is
what distinguishes waiting from stopped:

| emulated | PC | tape w | disk w | what |
| --- | --- | --- | --- | --- |
| 15-60 s | `00002BCC`-`000039C0` | 0 → 157 | 2198 → 2322 | PROM, `di c` |
| 60-440 s | `000039xx` | 157 → 41,753 | 2322 | the PROM's tape loader |
| ~450 s | `3C451DE8` | 41,753 | 2322 → 3,513 | kernel entered, volume touched |
| 450-1200 s | `3C451DE8`-`3C451E00` | **41,753** | **3,513** | frozen |

So the kernel loads, writes 1,191 disk sectors, prints the gate and enters a
tight loop with **no further I/O at all**. A carriage return does not move it,
and neither do two. It is not a prompt and not a crash: it is the wait the
message describes, and the message means what it says -- service mode, reset,
`CALENDAR`.

### What the gate compares, and the three ways past it

`dn3500-invol-done.awd` was cleanly shut down on 2026-08-01 and the host is
2026-08-17: **16 days**, against a threshold of 14. That is the whole of it, and
it is why the SR10.4 install worked -- the checkpoint was made the same day it
was used. A checkpoint ages into this gate whether or not anything about it
changes.

The routes, none of them taken yet and each a piece of work:

1. **A checkpoint made the same day.** A fresh INVOL, which is blocked on
   `Unable to assign disk - error status = 100001`.
2. **A host-clock shim** for the MAME process (`libfaketime`), which is not
   installed here. MAME has no option to set the emulated date.
3. **The machine's own remedy**: boot in Service mode, `ex calendar` and set the
   date (C52 has the dialogue), then boot Normal from the same run directory so
   the RTC's NVRAM carries it. This needs a *clean* MAME exit -- `!quit` kills
   the process, and NVRAM is written on exit -- so it needs a driver flag before
   it can be tried.

**The rule this is an instance of:** a config whose effect depends on the host
clock has to be checked against the host clock, not assumed from its name. This
one had been switched on and off across several sessions and reasoned about in
both directions while doing nothing at all.

## C126 -- SR9.7 and SR10.1 are not obtainable, so the release item's scope is three

`COMPLETION_PLAN.md` asks for "every Domain/OS release obtainable (SR9.7,
SR10.1-10.4)". Establishing what *obtainable* means, rather than leaving it
open:

| Source | Domain/OS media held |
| --- | --- |
| `bitsavers.org/bits/Apollo/` | `SR10.3/`, `SR10.4/` |
| `bitsavers.org/bits/Apollo/Apollo_JRJ/` | `SR10.2/`, `SR10.3/`, `SR10.4/`, `SR10.4.1/` |
| Jim Rees' Apollo archive (`jim.rees.org/apollo-archive/`) | **none** -- it points at bitsavers for install tape images and holds only patches, `from-tape.txt` and a NetBSD boot block |

So there is **no SR9.7 or SR10.1 install media online**, from the two archives
this hobby actually uses. Searches return documentation and mentions -- SAU1 was
dropped "at about SR9.7.5", Emacs support notes for SR10.1 -- and no media.

**The item's scope is therefore SR10.2, SR10.3 and SR10.4**, and it should say so
rather than list two releases nobody can get. SR10.4.1 is an *upgrade* set over
SR10.4, not a release to install from bare metal.

**SR10.2 is now held**, four cartridges in `media/sr10.2/`, and its **standard**
boot cartridge passes both bootability tests where SR10.3's did not (C124's
lesson applied before any emulator was run):

    017286-001.CRTG_STD_SFW_BOOT_1  blk0 00 13 d8 00 ... "SYSBOOT REV " " M68K "
                                    13 sau7/ entries

## C127 -- the calendar gate's remedy is the machine's own, and CALENDAR stamps the volume

`001746-06` Procedure 2-7, "Synchronizing Node Hardware Clocks", is the
documented answer to `More than 14 days have elapsed since the last shutdown.
Switch to service mode, press reset and run CALENDAR.` -- the message means
exactly what it says. The manual's dialogue, and what this machine actually
prints, differ in wording and not in shape:

| Prompt | Answer |
| --- | --- |
| `Please select the disk [w=Winch\|s=Storage mod\|f=Floppy\|q=Quit]...` | `w` |
| `The time-zone is set to 0:00 (UTC). Would you like to reset it?` | `n` |
| `The calendar date/time is <date>. Would you like to reset it?` | **`y`** |
| `Please enter today's date (year/month/day):` | `2026/08/05` |
| `Please enter the local time in 24 hour format (hour:minute):` | `12:00` |
| `The calendar is being reset forward by more than 5 minutes. Is the above information correct?` | `y` |

The last prompt is not in the manual's example, which sets the clock by three
minutes. C52 recorded this dialogue with `n` to the third row and so never
reached it.

**The gate clears, and `ex domain_os` then offers the restore** -- `RBAK_BS
reloading system software from cartridge tape...` and `Do you wish to proceed?
(Y/N)`, which is the SR10.4 install's own next step. Service mode was tried and
is *not* required here despite the manual's step 2: the fault below is identical
in both positions, and the node has no display to shut down first.

**Why CALENDAR and not the era config**: the utility takes the **disk** as its
first answer, and on the evidence it stamps the volume rather than only setting
the RTC -- which is why running it clears a gate about "the last shutdown" and
why the message names it. The RTC alone would not survive to the next process,
since MAME writes NVRAM on a clean exit and this harness kills the emulator.

**One thing is unexplained and is recorded rather than smoothed over.**
CALENDAR aborts on exit, every time, in both switch positions: it prints
`The calend` -- ten characters of `The calendar has been set to ...` -- and MD
reports `10200E6: 6100`, which `002398-04` §4 makes an address-and-contents
crash-entry line at CALENDAR's own entry point. Read back afterwards the clock
says `2015/09/03`, not the `2026/08/05` asked for, so the write is partial or
mangled. **It is nonetheless sufficient**: the gate clears and the restore runs.
Whether the abort is ours, the oracle's, or what the utility does when the jump
is 24 years is not established, and no result here rests on the date being the
one requested.

## C128 -- RBAK skips SYSBOOT, so a restored volume is not a bootable volume

Our core booted the freshly restored SR10.3 volume and sat in the boot PROM at
PC `00000794` to 350 M instructions. The reflex is to look for a defect in our
disk path. The volume is the answer, and it takes one command and no emulator:

| volume | `SYSBOOT REV` |
| --- | --- |
| `dn3500-sr10.4-installed.awd` (MINST run) | at **`0x870`** |
| `dn3500-osclean.awd` (SR10.4, restored, MINST not run) | **absent** |
| `dn3500-sr10.3-osclean.awd` (SR10.3, restored, MINST not run) | **absent** |
| `dn3500-invol-done.awd` (INVOL only) | **absent** |

`0x870` is 2160 -- block 2 of a 1024-byte-block volume plus a 112-byte header --
which is `[AEGIS]` §4.3.2's ten contiguous blocks at physical `02`-`0B`
(`RING.md` 85d) exactly.

And the restore log says so in as many words, at line 45 of 534:

    TFP:  Skipping over SYSBOOT found at beginning of volume.

So **RBAK restores the archive's members and deliberately does not write the
node boot program to the volume's boot blocks.** The install manual
(`008860-A03`) shows why: `sysboot` is a *separate tape file*, `File ID: /base
sysboot`, restored after `ri.apollo.os.v.10` -- and `001746-06` Table 3-2 lists
`sysboot` as the "Node boot program" in `/sys`.

**Two things follow.** A `-osclean` checkpoint cannot be booted by the PROM *by
construction*, for either release -- both give byte-identical hashes
(`74FD47F132624CFF`, final PC `00000794`, 1,266,013,264 clocks), which is the
proof that the volume's contents are not what the PROM got to. And "boot the
release" requires MINST, not merely the restore.

**The rule:** when a volume will not boot, look for the boot block before
looking at the boot path. Two releases produced the identical wrong answer,
which is a stronger signal than either one alone.

## C129 -- the node ID is a loadable image, so the two-node blocker is not a media one

`COMPLETION_PLAN.md` recorded the ring's two-node item as blocked because "a
Domain volume label carries its own `node_id`, and `node_id_from_volume` refuses
a node that disagrees with its disk", so two nodes need two volumes with two
different IDs -- "a media question, not a ring one".

**The oracle can be given any node ID, and the route is in its source.**
`apollo_ni` is a `device_image_interface` (`apollo.h:375`), so the ID is a
**loadable 32-byte ROM image** rather than a compiled-in constant.
`DEFAULT_NODE_ID = 0x12345` (`apollo.cpp:102`) is only what a run without an
image gets -- which is why every MINST transcript here shows `//node_12345`.

The format, from `apollo_ni::call_load` (`apollo_m.cpp:932`):

    size        exactly 32 bytes, or the load is refused
    data[2]     node ID bits 23-16
    data[4]     node ID bits 15-8
    data[6]     node ID bits 7-0
    data[30]    (data[2] + data[4] + data[6]) & 0xFF, checked on load
    else        zero

The stride of two is the hardware's rather than the loader's whim:
`apollo_ni::read` returns each byte in the **high half** of a 16-bit word, so
this is a byte-wide ROM on a word-addressed bus and every second byte is a hole.
Offset 15 of that register window answers the checksum, which is why byte 30
carries it.

`tools/mame-oracle/nodeid.py` writes one and `mdsession.py --node-id` passes it.
The image is deliberately **not** staged the way a cartridge is: `apollo_ni`'s
`write` handler logs `Error: writing node id ROM` and stores nothing, so there is
nothing for a guest to damage.

**So the two-node item's blocker is now a second install, not an unknown.** The
route is: write an `.ani` for node B, INVOL and install a volume with that ID,
and both volumes then satisfy `node_id_from_volume` on their own machines. That
is hours of oracle time and a known procedure, which is a different kind of
blocked from "no route".

**What this does not license** is patching a copied volume's label to a new ID.
The objects on a copy were created by node A and carry its UIDs, so the copy
would be a machine lying about its identity -- which is exactly what
`node_id_from_volume`'s own comment refuses, and any finding from a two-node run
built that way would be a finding about a fiction.

*Verification: `oracle_nodeid`, 15 checks restating `call_load`'s acceptance rule
-- including that the checksum is a **byte-wide** sum, which agrees with a wider
one for every small ID and disagrees exactly when it carries.*

## C130 -- SR10.3 is installed and runs on this core, and MINST is what makes a volume bootable

The full route, end to end, with every step's evidence:

1. **`EX CALENDAR`** with a date supplied (C127) clears the 14-day gate.
2. **`ex domain_os`** off `019439-001` runs RBAK: 474 entries, `Restore
   complete.`, then `shut` -> `Shutdown successful`. Kept as
   `media/dn3500-sr10.3-osclean.awd`.
3. **`sh`, login `user`, `/install/tools/minst`** in the same session, because
   the RBAK environment is booted from tape and there is no other way back into
   it. Authorized Area and target both defaulted to `//node_12345`, `yes` to
   Domain/OS, media `ct`, template **11 (`large`)** -- the same choice the SR10.4
   install made.
4. **Four software cartridges** `018848-001..004` swapped in as MINST asked.
   Each swap staged a copy into the run directory (C124), so the media in
   `media/` was never opened by the guest and its md5 is unchanged.
5. `RAI MINST has completed` with **two** warnings, both named rather than
   summarised: `Unable to install .../os.v.10.3__bind4.8_operations_guide` (a
   release-notes document) and `Could not delete directory //node_12345/bscom`.
   Neither is an OS file.

**MINST is what writes the boot block.** Before it, `SYSBOOT REV` was absent
from the volume; after it, it is at `0x870` -- block 2 of a 1024-byte-block
volume, `[AEGIS]` §4.3.2's physical `02`-`0B` (C128). That is the difference
between a restored volume and a bootable one, measured on the same file.

**And this core boots it.** `media/dn3500-sr10.3-installed.awd` through the
DN3500's own PROM: the self-tests, `Loaded: SELF_TEST Revision: 2.4`, the CPU
diagnostics, then Domain/OS's own

    Salvaging boot volume
    Salvol - Offline(7), revision 10.3, June 5, 1990  2:05:12 am
        Preparing file list...
        Salvaging...  % complete

-- SR10.3's *own* salvage utility executing on this core. The salvage is
expected, not a fault: the install session was killed rather than shut down from
the target, and an unclean volume is what Domain/OS salvages.

**Nothing in the run is SR10.3-specific, checked rather than assumed.** The
`Configuration information is not initialized`, `Self test failed. Expected=
00000000, Actual= 00000012, Address= 00010912` and `Do you wish to continue
(y,n)? y` lines are **identical** in the SR10.4 reference boot, so they are
this core's standing gaps and not something the new release exposed.

*State hash at the 350 M reference bound: `8E1A2E2E106A367B`, final PC
`00002EE4`, 1,495,341,007 clocks -- mid-salvage, and recorded as such.*

## C131 -- a stray emulator from an earlier session had been burning a core for five hours

Found while listing processes for another reason: an `apollo-headless` with
`--mid-access-devices` against the SR10.4 volume, **4h57m elapsed and 4h55m of
CPU**, started before this session began. Killed.

It is recorded because of what it silently does to *timing*: one core of this VM
was unavailable for every measurement taken while it ran, and nothing in a
report says so. No figure here rests on it -- this session made no new timing
claim, and a state hash is unaffected by contention, which is exactly why the
identity harness is a hash and not a stopwatch.

**The rule:** `pgrep apollo-headless` and `pgrep mdsession` before any timing
run, and after any run that was killed rather than allowed to finish.
`mdsession.py` already arms `PR_SET_PDEATHSIG` and handles `SIGTERM`/`SIGINT` for
exactly this reason; a bare `apollo-headless` started from a shell has no such
protection.

## C132 -- the 14-day gate compares against a zero, so no clock can satisfy it

Three clocks were tried against the installed SR10.3 volume and all three failed,
in two different ways:

| `--clock` | Domain/OS says |
| --- | --- |
| 1987-08-02 (default era) | `The calendar is more than a minute slow.` |
| 2014-01-11 | `More than 14 days have elapsed since the last shutdown.` |
| 2015-09-04 | `More than 14 days have elapsed since the last shutdown.` |

A fourth would have been a parameter search, which `CLAUDE.md` forbids. The
volume's own label answers it instead.

### The physical volume label's time fields, and where they are

`002398-04`'s physical-volume-label diagram gives the structure:

    +B0  .label_write_time      TIME LABEL WRITTEN
    +B4  .last_mounted_node     LAST MOUNTED NODE
    +B8  .node_boot_time        TIME SYSTEM WAS BOOTED
    +BC  .mounted_time          TIME THIS VOLUME WAS MOUNTED
    +C0  .dismounted_time       TIME THIS VOLUME WAS DISMOUNTED
    +C4  .salvage_node          NODE OF LAST SALVAGE
    +C8  .salvage_time          TIME SALVAGE COMPLETED

**The base is `0x440`**, found by differencing an installed volume against an
INVOL-only one: `00 01 23 45` appears at `0x4F4`, which is MAME's
`DEFAULT_NODE_ID` in `.last_mounted_node` at `+B4`, so `0x4F4 - 0xB4 = 0x440`.
Not by reading a number off an OCR'd diagram.

**The unit is the high 32 bits of Apollo's 48-bit 4 µs clock from 1980-01-01**
-- one tick is `4 µs x 65536 = 262144 µs`, about 0.262 s, and a 32-bit field
reaches 2016. The 4 µs clock alone would overflow 32 bits in under five hours,
which is what makes the high-half reading the only one that fits.

**A quarter-second tick was tried first and is wrong**, by 4.9%: it put both
known volumes over a year early while getting the *time of day* right, which is
precisely what makes a wrong epoch constant believable. The correct rule is
confirmed against two independent statements by the machines themselves:

| field | decodes to | the machine said |
| --- | --- | --- |
| `FFF808EE` (SR10.3 `.mounted_time`) | 2015-09-03 15:57:47 | CALENDAR: *"last recorded time was 2015/09/03 15:47:46 UTC"* |
| `A45DF6AB` (SR10.4 `.mounted_time`) | 2002-11-27 19:51:49 | C52's CALENDAR reading, `2002/11/27` |

### What the four volumes say

| volume | `.mounted_time` | `.dismounted_time` |
| --- | --- | --- |
| `dn3500-sr10.4-installed.awd` | 2001-11-05 05:29:14 | **2001-11-05 07:17:23** |
| `dn3500-osclean.awd` | 2001-11-04 22:20:45 | 2001-11-04 22:26:46 |
| `dn3500-sr10.3-osclean.awd` | (2014 era) | **set** |
| `dn3500-sr10.3-installed.awd` | 2014-01-08 01:22:03 | **`00000000`** |

**So the SR10.3 installed volume was never cleanly dismounted, and the gate is
`now - .dismounted_time > 14 days` against a zero.** That difference is the whole
of the clock, at every clock, so **no `--clock` can ever satisfy it** -- which is
why 2014-01-11, three days after that volume's own mount time, failed exactly as
2015 did. The SR10.4 volume boots because it has a dismount time; this one has
none.

The cause is known and is ours: the MINST session was killed rather than shut
down from the target, and MINST's own last words are *"shut down the target node
and reboot it from its own disk"*.

**And this core cannot fix it, by design.** `main.c:3115` reads the image into a
private buffer and never writes it back -- "the drive the machine sees behaves
like a drive, and the user's image on disk is untouched" -- which is what makes
an identity boot repeatable and why the salvage re-runs every time. A clean
dismount has to come from the oracle, and the route is proven: `shut` from the
RBAK environment's `)` prompt wrote `.dismounted_time` on
`dn3500-sr10.3-osclean.awd`.

**The general lesson:** a stop that varies with a parameter invites tuning the
parameter. Two readings were enough to show the answer was not in that
direction; the third was one too many.

## C133 -- SR10.3 is cleanly dismounted and boots, and `--clock` is not what the kernel compares

The volume is finished. Under the oracle, in Service mode so the PROM stops at MD
rather than auto-booting a now-bootable disk:

    ex calendar -> w -> n -> **n** to "Is the calendar correct?" -> 2015/09/04
    ex domain_os -> n to "BOOT VOLUME NEEDS SALVAGING"
    re, di c, ex salvol -> w -> "1 -f -s -t"  ->  Salvage complete
    re, di w, ex domain_os
        Domain/OS kernel(7), revision 10.3, August 22, 1990
        Apollo Phase II Environment   Revision 10.3
    shut -> Shutdown successful

Four things in that sequence were learnt rather than known, and each cost a step:

- **CALENDAR branches.** On a volume whose recorded time is *ahead* of the clock
  it asks `Is the calendar correct?` instead of `Would you like to reset it?`, and
  answering `n` is what opens the date prompt. C127's transcript is the other
  branch.
- **Service mode is required once the disk is bootable.** With a `sysboot` on the
  volume the PROM boots it and the serial console goes silent -- two sessions
  produced *zero bytes* before this was understood. `--mode Service` (added for
  `001746-06` Procedure 2-7) is what reaches MD.
- **`ex salvol` needs a reset first.** After the kernel's decline the machine is
  left mapped and MD answers `NO FILE I/O IN MAPPED MODE`.
- **`di w` then `ex domain_os` boots the disk's own OS**, which is what makes the
  clean shutdown possible; `di c` would run the cartridge's RBAK.

`media/dn3500-sr10.3-installed.awd` now reads:

    mounted      FFF86BC8  2015-09-03 17:48:20
    dismounted   FFF885FA  2015-09-03 18:17:38
    salvage      FFF83FAE  2015-09-03 16:59:01

and its label is **structurally identical** to `dn3500-sr10.4-installed.awd`'s --
same fields set, cleanly dismounted, differing only in era and in the salvage
record.

### And this core still gates it, which localises a different question

Booted here with `--clock 2015-09-05`, two days after that dismount, the salvage
is gone and the kernel loads directly -- and it still says `More than 14 days
have elapsed since the last shutdown.` Hash `3253FD7CFB8821D4`, 9,786,961,926
clocks, MMU enabled, 442,527 ATC descriptor fetches.

**Two days is not fourteen, so the kernel is not comparing our `--clock`.** The
proof is the volume that works: `dn3500-sr10.4-installed.awd` has
`dismounted 2002-11-27 21:45:12` and boots under the **default 1987** clock with
no calendar message at all -- fifteen years *behind* its own dismount stamp,
which the same check would have to reject.

So the open question is no longer "which `--clock`" -- that was C132's mistake and
this is the evidence it was a mistake in both directions.

**A lead was written here, then "refuted", and the refutation was itself wrong.
Both are recorded because the mistake is the instructive part.**

The lead was that `--clock` does not reach this check, on the evidence that the
SR10.4 volume booted at `--clock 1987-07-31` and at `--clock 2015-09-05` with
*zero* calendar complaints in both. **That measurement used `--boot-limit
350000000`, which stops before the kernel reaches the check.** Re-run at 3 G
instructions, SR10.4 at `--clock 2015-09-05` says `The calendar is more than a
minute slow.` So `--clock` does reach it, and the "refutation" was an artefact of
a bound chosen for a different purpose -- the identity harness's bound, used
without asking whether it covered the event being tested.

**The live reading, and it is about representability rather than about which
date.** `ap_mc146818_read` returns `year % 100` because the part has no century
byte -- `[146818]` calls it "a 100 year calendar" and the century is the caller's.
So `--clock 2015-09-05` presents year **15** to the firmware, and a Domain/OS that
reads that as 19xx cannot be given 2015 at all. MAME's own driver says the same
thing from the other side: `apollo_m.cpp:1213` shifts the host year *into* the
eighties and nineties (`year < 25` -> `+75`, `year >= 70` -> `-70`), which is only
worth doing if that is the era the OS handles.

**And this volume's stamps are 2015**, because the install ran with the guest
clock where CALENDAR left it. So the volume and the clock may be mutually
unreachable: no `--clock` this core can express puts a representable year beside a
2015 stamp.

### The 199x experiment was run, and it produced a third result that fits neither

`EX CALENDAR` was given **1993/06/15 12:00** -- accepted with the documented
backward-time warning, *"setting the time backward may cause duplicate Unique
ID's to be generated"*, which `001746-06`'s Procedure 2-7 note also carries. Then
`re` / `di w` / `ex domain_os` off the disk:

    The calendar is more than a minute slow.

So **CALENDAR sets the RTC and does not rewrite the volume's recorded time** --
the 2015 stamp stood and the 1993 clock was behind it.

**And the mount it wrote decodes to neither date.** The volume now reads

    mounted      37CC10A0  1987-10-11 06:12:47
    dismounted   FFF885FA  2015-09-03 18:17:38

`37CC10A0` under the tick rule calibrated above is 1987-10-11, against a clock
CALENDAR was told to set to 1993-06-15. No unit makes `37CC10A0` into 1993-06-15
with a 1980 epoch either, so this is not simply the rule being wrong.

### Five reproducible readings, and no model that fits them

Against `dn3500-sr10.4-installed.awd`, whose `.dismounted_time` is
**2002-11-27 21:45:12** and sits at 64% of the tick range so nothing wraps, at
`--boot-limit 6000000000` so the check is actually reached:

| `--clock` | RTC year register | Domain/OS says |
| --- | --- | --- |
| 1987-07-31 | 87 | `calendar is more than a minute slow` |
| 1999-12-31 | 99 | `More than 14 days have elapsed` |
| 2000-01-02 | 00 | `calendar is more than a minute slow` |
| 2002-11-28 | 02 | `More than 14 days have elapsed` |
| 2015-09-05 | 15 | `calendar is more than a minute slow` |

`1999-12-31` was **re-run and gives the same answer**, so these are deterministic
and there is a model; four were tried against them and every one fails:

- *two-digit year read as 19yy*: predicts `slow` for 99 (1999 < 2002). Measured
  `elapsed`.
- *read as 20yy*: predicts `elapsed` for 87 (2087 overflows). Measured `slow`.
- *pivoted at some year*: cannot produce `slow` at 87, `elapsed` at 99, `slow` at
  00 -- the answer is not monotone in the year.
- *our BCD/binary conversion being inverted*: 87 -> `0x57` -> 57 and 99 ->
  `0x63` -> 63 land in the same era, so they cannot differ.

**The one thing the table does establish** is that the guest's notion of *now*
**decreases** as our clock crosses year 99 to year 00, since the message flips from
`elapsed` back to `slow`. Something wraps there. And `1987-07-31` -- our default,
the clock the identity boot uses -- is on the `slow` side, which means **the
reference boot has never passed this check either**; it stops at 350 M, before it.

### The handle for finding it, so the next session does not start from guesses

The message is printed from the kernel, and the boot's own report gives the
address: the run ends at **PC `3C456BAE`/`3C456BB2` -> physical `01081BAE`** in
every one of these five runs. `tools/kernel_symbols.py` exists and the kernel is on
the volume. Disassembling backwards from there is a bounded job and it answers the
question outright, where five more `--clock` values will not.

**What is measured, and what is not concluded**

Three readings on one cleanly-dismounted volume, all on this core:

| `--clock` | Domain/OS says |
| --- | --- |
| 1987-08-02 | `The calendar is more than a minute slow.` |
| 2015-09-05 (1.24 days after its own dismount stamp) | `More than 14 days have elapsed since the last shutdown.` |
| 1993-06-15, set through CALENDAR under the oracle | `The calendar is more than a minute slow.` |

The second is the one no reading yet explains: 1.24 days is not fourteen, and if
the firmware were reading the two-digit year 15 as 19xx the clock would be
*behind* the stamp and the message would be the other one.

**This is left as an open question rather than answered**, because this thread has
now produced two confident conclusions that were wrong -- a `--clock` sweep C132
called a mistake, and a "refutation" built on the identity harness's 350 M bound.
A third would be worse than none. What the next session should have before
running anything: the *logical* volume label (`002398-04` p. 4434 -- "The LV label
is the first block of a logical volume"), which this project has not walked and
which is where a per-logical-volume shutdown record would live. The physical
label's mount history, modelled and tested here, is demonstrably not it.

**The method lesson, twice over in one thread:** a bound is part of an experiment.
`--boot-limit 350000000` is the identity harness's number, chosen because the
reference boot is 350 M, and reusing it to ask a question about something that
happens at 3 G produced a confident wrong answer -- then a "refutation" published
on the strength of it.

**What is established regardless**: SR10.3 is installed, cleanly dismounted, and
executes here through the PROM, the self-tests, the kernel and the MMU. What is
not established is a boot past the calendar check on this core, and the reason it
is not is now a question about our calendar rather than about the volume.

## C134 -- the calendar check, disassembled, and the defect is in the date we present

Five console messages became a disassembly. `--boot-stop-physical-pc` stops the
machine on the check and the report prints the registers, so the operands are
measurable rather than inferred.

### The code

`sau7/domain_os` extracted from the boot cartridge with `ct_extract.py` -- no
emulator -- and disassembled with capstone. The function ends just before the
message strings, so walking back for a `link a6` prologue finds it. Addresses are
file offset + `0x0102A400`, the bias fixed by a string seen in both the file and a
memory dump:

    010C5132  move.l $3C4453FA.l, d0     ; now
    010C5138  sub.l  $3C4C19E0.l, d0     ; - last shutdown
    010C513E  cmpi.l #$FFFFFF1B, d0      ; -229
    010C5144  bge.b  $10C5152
    010C5146  ...                        ; "The calendar is more than a minute slow."
    010C5152  cmp.l  (a2), d0            ; a2 = 3C457AC6, the day threshold
    010C5154  ble.w  $10C51DC            ; in range -> proceed
    010C5158  ...                        ; "More than %a days have elapsed ..."

**One signed 32-bit subtraction decides both messages**, and `-229` settles the
tick unit for a third time: `229 x 0.262144 s = 60.03 s`, which is exactly "more
than a minute".

**"More than 14 days" was never a threshold.** The string is `More than %a days`
-- the number is a *computed* argument, from `a3`. Every reading in C132 and C133
that treated 14 as a limit was reading a formatted variable as a constant.

Also in the same neighbourhood, and worth knowing on its own: *"The UID generator
is unable to function with the current setting of the calendar (year >= 2015).
Please run the CALENDAR utility before trying to boot again."* Domain/OS has a
**hard year-2015 ceiling**, which is where a 32-bit quarter-second-ish tick from
1980 runs out -- the same arithmetic, stated by the kernel.

### The operands, measured

`3C4C19E0` dumped at the check reads **`A45E5C08`**, 2002-11-27 21:45:11 -- the
SR10.4 volume's own label time. **So the physical volume label *is* the source**,
and C133's "demonstrably not it" is **withdrawn**: it was inferred from console
messages, and the memory says otherwise.

| our `--clock` | `d0` (signed) | the kernel's `now` | verdict |
| --- | --- | --- | --- |
| 2000-01-02 | -349,416,349 | 2000-01-02 18:01:52 | **correct**, and `slow` is the right answer |
| 2002-11-28 09:00 | +64,424,899 | 2003-06-11 09:01:52 | **+195 days wrong** |
| 1999-12-31 | +1,922,364,402 | 2018-11-08, wrapped to 1983 | **+18.85 years wrong** |

**The time of day is right in all three and only the date is wrong**, which is
what makes this our defect and not the kernel's arithmetic.

### What it is not, checked rather than assumed

The register file is **correct**. Dumped at the check for the 2002-11-28 09:00
case, `010900`:

    35 00 01 00 09 00 05 28  11 02 20 00 10 80

-- seconds `35`, minutes `01`, hours `09`, day-of-week `05` (Thursday, and
2002-11-28 was a Thursday), day `28`, month `11`, year `02`, register B `00`. All
BCD, 12-hour, which is exactly what the oracle configures: `apollo_m.cpp:1112`
sets `set_binary(false)` and `set_24hrs(false)`. So the bytes we hand the guest
are right and the BCD-mismatch reading -- which fits the "every field under ten
works" pattern seductively well -- is refuted by the dump.

### The next step, and it revives a lead I wrongly buried

What remains is the **battery RAM configuration block**, which
`ap_calendar_build_config` writes and which Apollo uses for its own stored
configuration. It is the only remaining thing this core presents that could carry
a date the register file does not. C133 named it, then "refuted" it on a
measurement that was itself wrong; the refutation is now doubly retired.

Concretely: `3C4453FA` is `now`, and it is dumpable. One run that dumps it beside
the register file, for a date whose fields exceed nine, shows whether the guest
built `now` from the registers or from somewhere else -- and there is no need to
guess which.

## C135 -- the date defect is isolated to one field, and it is an encoding disagreement

C134 left the kernel's `now` disagreeing with `--clock` by a date-dependent
amount. A discriminating pair settles what varies. Both clocks are 09:00 on the
third of a month, one year apart in neither direction that matters -- the *only*
difference is whether the month exceeds nine:

| `--clock` | fields over nine | kernel's `now` | error |
| --- | --- | --- | --- |
| 2001-02-03 09:00 | none | 2001-02-03 09:01:52 | **+0.00 days** |
| 2001-11-03 09:00 | month | 2002-05-05 09:01:52 | **+183 days** |
| 2002-11-28 09:00 | month, day | 2003-06-11 09:01:52 | +195 days |
| 1999-12-31 | month, day, year | 2018-11-08 (wraps to 1983) | +18.85 years |

**A clock whose every field is under ten is exact.** That is the signature of BCD
being read as binary: `0x11` is 11 in BCD and 17 in binary, and the two agree
only below ten. `2001-11-03` with the month taken as 17 lands on 2002-05-03,
two days from the measured 2002-05-05 -- the residue being the guest's own
normalisation, not the encoding.

### Our part is right, and that is checked three ways

- The register file dumped at the check reads correct BCD: day `28`, month `11`,
  year `02`, day-of-week `05` (2002-11-28 was a Thursday), register B `00`.
- `ap_mc146818_write` to register B **does** take: a standalone harness shows
  `B=00 month=11 day=28` after reset and `B=04 month=0B day=1C` after writing
  `DM`, which is the datasheet's behaviour exactly.
- `mc146818_suite` already covers it --
  `test_the_clock_reads_bcd_unless_told_otherwise` -- so this is not a table-walk
  gap.

**And the oracle configures the same thing**: `apollo_m.cpp:1112` calls
`set_binary(false)`, and `mc146818.cpp:266` sets `REG_B_DM` only when that flag is
set. So MAME presents BCD too.

### Which means the next step is the oracle A/B, and it is one number

`CLAUDE.md`: *"When our number differs, instrument the oracle to prove which side
is wrong."* The number is `now`, the kernel global at `3C4453FA`, and both cores
can be stopped at the same instruction (`010C513E`) with the same volume. If the
oracle's `now` is also wrong for a month over nine, this is Domain/OS's own
behaviour with a BCD calendar and our core is faithful; if the oracle's is right,
the difference is ours and the register file is not where it lives.

**There is already circumstantial evidence for the first**: under the oracle,
`EX CALENDAR` was asked for **2026** and the volume came back stamped **2015**
(C127) -- a 2026 whose year byte `0x26` read as binary 38 would land near 2018,
and the same shape of error. That was recorded at the time as "unexplained" and it
is the same phenomenon.

**What this does *not* license** is changing our RTC to present binary. The
datasheet ties the encoding to register B's DM bit, the guest leaves DM clear, and
our part does what the bit says. Making it lie to match a guest would be fitting
the model to one observation -- the thing the oracle A/B exists to avoid.
