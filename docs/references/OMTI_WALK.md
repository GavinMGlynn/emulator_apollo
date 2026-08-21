# OMTI controller manuals — walk coverage record

Three manuals, and the DN3500's controller is an **8621**.

| Tag | File | Pages | Native | Cited |
| --- | --- | --- | --- | --- |
| `[OMTI]` | `omti/OMTI_AT_Controller_Series_Jan87.pdf` | 88 | 800 ppi | throughout `ap_omti.h` |
| `[8640]` | `omti/OMTI_8640_Technical_Reference_Manual_Jun89.pdf` | 61 | 600 ppi | as the sibling, several places |
| `[8000]` | `omti/OMTI_8000_Series_AT_Reference_Jun86.pdf` | 71 | 400 ppi | **once** — effectively unconsulted |

**220 pages total. None is walked.**

## STATUS: NOT STARTED. Record opened 2026-08-21 with the method established.

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

**`[8000]` is the most interesting of the three and the least consulted.**
`[OMTI]`'s title page lists only the **8620 and 8627**; `ap_omti.h` records that
it is "the same family, so it covers the DN3500's 8621" — an inference, not a
statement. A manual titled *8000 Series* may name the 8621 outright and may
differ from `[OMTI]` where the parts differ. **Whether it names the 8621 is
currently unknown**, and the search that appeared to answer it did not.

## Suggested order when this is picked up

1. **`[8000]`, front matter and contents** — reading only enough to learn
   whether it covers the 8621 and how it is organised. That answers whether
   `ap_omti.h`'s family inference is safe, which bears on every constant in the
   file. **Cheapest high-value question in the set.**
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
at the two sections named**. If `[8000]` covers the 8621 and states ST3
differently, that is a fourth reading nobody has looked for.
