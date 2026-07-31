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
