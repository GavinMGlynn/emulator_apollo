# M68000 Family Reference — walk coverage record

`M68000_Family_Reference_1988.pdf`, 608 pages. The last document in
`COMPLETION_PLAN.md`'s second processor-manual batch.

| Tag | File | Pages | Text layer | State |
| --- | --- | --- | --- | --- |
| `[FAMREF]` | `motorola/M68000_Family_Reference_1988.pdf` | 608 | **scanned, OCR** | **§1-§4 walked, 242/608** |

## The citation audit: genuinely zero

One grep of `src/`, `docs/` and `tools/` for both a bracket tag and the full
title returns **one** hit, and it is the plan item that opened this walk. Every
other document in this batch had its premise reversed by that check; this one
is what the check looks like when the answer really is nothing.

*Method*: `pdfimages -list` shows full-page **400 dpi JBIG2** scans, so page
images throughout. `pdffonts` reports non-embedded Times and Helvetica, which
is exactly the `[851]` trap the other walk records warn about -- it looks
born-digital and is not.

## What it is

A **databook**, as the plan item established from the contents before any page
was read: a selector guide, technical summaries and abridged datasheets for the
whole family, development systems, and mechanical data. Its value here is as a
*cross-check on transcription* rather than as independent evidence -- where a
summary agrees with `[030]`, `[851]` or `[881]`, the shared-source rule makes
that one witness, not two.

### Section map

| § | Title | PDF pages | State |
| --- | --- | --- | --- |
| 1 | Introduction / Family Overview | 8-13 | **walked 6/6** |
| 2 | Selector Guide | 14-18 | **walked 5/5** |
| 3 | Microprocessors | 19-158 | **walked 140/140** |
| 4 | Coprocessors (MC68851, MC68881, MC68882) | 159-244 | **walked 86/86** |
| 5 | DMA / peripheral controllers | 245-~318 | owed |
| 6 | Data communications | ~319-384 | owed |
| 7 | Networking | 385-492 | owed |
| 8 | General-purpose peripherals | 493-~570 | owed |
| 9-11 | Development systems, mechanical, ordering | ~571-608 | owed |

## §3's yield: six corroborations, and every one of them lands on this session

The expectation was that the MC68030 summary would restate `[030]` and the
MC68000/`HC000`/`008`/`010` summaries would be architecture this core already
implements as a superset. That held. What it also carries is **five and six
witnesses for two facts this session changed the code over**:

- **The `RESET` instruction asserts for 512 clocks.** The MC68030's own AC
  table here, spec 56, prints "`RESET` Pulse Width (Reset Instruction) — 512
  Clks" at 16.67, 20 and 25 MHz; the **MC68020's** spec 56 prints the same at
  12.5 through 33.33 MHz. That is a fourth and fifth document beside `[030]`
  §11.6.17's `518(0/0/0)`, `[PRM]`'s "512 ... clock periods" and `[030EC]`'s
  own spec 56 — and the 68020's is the one that covers the low speed grades
  neither of the others does.
- **The arbitration lock.** Spec 35 is "`BR` Asserted to `BG` Asserted **(`RMC`
  Not Asserted)**" on *both* the MC68020 and the MC68030 pages here, which is
  `[030]` §7.7.1/§11.9/§12.1.2 and `[030EC]` spec 35 stated a fifth and sixth
  way. `ap_board_set_processor_rmc` was wired up for this rule.
- **The MC68881/MC68882 interoperation notes.** Note 8 and note 14 on the
  MC68030 page, and notes 8 and 11 on the MC68020's, exist so a designer can
  qualify the FPU's `CS` with `AS` and still meet spec 8B, and so the processor
  meets spec 13A — the same pair `[030EC]` carries. Three documents, one
  guarantee.
- **Specs 62 and 63 for `STATUS`/`REFILL`**, and spec 47A's asynchronous setup
  on `MMUDIS`/`CDIS`, with Figure 16 drawing all four. These are the emulator
  support signals `[030]` §5 and the `MC68030EC` walk both record as not
  modelled; this is a third place their timing is printed.
