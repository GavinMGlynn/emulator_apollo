# M68000 Family Reference — walk coverage record

`M68000_Family_Reference_1988.pdf`, 608 pages. The last document in
`COMPLETION_PLAN.md`'s second processor-manual batch.

| Tag | File | Pages | Text layer | State |
| --- | --- | --- | --- | --- |
| `[FAMREF]` | `motorola/M68000_Family_Reference_1988.pdf` | 608 | **scanned, OCR** | **WALKED WHOLE, 608/608, 2026-09-07** |

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
| 5 | DMA controllers (MC68440/68442/68450) | 243-300 | **walked 58/58** |
| 6 | Data communications (MC2681, MC68652, **MC68681**) | 301-384 | **walked 84/84** |
| 7 | Network devices (MC68184/68194/68824/68605/68606) | 385-492 | **walked 108/108** |
| 8 | General-purpose peripherals (MC68153, MC68230, MC68452, MC68901) | 493-558 | **walked 66/66** |
| 9 | Mechanical data | 559-578 | **walked 20/20** |
| 10 | Technical support | 579-590 | **walked 12/12** |
| 11 | Development systems | 591-608 | **walked 18/18** |

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

## §5 is what the plan predicted; §6 is what the plan got wrong

**§5, the DMA controllers**, is the MC68440/MC68442 and the MC68450 -- Motorola
parts the DN3500 does not have, its DMA being an Intel 8237 with its own walked
datasheet. Fifty-eight pages, nothing implementable, exactly as the item
predicted.

**§6 is not.** The plan characterised the peripheral sections as "datasheets for
Motorola peripherals this machine does not have", and named five. It missed the
sixth: §6 opens with the **MC2681 DUART** at 6-1 and carries the **MC68681
DUART** at 6-52 -- and the DN3500's serial ports *are* a 68681. `ap_mc68681.c`
is a real module in this core with its own walk record.

That is the `[8259]` lesson exactly: a range dismissed as belonging to another
part turns out to belong to this one. The yield is in `SCN2681_WALK.md`, and it
**closes that record's opening finding**: Motorola's "functionally equivalent
... with some minor differences" never names the differences, and this book
names them, by printing both datasheets fifty-five pages apart with the same
table numbering.

The headline is the **`0x0C` divergence**, which `COMPLETION_PLAN.md` had closed
by measurement (zero accesses at register 12 over 350 M instructions) and which
now has a documentary source: MC68681 Table 2 gives `1100` as the
**Interrupt-Vector Register** in both directions, MC2681 Table 2 gives it as
**Do Not Access** in both. One vendor, one book, two maps. Also named there: the
bus interface (`R`/`W` strobes and no `DTACK` against `R/W` + `DTACK`), the
`IACK` pin, the input-port width, and a `Bit Set`/`Bit Reset` typo -- plus the
reason the differences *look* unnamed, which is that the abridgement is not
clean and four of the MC2681's own tables still describe the MC68681.

Nothing to implement: `ap_mc68681.c` already matches both baud-rate sets code
for code, the `$0F` IVR reset value, the input port's bit-7-reads-one rule, and
the 38.4 kHz two-sample change-of-state mechanism it declines to model.

§6 also carries the **MC68652/MC2652 MPCC**, a synchronous multi-protocol
controller this machine does not have.

## §7 through §11: nothing to implement, and two things worth keeping

**§7, network devices**, is 108 pages of IEEE 802.4 token bus -- the MC68184
broadband interface controller, the MC68194 carrierband modem, the MC68824
token bus controller -- plus the MC68605 X.25 protocol controller and the
MC68606 multi-link LAPD protocol controller. The DN3500's networking is a 3Com
3c505 Ethernet card and the Apollo Domain ring, neither of which is any of
these, and both of which have their own walked documents. Nothing.

**§8, general-purpose peripherals**, is the MC68153 bus interrupter module, the
MC68230 parallel interface/timer, the MC68452 bus arbitration module and the
MC68901 multi-function peripheral. This machine's equivalents are all Intel or
Signetics parts with their own walk records. Nothing.

**§9 mechanical data** and **§11 development systems** are packages, in-circuit
emulators and part numbers. Two things are worth keeping from them:

- **§11.3.3's MC68030 emulator signal lists** name `MMUDIS`, `CDIS`, `CBREQ`,
  `CBACK`, `STERM`, `CIOUT` and `CIIN` among the signals the emulator can
  substitute or drive. That is the `MMUDIS`/`CDIS` plan item's own subject seen
  from the other side -- these are the emulator-support inputs this core does
  not model, and this is the fourth document to print what a system that *did*
  drive them would look like.
- **§10's Table 10-1, *MPU Product Literature*, is a bibliography**, and it
  settles a question this project could otherwise only assume: the MC68882's
  user's manual is listed as **`MC68881UM/AD`** -- the *same* document as the
  MC68881's. There is no separate MC68882 user's manual, so
  `MC68881_MC68882_..._Users_Manual_1ed_1987.pdf` is the whole of it and nothing
  is missing. The table also gives `MC68030UM/AD`, `MC68851UM/AD` and
  `MC68020UM/AD`, which are the three other manuals this project holds, and the
  `BRxxx/D` technical-summary numbers for the abridgements §3 and §4 reprint.

## What the whole document was worth

**Zero implementable facts, and that is the correct outcome** for a databook
whose content is either an abridgement of a manual already walked whole or a
datasheet for a part this machine does not have. The shared-source rule says as
much in advance.

Its value was elsewhere, and there was more of it than expected:

1. **It named the MC68681/MC2681 differences**, closing the opening finding of
   `SCN2681_WALK.md` -- see §6 above. That is the `[8259]` lesson repeating: the
   plan characterised these sections as parts this machine lacks, and §6 turned
   out to hold the DUART it has.
2. **It gave a second, independent statement of this session's `[881]`
   exception-handler work**, as a three-item list on one page.
3. **It cross-checked `ap_m68882_timing.c`'s transcription** against a
   differently typeset printing of the same tables.
4. **It supplied a fourth and fifth witness for the `RESET` instruction's 512
   clocks and a fifth, sixth and seventh for the `RMC` arbitration lock** --
   both facts this project changed code over in the same week.
5. **It demonstrated the failure `ap_m68882_cir.h` warns about**, by printing a
   CIR map without its footnote. Twice.
6. **It closed the "is there a separate MC68882 manual?" question** by
   bibliography.

## Owed

Nothing. **608/608.**
