# OMTI controller manuals — walk coverage record

Three manuals, and the DN3500's controller is an **8621**.

| Tag | File | Pages | Native | Cited |
| --- | --- | --- | --- | --- |
| `[OMTI]` | `omti/OMTI_AT_Controller_Series_Jan87.pdf` | 88 | 800 ppi | throughout `ap_omti.h` |
| `[8640]` | `omti/OMTI_8640_Technical_Reference_Manual_Jun89.pdf` | 61 | 600 ppi | as the sibling, several places |
| `[8000]` | `omti/OMTI_8000_Series_AT_Reference_Jun86.pdf` | 71 | 400 ppi | **once** — effectively unconsulted |

**220 pages total. None is walked.**

## STATUS: 8 of 220 pages read. **The 8621 is named in neither manual**, and two navigational traps are recorded below.

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

## **The 8621 is in neither manual, and `[OMTI]` is also called "8000 Series"**

Read 2026-08-21 from `[OMTI]` p. 1-1 (PDF 7), §1.1 and §1.2.

**§1.2's Table 1-1 lists exactly four models: 8620, 8627, 8120, 8127.** §1.1's
closing sentence names the same four. **There is no 8621 anywhere on the page.**

| | 8620 | 8627 | 8120 | 8127 |
| --- | --- | --- | --- | --- |
| Drives | 4 max W+F | 4 max W+F | 2 max W | 2 max W |
| Winchesters | up to 2 | up to 2 | up to 2 | up to 2 |
| ST412 recording | MFM | 2,7 RLL | MFM | 2,7 RLL |
| ESDI | Yes | Yes | No | No |
| Flexible disks | **Yes** | **Yes** | No | No |

**This settles the naming confusion I flagged last turn, in the opposite
direction from either guess.** §1.1 opens "**The OMTI 8000 Series** are a
combination of Winchester disk and floppy disk controllers" — so `[OMTI]` calls
*itself* the 8000 Series too. The brand covers both manuals; the **model sets do
not overlap**. `[8000]` documents 8100/8200/8500/8600, `[OMTI]` documents
8620/8627/8120/8127. Neither documents the **8621**.

**What this costs.** `ap_omti.h` says `[OMTI]` covers "the DN3500's 8621"
because it is "the same family" — an inference. **Walking these manuals cannot
turn that into a citation**, because the part is not in them. The inference may
well be sound (the 8620 is the ESDI+floppy 4-drive part, which is what the
DN3500 needs) but it stays an inference, and any `ST3` reading taken from
`[OMTI]` §6.4.4 inherits that status. **That is a real limit on what the
remaining 213 pages can deliver**, and it is better known now than at page 200.

Two facts worth keeping from the same page: "Support for high capacity
(**1.6 Megabyte**) floppies on the 8620 & 8627", and "**Concurrent** data
operations on winchester and floppy disk on the 8620 & 8627".

**Revision bars** (Rev C's own change markers) sit against §1.1's model sentence
and against the whole of Table 1-1 — so the model list itself was **changed in
Revision C**. A Rev B copy would list a different set, which is worth knowing if
one ever turns up.

## Two navigational traps, both found the expensive way

**1. The leftover renders are ~110 dpi and must not be used.** The PNGs in
`apollo-scratch/c151/omti/` left by earlier sessions measure **605 x 935** for a
5.5 x 8.5 inch page — about **110 ppi against a native 800**. That is far below
the resolution at which this project has twice found table cells unreliable
(the `002398-04` keyboard charts at 150, and again at 200). **Anything ever read
off those files should be treated as unverified**, including claims now in
`ap_omti.h`, and they should be deleted rather than reused. Render at 800 and
downscale a *reading copy* by an integer factor; the full page is 4400 x 6800.

**2. The contents cannot be used to compute PDF offsets.** PDF 29 is document
page **2-19** — established by reading its footer, not by arithmetic. The
contents lists §2.6 as starting at **2-12** and gives no end, and that section
(the DOS 3.1/3.2 patch note) runs to at least 2-19. So **a section's listed
start says nothing about its extent**, and with no text layer there is no way to
search for the next heading. The footer map has to be built by reading footers,
page by page, and the record should carry it as it grows.

**Footer map so far**: PDF 7 = 1-1 · PDF 29 = 2-19.

## `[OMTI]`'s section map — read off the contents, since nothing can search it

Document **3001483, Revision C, 20 January 1987**. Its own note: **"Vertical
bars in the left margin indicate changes from the previous revision"**, so Rev C's
changes are markable — in the contents the bar sits against **§2.5 and §2.6**,
which are therefore new since Rev B.

| § | Title | Doc page |
| --- | --- | --- |
| 1 | Introduction — 1.1 Product Description, **1.2 Number and Type of Drives supported**, 1.3 Specification | 1-1 |
| 2 | Configuration and Installation — jumper allocation 2-3, install 2-7, **2.5 1701 Error Code** 2-11, **2.6 DOS 3.1/3.2 patch** 2-12 | 2-1 |
| 3 | Host Electrical Interface — pin assignment 3-1, signal description 3-4, **3.4 Controller Hardware Architecture** 3-7 | 3-1 |
| **4** | **Host/Controller Software Interface** — 4.1 overview, **4.2 Fixed Disk Registers** 4-1, 4.3 fixed disk protocol 4-3, **4.4 Floppy Disk Registers** 4-5, 4.5 floppy protocol 4-7 | 4-1 |
| **5** | **Fixed Disk Functions** — 5.1 CDB 5-1, 5.2 Control Byte 5-3, **5.3 Status Register** 5-4, 5.4 Fixed Disk Commands 5-5 | 5-1 |
| **6** | **Floppy Disk Functions** — 6.1 command summary, 6.2 symbols, 6.3 Floppy Commands 6-3, **6.4 Command Status Registers 6-6** | 6-1 |
| A | Sense Code Summary and Description | — |
| B | Interleave Scheme | — |

**§6.4 at page 6-6 is where `ST3` lives** (§6.4.4). **§1.2 is where the covered
models are named**, which is the outstanding question about whether `ap_omti.h`'s
8621 family inference is a citation or a guess — one page, and it should be read
first.

**Prior renders exist** in `apollo-scratch/c151/omti/` for pages 28–51 and 74–85,
left by earlier sessions. They indicate which parts were previously *consulted*;
they are not evidence any page was walked.

## Suggested order when this is picked up

1. ~~`[8000]` front matter~~ — **done.** It covers 8100/8200/8500/8600, not the
   862x. Dropped to last; see above. The `ap_omti.h` family inference is
   untouched by it.
2. ~~`[OMTI]` §1.2~~ — **done.** Models are 8620/8627/8120/8127; **no 8621**.
   The inference cannot become a citation from these manuals.
3. **`[OMTI]` §4**, doc pages 4-1 to 4-8 — Table 4-1's four ports and Table 4-3,
   which `ap_omti.h` cites most heavily and which the boot exercises. **Find it
   by walking footers forward from PDF 29 (= 2-19)**, not by arithmetic: §2 runs
   well past the 2-12 the contents implies, so §4 starts later than PDF 29 + 8.
4. **`[OMTI]` §5 and §6** — the fixed-disk and floppy command sets, walked
   command by command against `ap_omti.c`. §6.4 carries `ST3`.
5. **`[8640]`** — the sibling.
6. **`[8000]`** — last; different product line.

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
