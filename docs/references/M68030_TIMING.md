# MC68030 instruction timing — what the manual actually publishes

Source: `MC68030 User's Manual`, 3rd ed., 1990, ch. 11 (*Instruction Execution
Timing*), cited below as `[030]` with page numbers.

Recorded before the CPU core is written, because the shape of what Motorola
publishes decides the shape of what we build — and reading it the other way
round is how a "cycle-accurate" core ends up merely reproducing a table.

## The three published quantities

`[030]` §11.3, pp. 11-6 ff. Every instruction and addressing mode in the tables
of §11.6 (p. 11-24 on) carries:

| Quantity | Meaning |
| --- | --- |
| **CC** — instruction-cache case | Clock periods to execute when *all* corresponding instruction prefetches hit the on-chip instruction cache. Split for some instructions into **CCea** (effective address calculation) and **CCop** (the rest of the operation). |
| **NCC** — average no-cache case | Microcode time plus all external bus activity, assuming **both caches miss**, and one external bus cycle per two instruction prefetches. |
| **head** / **tail** | The overlappable portions. The *head* of an instruction is the time at its start that can overlap the previous instruction's end; the *tail* is the time at its end that can overlap the next instruction's start. |

**Overlap between consecutive instructions A and B is the lesser of A's tail and
B's head** (`[030]` p. 11-7). Best case for B is its head fully absorbed by A's
tail.

## The assumptions baked into those numbers

These are the reason the tables are a *check* and not a recipe.

- **All bus cycles are assumed to take two clock periods** (§11.3.1, §11.3.3).
  Real bus cycles do not, once wait states and contention exist — §11.5
  (p. 11-18) covers wait states separately.
- **CC assumes no overlap at all, and no data-cache hits** (§11.3.1). It is not
  the time an instruction takes in a running program; it is the time it takes in
  isolation with a warm instruction cache and a cold data cache.
- **NCC assumes no overlap either**, so *the head and tail values do not apply
  to NCC* — the manual says so explicitly (§11.3.3, p. 11-8). Head/tail compose
  only with CC.
- **NCC is an average over prefetch alignment.** Because the real figure depends
  on whether the instruction stream is odd- or even-word aligned, Motorola
  computed both and published "the average of the odd-word-aligned case and the
  even-word-aligned case (rounded up to an integral number of clocks)". The
  prefetch bus-cycle count is averaged and rounded the same way.

## What this means for the core

The last point is the important one, and it is worth stating flatly:

**No published NCC number is a value any single execution of that instruction
ever takes.** It is a mean of two alignment cases, rounded up. An emulator that
looks up an instruction's published cycle count and adds it is therefore not
cycle-accurate and cannot be made so by refining the table — it is reproducing
an average that the hardware never exhibits on any particular run.

This is exactly why the project builds a strictly cycle-stepped core, where
timing is *emergent*: alignment falls out of where the instruction actually
sits, cache hits fall out of actual cache state, bus cycle length falls out of
actual wait states, and contention falls out of a single shared arbitration
point. The published tables then become what they are good for — an independent
check on numbers we produce, rather than the source of them.

So when a probe disagrees with a table figure, the first questions are
mechanical, not mysterious:

1. Is this a CC or an NCC comparison? Head/tail only compose with CC.
2. What is the actual prefetch alignment? An NCC table entry is an average of
   two, so agreeing with it exactly is the surprise, not disagreeing.
3. Are both caches actually in the state the table assumes?
4. Are all bus cycles actually two clocks in this configuration?

Characterise the shape before touching anything, per the project's discipline.
A discrepancy that is a consistent half-clock on odd-aligned instructions only
is an alignment story, not a bug in an opcode.

## Where the rest of it is

| Topic | `[030]` section | Page |
| --- | --- | --- |
| Performance tradeoffs | 11.1 | 11-1 |
| Resource scheduling (eight independent resources) | 11.2 | 11-2 |
| Timing calculations, head/tail/overlap | 11.3 | 11-6 |
| Effect of the data cache | 11.4 | 11-16 |
| Effect of wait states | 11.5 | 11-18 |
| **Instruction timing tables** | 11.6 | 11-24 |
| Address translation tree search timing | 11.7 | 11-51 |
| MMU instruction timing | 11.7.2 | 11-60 |

## Status

`reference recorded, nothing implemented`. No figure from ch. 11 has been
transcribed into code yet. When one is, it lands as a *check* on a measured
number, with its page cited, per `docs/COMPLETION_PLAN.md` Phase 2.

Data-dependent instructions published only as a range (division and the like)
are the `PROVISIONAL` case the plan names: model the documented value, mark it
`PROVISIONAL` in code and in `docs/PROJECT_STATUS.md`, and never invent a point
number to fill the gap.

## The quantity we actually need, and where it is published

