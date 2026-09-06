# M68000 Family Programmer's Reference Manual — walk coverage record

The instruction set this project implements, for every part in it.

| Tag | File | Pages | Text layer | State |
| --- | --- | --- | --- | --- |
| `[PRM]` | `motorola/M68000_Family_Programmers_Reference_Manual_1992.pdf` | 646 | **born-digital** | audit done 2026-09-06; **WALKED WHOLE, 646/646**, 2026-09-07 |

## The audit, and it reversed the item's premise for the third time

`COMPLETION_PLAN.md` had this document at "**cited 9 times**". It is cited
**nine times by the `[PRM]` tag and twenty-nine more by its full title**, across
more than twenty modules — `ap_m68030_alu`, `_arith`, `_branch`, `_category`,
`_cond`, `_ea`, `_immediate`, `_move`, `_opcode`, `_quick`, `_shift`, `_single`,
`_operand`, `_bounds`, `_control`, `_addr`, `_coproc`, `_mmusr`, `_tt`, and
`m68882/_accuracy`. Essentially the whole instruction set derives from it.

**This is the third time the same check has reversed the same kind of claim**,
and the second time *after* the lesson was written down. `[Bt458]` taught it:
"counting tags is a first pass, never a verdict — grep the full title too."
`[881]`/`[851]` were mis-stated as "zero citations" one commit later. And the
`[PRM]` item said nine. The `§`-shaped and tag-shaped greps keep finding the
same fraction of the truth.

### Section map

| § | Title | PDF pages | Doc pages | State |
| --- | --- | --- | --- | --- |
| 1 | Introduction | 12-41 | 1-1 to 1-30 | **walked 30/30, 2026-09-06** |
| 2 | Addressing Capabilities | 42-71 | 2-1 to 2-30 | **walked 30/30, 2026-09-06** |
| 3 | Instruction Set Summary | 72-104 | 3-1 to 3-33 | **walked 33/33, 2026-09-06** |
| 4 | Integer Instructions | 105-302 | 4-1 to 4-198 | **walked 198/198, 2026-09-06** |
| 5 | Floating-Point Instructions | 303-454 | 5-1 to 5-152 | **walked 152/152, 2026-09-07** |
| 6 | Supervisor (Privileged) Instructions | 455-540 | 6-1 to 6-86 | **walked 86/86, 2026-09-07** |
| 7 | CPU32 Instructions | 541-556 | 7-1 to 7-16 | **walked 16/16, 2026-09-07** |
| 8 | Instruction Format Summary | 557-596 | 8-1 to 8-40 | **walked 40/40, 2026-09-07** |
| A | Processor Instruction Summary | 597-627 | | **walked, 2026-09-07** |
| B | Exception Processing Reference | 628-640 | | **walked, 2026-09-07** |
| C | S-Record Output Format | 641-646 | | **walked, 2026-09-07** |

**There is no Appendix D and no Appendix E**, which matters: see the citation
defect below.

## Method: this document is born-digital, and that changes the right way to read it

`CLAUDE.md` requires page images rather than text extraction, and gives the
reason: "OCR mangles precisely what timing and register tables are made of, and
`4(1/1/0)` arriving as `4(1/010)` reads as plausible data." That reason is about
**OCR error**, and this document has no OCR in it.

`pdffonts` shows embedded Type 1 faces — Helvetica, Times, Courier, Symbol — and
`pdfimages -list` finds only small raster figures (288×154 diagrams). The text
layer *is* the document's own text, the same glyph codes the renderer draws, not
a recognition of a picture of them. The failure mode the rule guards against
does not exist here.

**Validated against a known answer before being relied on.** `pdftotext -layout`
on Table 2-2 (*IS-I/IS Memory Indirect Action Encodings*) reproduces all
fourteen rows in the right columns, and they match what the `[030]` walk read
from that manual's page image for the same table, trap included.

**Two residual risks, and both are handled by rendering:**

1. **Superscripts and subscripts are lost.** `[PRM]` §1.7.2's bit-field range
   prints as "values between 2 – 31 to 231 – 1" in the extraction and is
   −2³¹ to 2³¹−1 on the page. Anything with an exponent gets the image.
