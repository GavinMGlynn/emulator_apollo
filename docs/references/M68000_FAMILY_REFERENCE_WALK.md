# M68000 Family Reference — walk coverage record

`M68000_Family_Reference_1988.pdf`, 608 pages. The last document in
`COMPLETION_PLAN.md`'s second processor-manual batch.

| Tag | File | Pages | Text layer | State |
| --- | --- | --- | --- | --- |
| `[FAMREF]` | `motorola/M68000_Family_Reference_1988.pdf` | 608 | **scanned, OCR** | **§1-§3 walked, 156/608** |

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
| 4 | Coprocessors (MC68851, MC68881, MC68882) | 159-244 | owed |
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

## Owed

§4 through §11, PDF 159-608.