- **Table 2-1's speed grades**, which are the version note the `MC68030EC` walk
  opened. This 1988 databook lists the **MC68030 at 16, 20, 25 and 33 MHz** and
  the **MC68882 at 16, 20, 25 and 33 MHz**, where `[030]` §14.1's ordering
  information stops at 33.33 and drops the 16, and `[881]` §13's stops at 25
  with the 25 "available 1st quarter 1988". Every manual's ordering section
  lags its own part, in both directions, and this is the fourth instance.
  *It disagrees with itself*: the MC68030 summary's own feature list on 3-108
  says "Selection of Processors Speeds: **16.67 and 20 MHz**", against Table
  2-1's four grades in the same book.
- **`[881]` §3.4's 67-bit intermediate arithmetic unit**, printed in Table 2-1's
  MC68881 row as a headline feature. Second witness for the format
  `ap_m68882_arith.c` computes in.

### Confirmed, not changed

The MC68030 summary restates the cache organisation (16 blocks of four long
words, tag from the upper 24 address bits plus `FC2` for the instruction cache
and all three function code bits for the data cache, write-through with no write
allocation), the MMU's 22-entry fully-associative ATC, the transparent
translation registers, the five MMU instructions, the burst mechanism's
`CBREQ`/`CBACK`/`STERM` handshake, and Table 4's coprocessor primitives — the
last including **Take Post-Instruction Exception**, which the MC68030 accepts
and which `PROJECT_STATUS.md` records neither the MC68881 nor the MC68882 ever
issues. All of it is in `M68030_WALK.md` already.

The MC68020's summary is the one item here the project does not otherwise hold
(the full `[020]` is deferred to Phase 2b). Its Table 2 lists `CALLM` and `RTM`,
which the MC68030's Table 2 does not — the difference `ap_cpu_features`'
`has_module_calls` expresses, now with both tables side by side in one book.


## §4's yield: an independent check on this session's `[881]` work, and one error

### The MC68882 datasheet states today's exception-handler contract, in three lines

Page 4-71's **MC68881 COMPATIBILITY** section is the most useful page in this
book. It gives, as a numbered list, exactly the three things `[881]` §6.4 and
§7.5 were walked to establish:

> "floating-point exception handlers must have these minimum requirements:
> 1. An **FSAVE** must be executed before any other floating-point instruction.
> 2. A **BSET** or similar instruction that sets **bit 27 of the BIU flag word**
>    (located in the saved idle state frame).
> 3. An **FRESTORE** instruction must be executed before the RTE instruction.
> ... For interrupt handlers that have floating-point instructions, only
> requirements #1 and #3 must be implemented."

Requirement 1 is `save_negated_exc_pend`, requirement 2 is the frame's bit-27
record, and requirement 3 is the restore reading it back -- a second document,
stating in three lines what took `[881]` five separate passages.

The same page gives the frame difference the `$1F38` finding rests on: "the idle
and busy state frames ... are **32 bytes larger** with the MC68882 than the
MC68881. The offsets for the exceptional operand, the operand register word, and
the BIU flag word from the top of the saved idle state frame are 32 bytes more
... However, a **unique format word** is generated by the MC68882 enabling the
system software to detect this difference. The unique format word **prevents a
saved MC68881 context from being restored into an MC68882 and vice versa**."
That is `$28`/`$34`/`$38` against `$08`/`$14`/`$18`, and it is the reason
`ap_m68882_frame_length` refuses a `$vv18` frame.

### It gets the CIR map wrong, in exactly the way this core's header predicts

`ap_m68882_cir.h` warns that the coprocessor register map is "the kind of thing
a map transcribed without its footnote gets wrong", because Table 7-2's footnote
excludes the operation word and operand address CIRs -- "since they are not used
by the MC68881, they are not implemented".

**Both of this book's CIR tables do exactly that.** The MC68881's Table 8 and
the MC68882's Table 3 each list `111xx $1C 32 R/W Operand Address` with no
footnote, while marking `$08` "(Reserved)" -- inconsistent even within one
table. `[881]` Table 9-1 marks `$1C` "Not used by the MC68881 or MC68882"
outright. The warning was written before this document was read, and here is a
real example of the failure it describes.