2. **Column alignment can mislead.** Every table below that yielded a finding
   was rendered and read as an image before the finding was recorded.

So: `-layout` text for prose and structure, page images for figures, for
anything with exponents, and for every table a claim rests on. Faster, cheaper
and — here — more accurate than rendering 646 pages.

## §1, INTRODUCTION — WALKED, 30/30, 2026-09-06

The programming models (integer, floating-point, supervisor), the CCR bit
definitions, FPCR/FPSR field layouts, the data types and the memory organisation
of every operand format.

### Table 1-5 carries four slips, three of them copied from Table 1-4

*Double-Precision Real Format Summary*, doc p. 1-22, **read as a page image**
because these are the kind of claim that must not rest on an extraction:

| Row | Printed | Should be | Evidence |
| --- | --- | --- | --- |
| NANs → Biased Exponent Format Maximum | `255 ($7FF)` | `2047 ($7FF)` | 255 is single precision's; the *Signed Infinities* row three lines above prints `2047 ($7FF)` |
| Signed Zeros → Biased Exponent Format Minimum | `0 ($00)` | `0 ($000)` | an 11-bit field; the *Denormalized Numbers* row two lines above prints `$000` |
| Approximate Ranges → Maximum Positive Normalized | `18 x 10³⁰⁸` | `1.8 × 10³⁰⁸` | a lost decimal point; 18e308 is not representable |
| Denormalized → Relation to Representation | `(–1)ˢ × 2–¹⁰²² × 0.f` | `(–1)ˢ × 2⁻¹⁰²² × 0.f` | the minus is set before the superscript rather than inside it, unlike the normalized row above |

Three of the four are Table 1-4's values left behind when the single-precision
table was copied to make the double-precision one.

**This core is structurally immune to the first, which is worth more than
catching it.** `ap_m68882_format.c` converts single and double through one
`from_ieee` helper that takes the field widths as parameters and computes
`max_exponent = (1 << exponent_bits) - 1`, so double precision gets 2047 because
it has eleven exponent bits, not because anyone transcribed a number. The
helper's own comment says why it is shared: "writing them twice is how one of
them ends up with the other's bias" — which is exactly the mistake the manual
made.

### Captured

- **§1.1**, the integer user model, and **§1.1.4**'s CCR: five bits, `X N Z V C`,
  with the definition of each and why `C` and `X` are separate ("to simplify
  programming techniques that use them").
- **§1.2**, the floating-point user model: Figure 1-3's FPCR (`ENABLE` byte
  `BSUN SNAN OPERR OVFL UNFL DZ INEX2 INEX1`, `MODE` byte `PREC`/`RND`, bits
  31-16 "always read as zero and are ignored during write operations" — which
  is what `test_the_control_registers_mask_their_unimplemented_bits` asserts);
  Figure 1-4's FPCC (`N Z I NAN` in bits 27-24); Figure 1-5's quotient byte
  (sign in 23, seven LSBs in 22-16, "remain set until the user clears them");
  Figure 1-6's EXC byte and the AEXC byte.
- **§1.6**'s five floating-point data types and the rule that separates
  extended precision from the other two: an extended number with a zero exponent
  may have an explicit integer bit of one and so be *normalized*, which is why
  Table 1-6's normalized range reads `0 <= e < 32767` where single and double
  read `0 < e`.
- **§1.6.5**: the FPU "never creates an SNAN resulting from an operation"; a
  disabled SNAN trap returns the source SNAN with the mantissa MSB set; and an
  FPU-created NAN "always contains the same bit pattern in the mantissa. All
  bits of the mantissa are ones for any precision."
- **Tables 1-4, 1-5, 1-6, 1-7** in full, and **Table 1-8**, the MC68040 FPU's
  hardware-vs-`M68040FPSP` data type matrix.
- **§1.7**'s memory organisation, Figure 1-21 included: bit, bit field, byte,
  word, long word, quad word, the MOVE16 16-byte block "aligned to a 16-byte
  boundary", packed and unpacked BCD.
- **§1.7.2**: bit field offsets −2³¹ to 2³¹−1, widths 1 to 32 — the same bound
  `[030]` §2.3 gives and `bitfield_spec_t.offset` carries as `int32_t`.