Recorded after the first differential measurement (`tools/mame-oracle/FINDINGS.md`
C7), because it changes this document's conclusion in one specific way.

The core needs *microcode time* — the clocks an instruction takes between its bus
cycles — and the argument above says the published tables are a check rather than
a recipe. That argument is about **NCC**, and it holds. It does not apply to
**CC**, and the difference is worth stating precisely.

§11.6's legend says each timing column carries `total(a/b/c)`, where `a` is
operand read cycles, `b` is instruction bus cycles including prefetches, and `c`
is write cycles. For a register-to-register instruction the cache case reads:

```
ADD    Rn,Dn     head 2  tail 0   CC 2(0/0/0)   NCC 2(0/1/0)
ADDA.W Rn,An     head 4  tail 0   CC 4(0/0/0)   NCC 4(0/1/0)
```

**`CC` with `(0/0/0)` is pure execution time.** No operand reads, no writes, and
— because the instruction is in the cache — *no instruction bus cycles either*.
There is nothing in that number but microcode. And §11.3.1 defines CC without
any averaging: "the total number of clock periods required to execute the
instruction, provided all the corresponding instruction prefetches are resident
in the on-chip instruction cache."

So the averaging objection is specific to NCC and to the parenthesised bus-cycle
count, which the legend itself flags:

> "Because the second value is the average of the odd-word-aligned case and the
> even-word-aligned case (rounded up to an integral number of bus cycles), it is
> always greater than or equal to the actual number of bus cycles (one bus cycle
> per two instruction prefetches)."

That sentence is also an independent confirmation of C7: the manual states the
*actual* prefetch rule — one bus cycle per two prefetches — and says the
published count is an over-estimate of it. Our core's alternating 0/2 is the
actual; the table's `b` and the oracle's flat constant are both roundings of it.

### What this means for the plan

The execution-time item is **not** blocked on measurement after all. Its route is
the project's other permitted one: a documented value with a cited page.
Transcribe `CC` and `head`/`tail` for the forms whose cache case reads `(0/0/0)`
— the register-to-register instructions — and add them to a bus time that keeps
alternating. Those rows are pure microcode and carry no averaging.

The forms with a non-zero `a` or `c` are different: their CC includes operand bus
cycles at the table's assumed two clocks each, which our core produces itself
from actual bus state. Adding the table's CC whole would count those twice. Those
rows need the bus part subtracted, which is arithmetic on published numbers
rather than a judgement — but it is a second step, and the register forms are
worth landing first because they need no adjustment at all.

### How a transcription gets checked

Not by re-reading it. §11.3.4 works two examples end to end with head, tail and
CC for each instruction and a stated total — `ADD.L A1,D1` then `SUBA.L D1,A2`
coming to six clocks. Any transcription of those rows must reproduce that total
through `ap_m68030_overlap`, which already implements Equation (11-1). That is an
external check on both the numbers and the rule at once.

## §11.3.4's worked example is mislabelled, and it was about to be trusted

Found while preparing to transcribe §11.6.8, and worth recording because the
previous section nominated this very example as the *external check* on the
transcription. Using it as one would have written a wrong number in.

The example computes an instruction pair:

```
                      Head   Tail   CC
  1. ADD.L  A1,D1       2      0     2
  2. SUBA.L D1,A2       4      0     4

  Execution Time = CC1 + [CC2 - min(H2,T1)] = 2 + [4 - 0] = 6 clocks
```

§11.6.8's own table says otherwise for the second instruction:

| Row | Head | Tail | CC |
| --- | --- | --- | --- |
| `ADDA.W Rn,An` | 4 | 0 | `4(0/0/0)` |
| `ADDA.L Rn,An` | 2 | 0 | `2(0/0/0)` |
| `SUBA.W Rn,An` | 4 | 0 | `4(0/0/0)` |
| **`SUBA.L Rn,An`** | **2** | **0** | **`2(0/0/0)`** |
| `SUBA.W EA,An` | 0 | 0 | `4(0/0/0)` |
| `SUBA.L EA,An` | 0 | 0 | `2(0/0/0)` |
| `CMPA Rn,An` (word) | 4 | 0 | `4(0/0/0)` |

**The table is right and the example is mislabelled.** Three reasons, in
increasing order of weight:

1. The word forms cost 4 and the long forms cost 2 in **six separate rows**
   across three instructions. A single worked example does not outweigh that.
2. It is the physically sensible direction. `SUBA.W` sign-extends its word
   source to 32 bits before subtracting; `SUBA.L` does not. The extra work
   belongs to the *word* form, which is where the table puts it.
3. The example's own arithmetic is unaffected. `2 + [4 - min(4,0)] = 6` is a
   correct demonstration of Equation (11-1) whatever instruction those numbers
   belong to — so the example is sound as a demonstration of the *rule* and
   unsound only as a source for `SUBA.L`'s *numbers*.

