# M68000 Family Programmer's Reference Manual — walk coverage record

The instruction set this project implements, for every part in it.

| Tag | File | Pages | Text layer | State |
| --- | --- | --- | --- | --- |
| `[PRM]` | `motorola/M68000_Family_Programmers_Reference_Manual_1992.pdf` | 646 | **born-digital** | audit done 2026-09-06; **§1, §2 walked**, 60 pages |

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
| 3 | Instruction Set Summary | 72-104 | 3-1 to 3-33 | owed |
| 4 | Integer Instructions | 105-302 | 4-1 to 4-198 | owed |
| 5 | Floating-Point Instructions | 303-454 | 5-1 to 5-152 | owed |
| 6 | Supervisor (Privileged) Instructions | 455-540 | 6-1 to 6-86 | owed |
| 7 | CPU32 Instructions | 541-556 | 7-1 to 7-16 | owed |
| 8 | Instruction Format Summary | 557-596 | 8-1 to 8-40 | owed |
| A | Processor Instruction Summary | 597-627 | | owed |
| B | Exception Processing Reference | 628-640 | | owed |
| C | S-Record Output Format | 641-646 | | owed |

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

**The sibling manual settles it, exactly as `CLAUDE.md`'s resolution order
predicts.** `[030]` Table 2-2, *Effective Addressing Mode Categories*, rendered
and read: absolute short and long are `Alterable = X`, both PC memory indirect
rows are `Alterable = —`. That is what `ap_m68030_category.c` has.

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

## Owed

§3 through §8 and Appendices A, B and C — 586 pages. §4 (198) and §5 (152) are
the per-instruction pages and are the bulk of it; §8 is Table 8-2, the operation
code map, which ten citations already derive from.