## §2, ADDRESSING CAPABILITIES — WALKED, 30/30, 2026-09-06

The chapter `ap_m68030_ea.h` derives the whole effective-address decode from.

### Table 2-4 is wrong in four cells, and it is not a scan artifact

*Effective Addressing Modes and Categories*, doc p. 2-20, **read as a page
image**.

`ap_m68030_category.c`'s opening comment already said the Alterable column was
"shifted in the scan", and derived the categories from §2.3's *definitions*
instead — which is why this core has never been wrong about it. **But "in the
scan" is not true and is worse than useless**: it invites a reader to go and
find a better copy. `[PRM]` is born-digital. The cells are wrong in the
document's own vector text and in every copy of it.

| Row | Alterable printed | Should be |
| --- | --- | --- |
| Absolute Short `(xxx).W` | `—` | `X` |
| Absolute Long `(xxx).L` | `—` | `X` |
| PC Memory Indirect Postindexed | `X` | `—` |
| PC Memory Indirect Preindexed | `X` | `—` |

Wrong in **both directions**, which is what a swap between two row groups looks
like. `MOVE.W D0,(xxx).L` is legal on every member of the family, and no
PC-relative operand is writable on any of them.

**The sibling manuals settle it, exactly as `CLAUDE.md`'s resolution order
predicts — and there are two of them.** `[030]` Table 2-2, *Effective Addressing
Mode Categories*, rendered and read: absolute short and long are
`Alterable = X`, both PC memory indirect rows are `Alterable = —`. And `[881]`
Table 4-10, of the same name, read on 2026-09-07 during that manual's walk,
says the same — including Absolute Long's register field as `001`.

So it is **three manuals against one**, and the one is `[PRM]`. That is what
`ap_m68030_category.c` has.

**A second, independent error in the same table**: Absolute Long's register
field is printed `000`, which is Absolute Short's on the line above. §2.2.17 on
the facing page says `001`, and `[030]` Table 2-2 says `001`.

Comment corrected in place with this evidence.

### The citation defect: `[PRM]` has no Appendix D

`ap_m68030_step.c`'s `CALLM` arm cited "`[PRM]` Figure D-1 and D-3". **`[PRM]`
ends at Appendix C** (*S-Record Output Format*); it has no Appendix D and no
Appendix E. The module descriptor and module stack frame figures are the
**MC68020 user's manual's** Appendix D, *Advanced Topics*, §D.1 *Module Support*
— which is where they belong, `CALLM` being a 68020 instruction the 68030
removed.

The facts were always taken from the right pages; only the tag was wrong. But a
reader following it would have opened a document that does not contain them and
concluded they were invented. Corrected.