### What this changes

The worked example still checks Equation (11-1), and `overlap_suite` uses it for
exactly that. It does **not** check a transcription of `SUBA.L`, and the note in
that test now says so — otherwise someone later, reconciling the test against
§11.6.8, would "correct" `4, 0, 4` to `2, 0, 2` and destroy the arithmetic check
in the process.

So the transcription's external check has to come from somewhere else: the
per-row pattern above, and the second worked example in §11.3.4 (which uses
Equation (11-2) and effective address tables). Recorded here rather than
discovered again.

## `CC + bus time` is not the model, and the tables prove it

Found by building it and looking at the result, which is why it is here rather
than in a commit message.

The obvious way to use the transcribed figures is to add each instruction's
`CC` to the bus time the core actually accumulated. `CC` carries no fetch time
(the instruction is in the cache) and the transcribed rows carry no operand bus
time either, so the two look disjoint and the sum looks safe.

It is not. Compare the two columns for one row:

```
ADD Rn,Dn     CC 2(0/0/0)     NCC 2(0/1/0)
```

The no-cache case has **one more instruction bus cycle and the same total**. A
bus cycle is two clocks by the table's own assumption, so that prefetch cost
*nothing*. It happened while the microcode ran.

And it is not a simple maximum either:

```
ADD Dn,EA     CC 3(0/0/1)     NCC 4(0/1/1)
```

Here the extra prefetch adds **one** clock, not zero and not two. How much of a
fetch can be hidden depends on how much execution there is to hide it in — which
is §11.2's whole point: the processor is "eight independently scheduled
resources", and "very little of the scheduling is directly related to
instruction boundaries".

So `bus + CC` serialises what the hardware overlaps, and over-counts by the
amount of the fetch the microcode would have covered. It was implemented,
measured against the oracle, and backed out: `MOVEQ` went to an alternating 2/4
where the oracle says a flat 2, and the manual says the answer in a warm cache
is exactly 2.

### What the model has to be instead

Not an addition. The fetch and the execution are two resources running
concurrently, and the instruction's cost is what falls out of scheduling them —
`max` in the simple case, and something structured where a bus cycle can only
overlap the part of the microcode that is not waiting on it.

The check is already available and does not need the oracle: for any
transcribed row, a cold-cache run of our core must come to the row's **NCC**,
and a warm-cache run to its **CC**. Two published numbers per instruction,
bracketing the same execution from both sides. A model that satisfies both is
scheduling the resources correctly; one that satisfies neither is adding when it
should be overlapping.

That is a far better target than either number alone, and it is the shape the
execution-time item should be built to.

## `max` also widens what can be transcribed

A note for the next transcription pass, recorded because the reasoning that
excluded those rows no longer applies.

Rows with a non-zero read or write count were left out on the grounds that their
`CC` includes operand bus cycles at the table's assumed two clocks each, and the
core produces those itself — so adding the published figure whole would count
them twice.

That was true of *addition*. Under `max(microcode, bus)` it dissolves. Take
`ADD Dn,EA`, `CC 3(0/0/1)` and `NCC 4(0/1/1)`: if the microcode is 3, then
`max(3, 2) = 3` gives `CC` with the write's two clocks, and `max(3, 4) = 4`
gives `NCC` with the write and a prefetch. The microcode is simply `CC` again,
and the bus time the core measures does the rest.

In general the microcode is `CC` for any row where `CC` is at least the bus time
its own cache case contains — which is every row seen so far, since a row whose
microcode was shorter than its own bus activity would have `CC` equal to that
bus activity rather than more.

So the memory forms are transcribable on the same terms as the register forms,
and the two-sided check applies to them unchanged. They are the obvious next
pass, and unlike the register forms they will exercise the `NCC > CC` case,
which nothing does today: every transcribed row has `NCC == CC`, because its
single prefetch hides under microcode of at least two clocks.

## `max(microcode, bus)` does not survive the effective address tables

Recorded before transcribing them, because it changes what the transcription is
for.

`ADD.B D0,(A0)` composes, by the manual's own Equation (11-2) and the no-overlap
rule of the no-cache case:

```
  CC  = CCea + [CCop - min(Hop,Tea)]  =  3 + [3 - min(0,1)]  =  6
  NCC = NCCop + NCCea                 =  4 + 3               =  7
```

and the oracle measures 7. So the target is 6 warm and 7 cold.

Now try to reach it with `max(microcode, bus)`. The core's bus for this
instruction, cold, is a prefetch, the operand read and the write — six clocks.
Whatever microcode figure is chosen, `max(m, 6)` is 6 when `m ≤ 6` and `m`
otherwise; it can produce 6 or it can produce 7, but the warm case has a
*smaller* bus (the read hits the data cache) and needs the *smaller* answer.
There is no single `m` giving 6 against a four-clock bus and 7 against a
six-clock one, because `max` is monotonic in both arguments and the required
answers move the wrong way relative to each other.

