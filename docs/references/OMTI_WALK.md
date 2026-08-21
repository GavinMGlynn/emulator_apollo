# OMTI controller manuals — walk coverage record

Three manuals, and the DN3500's controller is an **8621**.

| Tag | File | Pages | Native | Cited |
| --- | --- | --- | --- | --- |
| `[OMTI]` | `omti/OMTI_AT_Controller_Series_Jan87.pdf` | 88 | 800 ppi | throughout `ap_omti.h` |
| `[8640]` | `omti/OMTI_8640_Technical_Reference_Manual_Jun89.pdf` | 61 | 600 ppi | as the sibling, several places |
| `[8000]` | `omti/OMTI_8000_Series_AT_Reference_Jun86.pdf` | 71 | 400 ppi | **once** — effectively unconsulted |

**220 pages total. None is walked.**

## STATUS: 3 of 220 pages read — `[8000]`'s cover and title page only.

Record opened 2026-08-21 with the method established and the reading order
revised by what those three pages said.

## **These PDFs have NO TEXT LAYER. Read this before planning any work on them.**

`pdftotext` returns **zero characters** from the whole of `[OMTI]` and the whole
of `[8000]` — verified, not assumed. They are page images with no OCR.

**The consequence is not merely inconvenience.** Every technique the previous
four walks leaned on is unavailable here:

- **No text-layer searching.** The other walks used extraction *as a search* to
  find candidate pages and to build the PDF-to-document footer map. Neither
  works. The footer map must be built by reading footers off images.
- **A search that returns nothing means nothing.** This already nearly cost a
  wrong conclusion: a scan of `[8000]` for "862x" found no hits, which looks
  like "this manual does not cover the 8621" and actually means "this manual
  cannot be searched". **Any statement of the form "the OMTI manuals do not
  mention X" is unsupportable unless every page was read.** That includes
  statements already in this project — check what they rest on.
- **Cost.** 220 pages at one image read each, with no way to skip ahead or
  confirm a section's extent without reading it. This is the most expensive
  document work this project has left, and it should be budgeted as several
  sessions rather than attempted as one.

Native resolutions are high — 800/600/400 ppi — so render at native and expect
large images. `pdfimages -list` first, as always.

## Why these are worth the cost anyway

The 8621 is the DN3500's **disk and floppy controller**: the boot path runs
through it. `CLAUDE.md`'s rule that a misbehaving module is presumed incomplete
until its register tables are walked applies to it directly, and the register
walks done elsewhere in this project have averaged a defect every few tables.

**`[8000]` covers a different product line — checked 2026-08-21, and this
corrects an earlier guess in this same record.** Its title page (PDF 3,
Publication No. 3001241) lists **Models: OMTI 8100, 8200, 8500 and 8600**. None
of those is an 862x. I had written here that `[8000]` was "the most interesting
of the three" on the assumption that a manual titled *8000 Series* would cover
the whole series; it does not.

**What that does and does not settle.** It does not name the 8620, 8621 or 8627,
so it is not a second source for the Apollo's part and should drop to last in
the reading order — 71 pages off the critical path. It does **not** establish
that "8600" and "8620" are unrelated: whether `8600` denotes a family that
includes the 862x parts is not stated on the title page, and only §1 would say.
Recorded as unresolved rather than assumed either way, because the cost of
being wrong is reading the wrong manual for a week.

`[OMTI]` therefore remains the primary and its title page still lists only the
**8620 and 8627**, so `ap_omti.h`'s "same family, so it covers the DN3500's
8621" stays an **inference**. Nothing found so far turns it into a citation.

## Suggested order when this is picked up

1. ~~`[8000]` front matter~~ — **done.** It covers 8100/8200/8500/8600, not the
   862x. Dropped to last; see above. The `ap_omti.h` family inference is
   untouched by it.
2. **`[OMTI]` §4** — Table 4-1's four ports and Table 4-3, which `ap_omti.h`
   cites most heavily and which the boot exercises.
3. **`[OMTI]` §5 and §6** — the fixed-disk and floppy command sets, walked
   command by command against `ap_omti.c`.
4. **`[8640]`** — the sibling, last, since it is cited only to corroborate.

## The open question these manuals might settle

**`ST3`'s five constant bits.** `[OMTI]` §6.4.4 and `[8640]` §5.6.4 give ST3
three live bits and five constants with bit 0 "not used - always 1";
`002398-04` p. 12-14 draws the generic 765's eight, each "from drive". All three
resolution tiers are recorded as exhausted for that item — but **the firmware
tier and the oracle tier were checked, and the reference tier was checked only
at the two sections named**.

The hope that `[8000]` might hold a fourth reading is **gone**: it covers a
different product line. What remains is that `[OMTI]` and `[8640]` have only
ever been *queried* at §6.4.4 and §5.6.4. Walking them whole is the only
reference-tier move left, and it may well confirm rather than change the
reading.
