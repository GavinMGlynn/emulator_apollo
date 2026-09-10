# `AEGIS Overview` (1985) — walk coverage record

| Tag | File | Pages | Text layer | State |
| --- | --- | --- | --- | --- |
| `[AEGISOV]` | `bitsavers/AEGIS_Overview_1985.pdf` | 436 | born-digital, heavy OCR damage | **TRIAGED, with two pages read** — not a hardware source, on the evidence below |

Ranked third on `CONTENT_TRIAGE.md`'s hardware-term list — 59 hits across 357 K
characters — and opened on that ranking.

## It is a slide deck, and the measurement says so

**Every page was scored** for `register | MC680xx | 68000 | 68010 | 68020 |
hardware | MMU | interrupt | DMA | bus`. The **densest page in 436 scores
seven**, and three of the top five are the **bibliography** — Multics, Eden,
System/38, the VAX-11/780 Hardware Handbook. A reference manual does not look
like that; a course deck citing its influences does.

The pages themselves confirm it: a slide is a title, five bullets and an
arrow. `AEGIS_Internals_and_Data_Structures` — walked, 426 pages — is the same
material with the data structures drawn byte by byte, and this project already
has it.

**So this is recorded as triaged rather than walked**, and the basis is a
measurement over the whole text rather than a judgement about the title. It is
reopenable: the scan's vocabulary is in `CONTENT_TRIAGE.md`.

## The two pages that were read, and what they add

**p. 104, the MMU**, gives the lookup as a function — `(Virtual Address, ASID,
Operation) → Physical Address` — with three outcomes: a hit, a **protection
violation**, or **not found**, which goes "on to the MST". Operations are
**Read, Write, Execute**. That is `[AEGIS]` §10.7 and §13's fault path in one
slide, and it names the ASID as an *input to the lookup*, which is the other
half of §9.3's "a hardware global bit in the MMU hardware page tables" — ASID 0
being the value that makes a page global.

**p. 175, interrupt handling**:

> "Interrupts vector **directly to driver** — no special interrupt queueing or
> dispatching mechanism. Most interrupt handlers are **very simple** — just
> advance an eventcount and return — actual interrupt processing done by driver
> in requesting process."
>
> "`PROC1_$INT_ADVANCE` — jump to here to advance an eventcount and return from
> an interrupt. Push all registers on stack, plus eventcount address. **Must be
> done in assembly language.**"

**This is the behavioural half of a figure `[GPIO]` Appendix D gives as a
number.** That appendix times "device interrupt to the first instruction of the
interrupt routine" at 125 µs and the system handler's own eventcount advance at
260 µs with nobody waiting, 325 µs with someone. Here is what those microseconds
are *doing*: no queue, no dispatcher, a jump to a common assembly routine that
pushes the register set and advances a count. **Two documents, one mechanism,
and neither states both halves.**

Neither page changes anything in this core, which models the hardware below all
of it.

## What is owed

Nothing, unless the triage is overturned. **If it is, the place to start is the
crash-dump and OS-module-code material around pp. 347-364** — `OS module codes`,
`level 1 eventcounts`, and a dump listing showing `REWUN[D] TIMESLICE = 764` —
which is the only part of the deck that prints machine state rather than
describing it, and therefore the only part a reader of a DS5500 dump might want.