**So the two-resource approximation runs out exactly here**, which is where its
own header said it would: "a full model would let a bus cycle overlap only the
part of the microcode not waiting on it, which `max` does not express". An
`ADD` to memory *must* wait for its operand read before it can add, so that read
cannot hide under the microcode that consumes it — while a prefetch still can.

The distinction the model needs is therefore not more resources but one
question per bus cycle: **is the microcode waiting on this?** A prefetch is
not waited on; an operand read feeding the current operation is. That is a
two-bucket refinement of `max`, not a rewrite:

```
  total = max(microcode, hideable_bus) + blocking_bus
```

Under it, `ADD Dn,(An)` cold is `max(m, prefetch 2) + read 2 + write 2`, which
reaches 7 for `m = 3`; and warm, with the read cached, `max(3, 0) + 0 + 2 = 5`
— still not 6, so the arithmetic is not yet right either, and the missing clock
is where the `fea`'s own microcode lives.

This is not a change to make from a chair. It needs the effective address
tables transcribed so both sides of the composition are published numbers, and
then the two-sided check applied per addressing mode. What is settled is that
the *current* model cannot be extended to cover the footnoted rows, and that is
why the step now declines them rather than reporting a component.

## Two things found while working out the composition

### The tables assume the data cache is off, and our harness had it on

§11.6's assumption list, which is easy to read past on the way to the numbers:

> - All memory accesses occur with two-clock bus cycles and no wait states.
> - All operands in memory, including the system stack, are long-word aligned.
> - A 32-bit bus is used ...
> - **The data cache is not enabled.**
> - No exceptions occur (except as specified).
> - Required address translations ... are resident in the address translation
>   cache.

Comparing a figure measured with the data cache *on* against one computed with
it off is not a like-for-like comparison, and the difference is one operand read
per repeat. That is invisible for the register forms — they touch no data — which
is why the two-sided check passed for a dozen rows before this surfaced. The
sampling helpers now disable it, and say why.

The lesson generalises past this instance: a published table's *assumptions*
need transcribing as carefully as its numbers, because a harness that violates
one silently compares two different experiments.

### `max(microcode, hideable) + blocking` does not work either

C9's second question proposed splitting bus cycles into those the microcode
waits on and those it does not. Worked through for `ADD.B D0,(A0)`, whose
targets are 6 cached and 7 uncached:

| Split | Cached | Uncached |
| --- | --- | --- |
| blocking = read + write | `max(m,0) + 4 = 6` → m = 2 | `max(2,2) + 4 = 6` ✗ |
| blocking = read, write posted | `max(m,2) + 2 = 6` → m = 4 | `max(4,4) + 2 = 6` ✗ |

Both give 6 for the uncached case where the manual and the oracle say 7. The
reason is the same in each: the extra prefetch is worth two clocks and the
answer must move by **one**. No all-or-nothing split can produce a partial cost,
and every two-bucket arrangement is all-or-nothing by construction.

So the marginal cost of a prefetch is *fractional* with respect to a bus cycle,
and it varies per instruction: `NCC − CC` is 0 for `ADD Rn,Dn`, 1 for
`ADD Dn,EA` and `MOVE Rn,(An)`, and 2 for a taken `Bcc`. That quantity is
published for every row, which is the useful observation — it is the slack the
instruction's microcode has, measured by Motorola, and not something to be
derived from a scheduling rule this document could invent.

Whether to *use* it that way is the open question. Taking `NCC − CC` as the
per-instruction prefetch cost reproduces both columns by construction, which is
suspiciously easy — it fits two points with two points. It would need checking
against something neither column determines: an instruction with **two**
prefetches, or a wait-stated bus, where the published pair no longer pins the
answer. Until that check exists this stays unimplemented, and the footnoted rows
stay declined.

## The marginal cost of a prefetch: a claim made and withdrawn

**The section this replaces was wrong, and how it was wrong is worth more than
what it claimed.**

It asserted that `(NCC−CC)/p` — the published difference divided by the
published instruction-bus-cycle count — is "0 or 1, never 2, never fractional",
across eleven rows drawn from four tables, and offered that uniformity as the
discriminating evidence that the quantity is genuinely per-prefetch.

Running the same division over **every** transcribed row, rather than the eleven
chosen for the table, gives:

