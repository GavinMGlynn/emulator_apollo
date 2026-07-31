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