*(The two "Appendix E" citations in `m68882/ap_m68882_accuracy.h` and
`m68040/ap_m68040_fpu.h` are **not** this defect: both say "the 68040 manual's
Appendix E" and mean it.)*

### Confirmed against `[030]`, and they agree

- **§2.1**: "instructions consist of at least one word; some have as many as
  **11** words", and the longest is a `MOVE` with a full extension word for both
  effective addresses "and eight other extension words". That is 1 + 10, which
  is `[030]` §2.5's "the longest instruction for the MC68030 contains 10
  extension words" from the other end. The two agree and neither is enforced —
  the bound is recorded in the `[030]` walk as an unasserted invariant.
- **Table 2-1**'s field definitions and **Table 2-2**'s sixteen `IS`/`I/IS` rows,
  including both traps this project already carries: `BD SIZE` `00` is
  **Reserved**, and `IS`=0/`I/IS`=`001` is *indirect preindexed* where
  `IS`=1/`001` is *memory indirect*.
- **Table 2-3**, immediate operand location by size — byte in the low-order byte
  of the extension word, which is the rule `fetch_fp_source`'s immediate arm
  cites for why a byte still occupies a whole word.
- **§2.2.4/§2.2.5**: the stack-pointer byte step of two, and "coprocessors may
  support incrementing for any operand size, **up to 255 bytes**" — the same
  bound `[030]` §10.4.9 states as an operand length.
- **§2.5**: the four full-extension elements (`BR`, `Xn`, `bd`, `od`), each
  independently suppressible, "at least one element must be active", a
  suppressed element having "an effective value of zero", and that the PC's
  value as a base register "is the address of the extension word".
- **§2.4**'s brief extension word compatibility: `SCALE` = 0 is the encoding
  common to every family member, which is the note `[030]` §2.7 makes.

## §3, INSTRUCTION SET SUMMARY — WALKED, 33/33, 2026-09-06

The summary tables. Their authority is §4/§5/§6's per-instruction pages, which
is worth stating because one of the two discrepancies found here is settled that
way rather than by the table.

### One confirmed error, and one question this instrument cannot answer

**Table 3-3, *Integer Arithmetic Operation Format*, `SUB`**: the operation reads
"Destination **=** Source → Destination". It is `–`. Every sibling row prints it
correctly — `CMP` is "Destination – Source", `SUBI`/`SUBQ` are "Destination –
Immediate Data", `SUBX` is "Destination – Source – X". Confirmed on the page
image, in a rendering font.

**Table 3-3, `CMP2`: "Lower Bound ? Rn ? Upper Bound" — and no instrument here
can say which glyph that is.** The Symbol font is **not embedded** in this PDF
(`pdffonts`: `emb no`), so the arrow and less-than-or-equal glyphs neither
render in a rasterisation nor survive extraction reliably. `pdftotext` renders
both as `→`; the page image shows blanks. **Recorded as a limitation, not a
finding** — evidence that cannot tell two readings apart is not evidence.

It does not matter, because CMP2's own page in §4 is unambiguous and is the
authority: "Operation: Compare Rn < LB or Rn > UB and Set Condition Codes",
in glyphs that do render.

*This is the third residual risk of the text-layer method, alongside lost
superscripts and column alignment, and it is the only one page images do not
solve.*

### Verified against this core, and they agree

- **Table 3-23, the 32 floating-point conditional tests**, complete with
  predicate encodings. `ap_m68882_evaluate_condition` matches all of it: sixteen
  equations selected by `predicate & 0xF`, so each IEEE-aware test shares its
  equation with the nonaware one four bits above — which is what the table's two
  halves show. And the BSUN column is exactly bit 4, which the code already
  states as "the rule is exactly bit 4 against the NAN condition code, with no
  special cases at all". The `UGT` equation's overbar spanning the whole
  parenthesis is the trap the code records catching by meaning rather than by
  transcription.
- **Table 3-21, FPCR encodings**: `RND` 00/01/10/11 = RN/RZ/RM/RP and `PREC`
  00/01/10/11 = Extend/Single/Double/**Undefined**. `ap_m68882_regs.h`'s
  `AP_M68882_PRECISION_*` are 0/1/2/3 in exactly that order, with the reserved
  encoding carried rather than folded into one of the three.
- **Table 3-22, FPCC encodings** for each data type, and §3.6's postprocessing:
  underflow, round, overflow in that order.
- **§3.5.2's rounding boundaries** — 24, 53 and 64 bits — which
  `ap_m68882_precision_bits` returns and quotes.
- **Tables 3-1 through 3-20**, the notational conventions and the operation
  format summaries for every instruction group.

## §4, INTEGER INSTRUCTIONS — IN PROGRESS, 2026-09-06

198 pages, 97 condition-code blocks, ~80 instruction pages. The per-instruction
authority this core's whole integer set derives from.

### The defect: a bit field must access only the bytes it spans

The note on every bit field instruction page:

> For the MC68020, MC68030, and MC68040, all bit field instructions access only
> those bytes in memory that contain some portion of the bit field. **The
> possible accesses are byte, word, 3-byte, long word, and long word with byte
> (for a 5-byte access).**

`bitfield_read` and `bitfield_write` did **one byte access per bit** — thirty-two
single-byte reads for a 32-bit field, and the write path a read-modify-write
*per bit*, so a field written across a device register read and rewrote it eight
times a byte.

**`[030]` §11.6.14 prices the correct behaviour and nothing was checking it**:
`BFTST Mem (<5 Bytes)` is `10(1/0/0)`, one operand read, and `BFTST Mem
(5 Bytes)` is `14(2/0/0)`, two. Those rows are among the ones
`ap_m68030_timing_table.c` deliberately does not transcribe, because they have a
non-zero read count — so the table that would have caught it was, correctly, not
carrying them.

Fixed by computing the touched span and asking for it once.
`ap_m68030_operand_read` already splits a span at long-word boundaries and
issues the fewest cycles, so the shapes the note lists fall out rather than
being enumerated; the five-byte span is two calls because the result is a
`uint32_t`, which is the note's own "long word with byte".

### The measurement, and the first test of it was worthless

The obvious test — assert the clock cost — **passes on the broken code**, and did.
With the data cache enabled, thirty-two byte reads inside one sixteen-byte line
cost one fill and thirty-one free hits, so the clock is almost identical either
way. A test that passes either way measures nothing.

The defect is in the number of *bus cycles*, so bus cycles are what the test
counts, with the data cache turned off so that each is visible. Measured rather
than assumed:

| | 4-byte span | 5-byte span |
| --- | --- | --- |
| span access (correct) | 2 fills | 3 fills |
| per-bit walk (before) | **33 fills** | **33 fills** |

One of those is an instruction fetch. So the correct behaviour is one operand
access and two — exactly §11.6.14's `(1/0/0)` and `(2/0/0)` — and the per-bit
walk made thirty-two either way, **unable even to distinguish the manual's
four-byte case from its five-byte one**.

*Identity boot unchanged at `42B14372F3677EE8`, and the reason is the same one
that made the first test useless: the boot's bit fields are on cached data, so
the extra accesses were free hits into a line that ends in the same state. The
change is real and the hash cannot see it — which is why the fills count is the
verification and the hash is not.*

### Checked and holding

- **`ADDQ`/`SUBQ` to an address register**: "The condition codes are not
  affected when the destination is an address register", and "the entire
  destination address register is used regardless of the operation size".
  `execute_quick` has both, plus the rule that makes `ADDQ.B #1,A0` illegal and
  the alterable-not-*data*-alterable category that lets these reach `An` at all.
- **The zero-count shift and rotate rules**, which differ between the two rotate
  forms: `ROd` clears C on a zero count, `ROXd` sets it to X, and both leave X
  alone. `ap_m68030_alu_shift`'s `count == 0` arm is exactly that, and `V` is
  raised only for the arithmetic *left* shift.
- **The multiprecision `Z` rule** — "Cleared if the result is nonzero; unchanged
  otherwise" — on `ABCD`, `SBCD`, `NBCD`, `ADDX` and `SUBX`.
- **`MOVE to/from CCR` and `to/from SR` are word operations, `TAS` a byte one**,
  where the size field is the decode escape and names no size.
  `ap_m68030_single.c` assigns each a fixed size and says why.
- **`CLR` does not read its destination** on the 68020 and later — the note says
  "In the MC68000 and MC68008 a memory location is read before it is cleared",
  and `reads_destination` excludes it.
- **`MULS`/`MULU` overflow** "can occur only when multiplying 32-bit operands to
  yield a 32-bit result", and the divides' `N`/`Z` "undefined if overflow or
  divide by zero occurs".
- **`CHK2`/`CMP2`**: "Z — Set if Rn is equal to either bound; cleared otherwise.
  C — Set if Rn is out of bounds; cleared otherwise", with N and V undefined.

## §5, FLOATING-POINT INSTRUCTIONS — WALKED, 152/152, 2026-09-07

45 distinct instructions. **A completeness check against this list is what the
section is worth**, because `PROJECT_STATUS.md` claims "every 68882 instruction
and every data format executes" and this is the list that claim is against.

**45/45.** `AP_M68882_OP_*` carries 44 of them, and the forty-fifth is not
missing:

**`FNOP` has no operation code because it is not one.** Its instruction format
is `1111 cpID 010 000000` followed by a zero word — coprocessor type `010`,
which is `cpBcc` with a *word* displacement, and conditional predicate `000000`,
which is `F`. So `FNOP` is `FBF.W *+2`, and it executes through this core's
`AP_M68030_CP_BRANCH_WORD` arm: the predicate evaluates false, the branch is not
taken, and execution continues. Which is what "Operation: None" means.

And it does the one thing the page says it is *for*. "Execution of FNOP also
forces any exceptions pending from the execution of a previous floating-point
instruction to be processed as a preinstruction exception" — and the branch types
are not in `execute_step`'s save/restore/general exemption, so a pending trap is
delivered before it. Correct, and for the documented reason rather than by
accident.

## §6, SUPERVISOR (PRIVILEGED) INSTRUCTIONS — WALKED, 86/86, 2026-09-07

### The defect: `RESET` cost nothing, and it costs 518 clocks

`[PRM]`'s `RESET` page: "Asserts the RSTO signal for **512** (124 for MC68000
...) clock periods, resetting all external devices." `[030]` §11.6.17 gives the
instruction as `518(0/0/0)` — the 512 plus six of overhead.

`ap_m68030_step.c`'s `RESET` arm incremented `external_resets` and returned,
charging **no microcode time at all**. So the machine drove the board's reset
line and resumed instantly, where the hardware holds it for ~20 µs at 25 MHz.

Fixed by adding the row to `ap_m68030_timing_table.c` beside `NOP`, `RTS`, `RTR`
and `RTD`, which are already found by their whole instruction word. It is the
one row from outside §11.6.8/§11.6.9, and the header now says why: it qualifies
under the module's own `(0/0/0)` rule, it is doubly cited, and it is not
data-dependent despite dwarfing every other entry — a fixed number of clock
periods, not a range.

*Verification: `timing_table_suite`'s encoding test extended with `$4E70`.
Identity boot unchanged at `42B14372F3677EE8` — **and the clock total is
byte-identical too**, `1408663613` before and after, which is stronger than the
hash: adding 518 clocks per `RESET` to a total that does not move by one proves
the boot executes **no `RESET` instruction** in its 350 M window. So this change
is verified by the unit test and is not exercised by the boot, and saying which
is the point.*

### `STOP`'s trace bits are transposed here, and `[030]` is right

`[PRM]`'s `STOP` page: "A trace exception occurs if instruction tracing is
enabled (**T0 = 1, T1 = 0**) when the STOP instruction begins execution."

`[030]` §8.1.7: "A STOP instruction that begins execution with **T1 = 1 and
T0 = 0** forces a trace exception after it loads the status register."

**The two bits are swapped between the manuals.** Both read as page images on
2026-09-07, so neither is an extraction artifact, and they cannot both be right.

`[030]` is. `T1:T0` = `10` is *trace on any instruction*, `01` is *trace on
change of flow*, and `STOP` is not a change of flow — only the first has any
reason to trace it. **And `[PRM]`'s parenthetical contradicts its own prose in
the same sentence**: "instruction tracing is enabled" *is* `T1 = 1`, which is
what the parenthesis then denies. The part's own manual is also what
`CLAUDE.md`'s resolution order puts first.

This core follows `[030]` and was already right — the traced-`STOP` fix landed
on 2026-08-26 walking §8. The conflict is now recorded in
`ap_m68030_step.c`'s `STOP` arm, because the next reader to check it against
`[PRM]` will find it backwards and be tempted to "fix" it.

### Verified

- **`PMOVE`'s three operand sizes**: "a quad-word (8 byte) operation for the CPU
  root pointer and the supervisor root pointer ... a long-word operation for the
  translation control register and the transparent translation registers (TT0
  and TT1) ... a word operation for the MMU status register". `pmove_size`
  returns 8, 4 and 2 for exactly those.
- **`MOVEC` is "always a 32-bit transfer, even though the control register may
  be implemented with fewer bits. Unimplemented bits are read as zeros"** — which
  is the sentence that arm already quotes.
- `MOVES`, `MOVE USP`, `RTE`, `ANDI/EORI/ORI to SR`, `FSAVE`/`FRESTORE`.
- The 68851-only instructions this part does not have — `PBcc`, `PDBcc`,
  `PFLUSHR`, `PFLUSHS`, `PRESTORE`, `PSAVE`, `PScc`, `PTRAPcc`, `PVALID` — which
  is the list `[030]` §9.6 gives and `ap_m68030_step.c` already cites p. 9-51
  for.

## §7, CPU32 INSTRUCTIONS — WALKED, 16/16, 2026-09-07

Not this machine's part, read for completeness. Table 7-1's list of MC68020
instructions the CPU32 drops is a useful negative: it contains every bit field
instruction, `CALLM`/`RTM`, `CAS`/`CAS2`, `PACK`/`UNPK` and the whole coprocessor
group — all of which the 68030 *does* have and this core implements.

## §8, INSTRUCTION FORMAT SUMMARY — WALKED, 40/40, 2026-09-07

The chapter ten citations already derive from, and it verifies all of them.

- **Table 8-2, the operation code map**: all sixteen rows.
  `ap_m68030_opcode_family_t` is that table verbatim, including the ordering that
  invites the mistake — `0001` Move **Byte**, `0010` Move **Long**, `0011` Move
  **Word** — which `ap_m68030_opcode_move_size` keeps as an explicit mapping
  "because there is no arithmetic that produces it".
- **Table 8-1, the conditional predicate field**: the same 32 encodings as
  Table 3-23, and the same ones `ap_m68882_evaluate_condition` decodes.
- **§8.1.4's source specifier field**: `000` L, `001` S, `010` X, `011` P, `100`
  W, `101` D, `110` B. `ap_m68882_format_t` is those values exactly, with `111`
  carrying the dynamic packed form the 68881/68882 adds.
- **§8.1.7's shift and rotate fields**: `dr` 0 right / 1 left, `i/r` 0 immediate
  / 1 register, and a count field where "a zero specifies 8" — the rule
  `ap_m68030_quick.h` states for the quick data field and the shift decode
  applies here.
- **§8.1.8's size field**: `00` byte, `01` word, `10` long, and `11` unlisted
  because it is the decode escape — which is what `ap_m68030_single.c` and
  `ap_m68030_quick.h` both rely on.
- The binary format of every instruction in the family, which is what the rest of
  the chapter is.

## APPENDICES A, B AND C — WALKED, 50/50, 2026-09-07

- **Table A-8, the MC68030 instruction set**, and **Table A-9, its 18 addressing
  modes.** This core's `ap_m68030_ea_kind_t` has twelve values, not eighteen, and
  the difference is structural rather than a gap: Table A-9 counts
  `(d8,An,Xn)`, `(bd,An,Xn)`, `([bd,An],Xn,od)` and `([bd,An,Xn],od)` as four
  modes where all four share mode field `110`, and the four PC forms likewise
  share `111`/`011`. Which of the four an encoding names lives in the *extension
  word*, and `ap_m68030_ea.h` decodes that separately — the same split Table 2-4
  makes.
- **Table B-1, the exception vector assignments for the whole family**, and
  `ap_m68030_exception.h` matches it for every vector the 68030 has. The three it
  does not carry are all correctly absent: **55** is "FP Unimplemented Data Type
  (Defined for MC68040)", and **57** (MMU Illegal Operation) and **58** (MMU
  Access Level Violation) are the 68851's — access levels being one of the six
  features `[030]` §9.6 lists the 68030 as lacking.
- **Figures B-1 to B-15, every stack frame in the family**, including formats
  `$0`, `$1`, `$2`, `$9`, `$A` and `$B` — the six `AP_M68030_FRAME_*` carries and
  the same six Appendix A names.
- **Appendix C, the S-record output format.** A file interchange format for
  tooling, with no part behaviour in it. Read and recorded as such.

## `[PRM]`: WALKED WHOLE, 646/646, 2026-09-06 to 2026-09-07

Eight sections and three appendices.

| Batch | Sections | Pages | Yield |
| --- | --- | --- | --- |
| 2026-09-06 | audit, §1, §2 | 60 | Table 2-4 wrong in four cells and *not* in a scan; Table 1-5's four slips; a citation to an appendix that does not exist |
| 2026-09-06 | §3, §4 | 231 | a bit field accessed one byte per bit |
| 2026-09-07 | §5-§8, A-C | 355 | `RESET` cost nothing where it costs 518 clocks; `STOP`'s trace bits transposed against `[030]` |

**Two defects in this core, five documentary errors, one citation defect, and
one question recorded as unanswerable.** Everything else verified — and the
verification is the larger part: the opcode map, the vector table, the 32
floating-point predicates, the FP data formats, the addressing categories, the
45 floating-point instructions and the six stack frames are all this core's,
checked field by field against the document they came from.

## Owed

Nothing in `[PRM]`.