| Row | CC | NCC | `p` | `(NCC−CC)/p` |
| --- | --- | --- | --- | --- |
| `ADD Rn,Dn`, `NOP`, `UNLK`, `MOVE Rn,-(An)`, `Bcc.B` untaken | — | — | 1 | 0 |
| `ADD Dn,EA`, `MOVE Rn,(An)`, `LINK.W` | — | — | 1 | 1 |
| `Bcc` taken, `RTS`, `RTR`, `RTD`, `ANDI to SR`, `Bcc.L` untaken, `DBcc` looping | — | — | 2 | 1 |
| `DBcc` (count expired) | 10 | 13 | 3 | 1 |
| **`BSR`** | 6 | 9 | 2 | **1.5** |
| **`DBcc` (cc true)** | 6 | 8 | 1 | **2** |
| **`LINK.L`** | 6 | 7 | 2 | **0.5** |

Three counterexamples, all from rows that were already transcribed and sitting
in the same table when the claim was made. The values are not confined to 0 and
1, and the division is not always integral.

### What went wrong, and it was not the arithmetic

The eleven rows in the original table were the ones that had come up while
transcribing, and they agreed. The three that disagree were in the file too and
were not checked. A pattern found by looking at a subset and then stated as
holding generally is a *hypothesis presented as a result* — and the fact that
the subset spanned four different tables made it feel like coverage when it was
not.

The correct move, once a pattern is suspected, is to compute it over everything
mechanically and look at the exceptions. That takes a minute and it is what
overturned this.

### What survives

Not much, and that is the honest position. `NCC − CC` is still the published
marginal cost of an instruction's prefetch activity, and it is still the only
place the manual states how much of a fetch an instruction can hide. What is
withdrawn is that dividing it by `p` yields a clean per-prefetch constant — so
there is no licence to apply it per prefetch our core actually runs, which is
precisely what the composition needed it for.

The caveat already recorded — that `p` is "the average of the odd-word-aligned
case and the even-word-aligned case (rounded up)", an upper bound rather than a
count — is now doing real work rather than sitting as a hedge. `BSR` at 1.5 and
`LINK.L` at 0.5 are what a rounded denominator looks like. That suggests the
true prefetch counts are not integers for those rows, which would mean the
per-prefetch cost cannot be recovered from the published pair at all for them.

### Consequence for the plan

The composition item goes back to needing a model rather than a lookup, and the
footnoted rows stay declined. Transcribing `p` is still worth doing — so this
division is computed in code, over every row, where an exception is visible at
the point of use rather than dependent on someone checking. That is the change
that would have caught this.

### The check now runs in code

`ap_m68030_timing_t` carries `prefetches` — the `p` of the no-cache case's
`(r/p/w)` — and `ap_m68030_prefetch_cost` performs the division, reporting
`exact` false when `NCC − CC` is not divisible by it.

`timing_table_suite` then requires that **every** inexact row is named in a list
in the test. `BSR` and `LINK.L` are named; a row that becomes inexact without
being added fails, and so does a list that names a row which is no longer
inexact. That is the property the prose claim could not have: it was true of the
rows its author looked at, and nothing made the others speak up.

A second test asserts what the withdrawal was actually about — that an exact
cost of **2** exists, which the claim said it did not. `DBcc` with the condition
true divides cleanly and gives 2, so the values are not confined to 0 and 1 even
where the division is well behaved.

## Both compositions, from the manual's own worked examples

Found by going back to §11.3.3 and §11.3.4 in the PDF rather than to the notes
above, and it settled two things the notes had been circling.

### Equation (11-2) is Equation (11-1) over *components*

§11.3.4 prints it as a separate, "more specific" formula:

```
  CCea1 + [CCop1 - min(Hop1,Tea1)] + [CCea2 - min(Hea2,Top1)] +
    [CCop2 - min(Hop2,Tea2)] + [CCea3 - min(Hea3,Top2)] + ...
```

Every term has the same shape as (11-1)'s — a component's cache case less the
lesser of its own head and the *previous component's* tail. The only thing
(11-2) adds is that an instruction contributes **two** components, its effective
address then its operation. So there is one rule, not two, and (11-1) is (11-2)
for instructions whose effective address costs nothing.

That collapses what looked like a second accumulator into
`ap_m68030_overlap_add_component`, with `ap_m68030_overlap_add` as its
one-component wrapper and `ap_m68030_ea_timing_compose` adding the pair. Writing
(11-2) separately would have duplicated the rule and left two places for it to
drift.

**Verified against the manual's own five-instruction example**, which exists
precisely to exercise (11-2) and prints its answer: **40 clock periods** for

```
  ADD.L -(A1),D1 ; AND.L D1,([A2]) ; MOVE.L (A6),(8,A1) ; TAS (A3)+ ; NEG D3
```

`ea_timing_suite` runs it and gets 40. The components are fed in **as the
example prints them**, not read from our tables: feeding the transcription in
would check the composition against our own numbers, and a mistranscribed row
would move both sides of the comparison together.