### Table 4 is an independent transcription of the timing this session added

The MC68881's Table 4 prints the same numbers as `[881]` Table 8-2, from a
different typesetting: `FADD` 51, `FDIV` 103, `FSIN` 391, `FSQRT` 107,
`FMOVECR` 29, `FMOVE to FPn` 33. `ap_m68882_timing.c` transcribes the **68882's**
column (Table 8-3), which runs three to five clocks higher for almost
everything -- `FADD` 56, `FSIN` 394, `FMOVECR` 32 -- and that consistency across
two documents is the cross-check the transcription otherwise lacked.

**One row breaks the pattern in the other direction, and it is the interesting
one**: `FMOVE to FPn` is **33** on the MC68881 and **21** on the MC68882. Two
`[881]` tables agree (Table 5-7 and Table 8-3), and the datasheet explains it --
"these FMOVE instructions execute twice as fast as the corresponding FMOVE
instructions of the MC68881. The FMOVE instructions are also potentially fully
concurrent". `FMOVE` is the one instruction Table 5-5 marks fully concurrent, and
this is that fact as a number.

Table 5 (`FMOVE FPcr`/`FMOVEM`), Table 6 (conditionals: `FBcc` taken 18/20/23,
`FNOP` 16/18/19, `FTRAPcc` 36/39/47) and Table 7 (`FSAVE`/`FRESTORE`) are the
MC68881's versions of `[881]` Tables 8-6, 8-7 and 8-8, and agree with them.

### The performance claim is inflated in §1 and accurate in §4

§1's family overview says the MC68882 has "**more than twice** the
floating-point performance of the MC68881". The MC68882's own datasheet says
"**in excess of 1.5 times** the performance of the MC68881", and `[881]` Table
8-5's worked example measures **593/331 = 1.79**. The datasheet is the honest
number and the overview is marketing -- worth recording because
`ap_m68882_timing.h` charges the *sum* of a sequence's totals and names the
concurrency as its largest `PROVISIONAL`, so the size of that gap matters.

### Confirmed, not changed

- **Figure 8's MC68882 block diagram** draws §5's concurrency as hardware: the
  **conversion unit** owns the operand CIR and the S/D/X format conversion
  logic, sitting between the BIU and the APU. That is why `FMOVE` is fully
  concurrent and why a NAN, unnormalized or denormalized operand has to be
  handed to the APU -- the fold this session added, seen from the silicon.
- **Table 2's thirty-two conditional predicates**, split into the sixteen that
  "do **not** set the BSUN bit under any circumstances" and the sixteen that do.
  That split is predicates `$00-$0F` against `$10-$1F`, which is
  `ap_m68882_evaluate_condition`'s `predicate & 0x10` rule stated a fourth way.
- **The programming model** field for field: FPCR's enable byte at 15-8 and mode
  byte at 7-0, FPSR's condition code at 31-24 / quotient at 23-16 / exception at
  15-8 / accrued at 7-3, the four rounding modes, the four precisions with `11`
  reserved, and the accrued byte's five bits.
- **"Forty-six instructions, including 35 arithmetic operations"** -- the back
  cover's count with its breakdown, matching the 25 monadic + 10 dyadic of
  `[881]` Tables 5-2 and 5-3.
- **The MC68851's six `BERR` conditions** (page 4-10), which `M68851_WALK.md`
  builds the protection gap around, listed again as a numbered six.
- **Spec 35A/35B, "`PBR` Asserted to `PBG` Asserted (`RMC` Not Asserted)"** on
  the MC68851 -- a seventh witness for the arbitration lock.
- **Specs 25 and 27 in clock units** on both FPU datasheets, and note 3's
  "synchronous read cycles occur **only** when the save or response CIR
  locations are read" -- `[881]` §12.6's notes, twice more.

### A second internal inconsistency

The MC68882's electrical section here lists three speed grades (16.67, 20, 25)
where §2's Table 2-1 lists four (16, 20, 25, **33**) -- the same shape as the
MC68030's disagreement with itself, in the same book.

## Owed

§5 through §11, PDF 245-608.