**A register operand contributes no component at all** — not one costing zero.
The example's last instruction, `NEG D3`, reaches back past where an address
component would have been, `[CCop5 - min(Hop5,Top4)]`, and overlaps against the
*previous operation's* tail. A zero-cost component in its place costs nothing
itself and still consumes that tail, so it over-counts by up to the tail. The
tables say so too, writing a register row's head and tail as `-` rather than 0 —
which is why `head_applies` was carried from the first transcription, and this
is the first thing that needed it.

### The no-cache case composes by addition, and the published figure is a mean

§11.3.3 works `MOVE.L (d16,An,Dn),Dn` followed by `CMPI.W #(data).W,(d16,An)`
with both caches missing throughout, and prints processor-activity diagrams for
both alignments (Figures 11-4 and 11-5). Three numbers come out of it:

- The MOVE's average no-cache case is "**2 + 7 = 9 clocks**" — its own figure
  from §11.6.6 plus its effective address's from §11.6.1, with no overlap term.
  "It should be noted again that the no-cache-case time assumes no overlap."
- The two instructions together are "9 + 7 = 16 clocks", the same plain sum.
- And the one that matters most here: the MOVE "is **eight clocks for even
  alignment and 10 clocks for odd alignment**, an average of nine clocks",
  while "the total execution time of the two instructions ... is **16 clocks for
  both even and odd alignment**".

So the two published columns compose by two different rules — `CC` through head
and tail, `NCC` by addition — and `ap_m68030_no_cache_total` is the second,
deliberately not the same function as the accumulator.

**The third bullet is the useful one, and it is stronger than the averaging rule
already recorded above.** It is not merely that a published `NCC` is a mean of
two alignment cases; it is that the difference between those cases *moves
between adjacent instructions rather than adding to the stream*. The MOVE costs
8 or 10 depending on where it sits, and whichever it costs, the CMPI after it
costs 8 or 6 to match. That is the cache holding register's long word being paid
for by one instruction or the other, and it is exactly the alternation this core
already exhibits (`FINDINGS.md` C7).

Two consequences for the item:

1. **A per-instruction comparison against a published `NCC` is the wrong
   comparison**, and no refinement of this core will make it right — 9 is a
   number that instruction never takes on real hardware. The right unit is a
   *sequence*, where the alternation cancels. `machine_suite` already averages
   over alignments for the single-instruction rows, which is the same statement
   for a stream of one repeated instruction.
2. It is an independent confirmation of the fea table's full-format rows, which
   this project has not transcribed: the "7" in "2 + 7" is
   `fea (d16,An,Xn)` full format, `7(1/1/0)`, read off §11.6.1 while checking
   this. The brief-format row we *do* carry is `6(1/1/0)`, and the two being one
   apart is the extension word.

## The decomposition: `microcode = CC - 2(r + w)`

The question the section above left open, answered from the tables themselves.

Every table in §11.6 states two things at its head that together make a
published figure decomposable:

> The number of read, prefetch, and write cycles is given inside the parentheses
> as (r/p/w). The read, prefetch, and write cycles **are included in the total
> clock cycle number**.
>
> All timing data assumes **two-clock reads and writes**.

So `CC` is not pure microcode for any row that touches memory — it is microcode
*plus* its own operand cycles at two clocks each. That is precisely why
`CC + bus time` over-counted, and why the original transcription took only the
`(0/0/0)` rows: those are the ones where the distinction does not arise.

`r` and `w` are now transcribed beside `p`, and

```
  microcode = CC - 2(r + w)
```

is the quantity this core was missing. The bus half it measures for itself, so a
wait-stated cycle or a cache hit still moves the answer — which is the whole
difference between this and a cycle-table model.

**It separates things the totals hide.** `MOVE Rn,(An)` and `MOVE Rn,-(An)` are
3 and 4 clocks; after the write's two come out they are 1 and 2 clocks of
microcode, and that difference is exactly the predecrement's extra work.

### The marginal cost of a prefetch, recovered for one class of row

The withdrawn claim tried to get a per-prefetch cost by dividing `NCC − CC` by
`p`. What was missing was not arithmetic but §11.3.3's definition of what `NCC`
*is*: "the average of the odd-word-aligned case and the even-word-aligned case
(rounded up)".

For a **single-word instruction that is not a change of flow**, the odd-aligned
case runs no external fetch at all — the cache holding register's long word
already holds the word — so the published difference is half the even case:

```
  exposure = 2 (NCC - CC)
```

A bus cycle is two clocks, so this can only come to **0 or 2**: such a prefetch
either hides completely under the instruction's microcode or not at all. That is
a falsifiable claim about the published tables, and `timing_table_suite`
computes it over **every** row — the discipline the withdrawn claim had to be
given after it was stated from eleven rows and falsified by three others sitting
in the same table. It survives: no applicable row gives 4, and both outcomes
occur, so the rule discriminates rather than being vacuous.

Which rows expose and which hide is itself a check on the reasoning. The
register forms hide; the memory destinations expose — *except* `MOVE Rn,-(An)`,
whose extra clock of microcode is exactly what covers its fetch. A rule that got
that pair the same way round would be describing "writes to memory" rather than
"has microcode to spare".

**Where it does not apply, and why**, both named in the test rather than
assumed: a multi-word instruction, where both alignments may need a fetch so the
average is not half of one case; and a change of flow, where the pipe refills at
the target whatever the instruction's own alignment. `BSR` at 1.5 clocks per
prefetch and `LINK.L` at 0.5 are what those rows look like under the withdrawn
division, and they are exactly the rows this excludes.

### What is still open

The model, stated in full:

```
  total = microcode + measured operand bus + (a prefetch ran ? exposure : 0)
```

Worked for `ADD.B D0,(A0)`, which is `FINDINGS.md` C9's open row: the operation
composes with `fea (An)` through Equation (11-2) to `CC 6`, of which 4 clocks
are the published read and write, leaving **2 clocks of microcode**. Our core
measures the read and the write itself at 2 clocks each, so the warm total is 6;
the exposure from the operation's row is 2, so the even-aligned cold total is 8
and the odd-aligned 6, averaging **7** — the manual's figure and the oracle's.

### Wired in, and C9's row closed

The step now prices a transcribed row as

```
  total = microcode + measured operand bus + prefetch cost
```

and `ADD.B D0,(A0)` comes to **6 warm** and **7 cold averaged over both
alignments** — the manual's composed figure and the oracle's measurement. The
effective address is taken from bits 5-0 of the instruction word, which is where
it is for every row this applies to: the arithmetic forms' operand, and MOVE's
*source*, §11.6.6's own figures already including the destination address.

Three things had to be got right beyond the arithmetic, and each was found by
the numbers moving when they should not have.

**The `*` and `**` footnotes name different tables.** They had been one boolean
meaning "this figure is a component", which was fine while both were declined.
`*` is §11.6.1, transcribed; `**` is §11.6.2, Fetch Immediate Effective Address,
which is not. Collapsed, a `**` row would have been priced off the wrong page —
a plausible number, wrongly sourced. They are now separate values and the `**`
rows still decline.

**The exposure rule was being applied to rows it was derived to exclude.** The
test named those rows; the step did not, so `DBcc` and `BSR` were being charged
`2(NCC − CC)` by a formula whose derivation assumes a single-word instruction
that does not change flow. The applicability is now data on the row —
`ap_m68030_prefetch_class_t` — because it belongs where the figure is *used*,
not only where it is checked. Three classes, each following from the
instruction's length in words and whether it changes flow rather than from any
figure:

| Class | Fetches, even vs odd alignment | Exposure |
| --- | --- | --- |
| single word, no change of flow | 1 vs 0 | `2(NCC − CC)`, which comes to 0 or 2 |
| even word count, no change of flow | alike | `NCC − CC`, nothing being averaged |
| odd count ≥ 3, or any change of flow | differ, or the target decides | declined |

**A pipe refill is not the row's own prefetch.** Substituting the published
exposure for *measured* instruction-bus time is only valid for the one cycle
that keeps a full pipe full. An instruction that ran more than one is refilling
a pipe some change of flow emptied, and §11.6 charges that refill to the branch —
`Bcc` taken is 6 clocks against an untaken byte branch's 4 for exactly that
reason. Substituting there made the refill vanish twice over: the target
discarded it, and the branch that caused it is a change of flow, which declines.
The step now charges a refill where it happens, at what it measured.

That does put the cost on the target instruction rather than on the branch, and
it is worth being explicit that this is a *shift between adjacent instructions*
and not a change to the total — which is precisely what §11.3.3 says alignment
does, and why it says the pair is stable at 16 clocks while the individual
instructions are 8 or 10. A per-instruction figure from this core is not
comparable to a published one; a sequence is.

### What is still open

- The **full-format extension word rows** of §11.6.1 and §11.6.3, without which
  nothing composes over a memory indirect mode.
- §11.6.2, Fetch Immediate Effective Address, which the `**` rows need.
- The **change-of-flow rows' prefetch cost**, declined above. Their warm figures
  are exact and their cold ones a lower bound.
- The rows §11.6 marks `+`, whose figures are maxima: the four divides, already
  `PROVISIONAL`.

## §11.6.1's full-format rows, and the ambiguity in them

Read off the page image rather than the text extraction, which is what the next
step of the composition needs and which turned up a defect on the way.

### A transcription defect, found by reading the page

Five rows of §11.6.1 carry `p = 1` in their **no-cache** column and had been
transcribed as zero: `(d16,An)` and `(xxx).W` are `4(1/0/0)` cached against
`4(1/1/0)` uncached, the brief-format `(d8,An,Xn)` is `6(1/0/0)` against
`6(1/1/0)`, and both immediate rows are `(0/0/0)` against `(0/1/0)`.

Their totals are equal in the two columns, which is the point: an effective
address calculation has enough microcode to hide its own extension word's fetch.
A `p` of zero says something different — that there is no fetch to hide — and
that is the reading a text extraction rendering `4(1/1/0)` as `4(1/010)` had
left behind. Corrected.

### The ambiguity, which is not ours to resolve from the page

The full-format table is in two groups. The first names its rows with `d16,An`
spelled out; the second uses `B`, defined by the table's own footnote as "Base
Address; 0, An, PC, Xn, An + Xn, PC + Xn. Form does not affect timing", with a
note that "Xn cannot be in B and I at the same time".

Every **memory indirect** row agrees between the two groups — `([d16,An])` and
`([B])` are both `10(2/0/0)`, `([d16,An],d16)` and `([B],d16)` both `12(2/0/0)`,
and so on down the table. The two groups differ only in head, 2 against 4, and
in the rows with no indirection:

| Row | Head | I-Cache | No-Cache |
| --- | --- | --- | --- |
| `(d16,An)` or `(d16,PC)` | 2 | `6(1/0/0)` | `7(1/1/0)` |
| `(d16,An,Xn)` or `(d16,PC,Xn)` | 4 | `6(1/0/0)` | `7(1/1/0)` |
| `(B)` | 4 | `6(1/0/0)` | `7(1/1/0)` |
| `(d16,B)` | 4 | `8(1/0/0)` | `10(1/1/0)` |

If `B` may be a plain `An`, then `(d16,B)` and `(d16,An)` describe the same
addressing mode and cost 8 and 6. If `B` means specifically a base that includes
an index, then `(d16,B)` and `(d16,An,Xn)` describe the same mode and cost 8 and
6. Either reading makes one pair of rows contradict, and the footnote's "form
does not affect timing" rules out the obvious escape.

**So the rows are transcribed and the *mapping from encoding to row* is not.**
Guessing it would put a two-clock error on every full-format effective address,
in a direction no test here could see — which is precisely the shape of mistake
this document exists to record rather than repeat.

### What the sibling manual and the web add

Both were checked before any thought of measuring, which is the order
`CLAUDE.md` now requires.

**The web** (an Amiga-era transcription of the same table, and ManualsLib's scan
of the manual itself) reproduces the figures exactly as read off the page here —
so the transcription is confirmed by a source independent of our reading — and
carries neither the footnotes nor any explanation of the notation.

**The `MC68020 User's Manual` §9.2.1 is the more useful check**, and it is the
step that should have come first. It has the same table with the *same* `B` and
`I` footnotes and the same note that "Xn cannot be in B and I at the same
time" — and, decisively, **no `([d16,An])` family at all**. Its memory indirect
rows are written only in the general `([B],I)` form:

| Row | 68020 cache case | 68030 cache case |
| --- | --- | --- |
| `(d16,An)` | 5 | 6 |
| `(d8,An,Xn)` | 7 | 6 |
| `(d16,An,Xn)` | 7 | 6 |
| `(B)` | 7 | 6 |
| `(d16,B)` | 9 | 8 |
| `(d32,B)` | 13 | 12 |
| `([B],I)` | 12 | 10 |

So the `d16,An` rows are a **68030 addition**: a fast path for the full-format
encodings whose base is a plain `An` or `PC`, which the 68020 did not have and
which is why its table needs only one group. That is a real finding about the
part, and it explains why the 68030's table has two groups at all.

What it does *not* do is say which encoding selects which group, because the
68020 never had to make the distinction. On the 68020 the two readings collapse:
`(d16,An,Xn)` and `(B)` cost the same 7 clocks, so it does not matter whether an
index in the base and an index outside it are the same row. On the 68030 they
are 6 against 6 for one pair and 6 against 8 for the other, and the difference
is live.

### Where that leaves it: measurement, and exactly which one

The documents are exhausted — the part's own manual is ambiguous, the sibling
manual predates the distinction, and the web only reproduces the figures. That
is the point at which measuring is the right move rather than the lazy one, and
`FINDINGS.md` is where it belongs.

The experiment is three readings through `steptime.lua`, which already
side-loads an instruction and reports the interval between steps:

1. `(d16,An)` encoded in the **full format** — mode 110, full-format extension
   word, word base displacement, index suppressed. The two candidate rows give
   6 and 8.
2. The same with the index **not** suppressed, which is `(d16,An,Xn)` or
   `(d16,B)` depending on the reading: 6 against 8 again.
3. A **null** base displacement, `(B)`, as a control — both readings give 6, so
   a disagreement there would mean the whole transcription is wrong rather than
   the mapping.

Until then the memory indirect rows — where the two groups agree, so there is
nothing to resolve — are the transcribable half, and they are the half the
composition needs first.
