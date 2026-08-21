# OMTI controller manuals — walk coverage record

Three manuals, and the DN3500's controller is an **8621**.

| Tag | File | Pages | Native | Cited |
| --- | --- | --- | --- | --- |
| `[OMTI]` | `omti/OMTI_AT_Controller_Series_Jan87.pdf` | 88 | 800 ppi | throughout `ap_omti.h` |
| `[8640]` | `omti/OMTI_8640_Technical_Reference_Manual_Jun89.pdf` | 61 | 600 ppi | as the sibling, several places |
| `[8000]` | `omti/OMTI_8000_Series_AT_Reference_Jun86.pdf` | 71 | 400 ppi | **once** — effectively unconsulted |

**220 pages total. None is walked.**

## STATUS: 9 pages read plus **a footer map for PDF 30–45**. The 8621 is not *listed* but the body text addresses **`862X`**; two navigational traps and one unmodelled timing figure are recorded below.

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

**What this costs — and this paragraph is a CORRECTION of itself.** It first
read: "walking these manuals cannot turn that into a citation, because the part
is not in them." **That was too strong, and §4.1 shows why.** Its opening
sentence is "From the perspective of software execution on the host, the **OMTI
862X** controller looks like two independent controllers", and p. 4-1 uses
`862X` and "OMTI 8000 series" as the subject throughout. **The body text
addresses the family by wildcard where Table 1-1 lists individual SKUs**, and
an 8621 is a 862X.

So the position is narrower than "not covered" and weaker than "cited":

- **Table 1-1 does not list the 8621**, so there is no SKU-level statement of
  what an 8621 is or how it differs from an 8620.
- **§4's normative text is written for `862X`**, so the software interface it
  describes is stated to apply to the whole family — which is exactly the level
  `ap_omti.h` relies on.

The honest reading is that `ap_omti.h`'s family inference is **well supported
for the software interface** and **unsupported for anything SKU-specific**.
Where a section says `862X`, quote it; where it says `8620`, do not silently
extend it. Any `ST3` reading from §6.4.4 needs that section checked for which
form it uses — a question with a definite answer, unlike the one recorded here
before.

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

**Footer map.** Built cheaply by rendering a page range at 150 dpi, cropping the
footer band, collapsing blank rows and stacking the strips into **one image** —
sixteen footers in a single read instead of sixteen. Worth reusing: it is the
only affordable way to map a manual with no text layer.

| PDF | 7 | 29 | 30 | 31 | 32 | 33 | 34 | 35 | 36 | 37 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| doc | 1-1 | 2-19 | 2-20 | 2-21 | 2-22 | **3-1** | 3-2 | 3-3 | 3-4 | 3-5 |

| PDF | 38 | 39 | **40** | 41 | 42 | 43 | 44 | 45 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| doc | 3-6 | 3-7 | **4-1** | 4-2 | 4-3 | 4-4 | 4-5 | 4-6 |

**§3 is PDF 33–39. §4 begins at PDF 40** and runs to 4-8, so PDF 40–47.

## One gap already visible, and two facts confirmed

The footer strips carry enough body text to see three things without a full read.

**GAP — the 100 µs post-reset wait is not modelled.** p. 4-3: "The RESET STATE
is entered by applying power to the controller (power-on-reset), by the reset
signal on the system bus, or by writing the RESET Register (**port 321**).
During this phase, the controller will initialize itself, will set default
parameters (ST412) to the LUNs, will de-assert all control functions and clear
all bits in the STATUS register. It will then enter the idle state. **WARNING:
The host must wait 100 usec after a -RESET before issuing a SELECT.**" Grepping
`ap_omti.h` and `ap_omti.c` for that wait returns **nothing**. A driver that
selects too early gets undefined behaviour on hardware and works here.
*Also to check when §4 is walked*: whether the **RESET Register at port 321** is
decoded at all — a narrow grep found no `0x321`, but that is not a careful check
and should not be recorded as a finding until §4.2 is read properly.

**Confirmed** — the `C/D` bit's word/byte semantics ("when C/D is 1 then only
bits 0-7 are used ... when C/D is 0 then all 16 bits are valid, byte 0 in bits
8-15 and byte 1 in bits 0-7") are already quoted in `ap_omti.h` twice, at its
header and at §4.2's data port. And p. 4-5's floppy **Digital Input** register —
"Bit 7 ... received from pin 34 of the floppy disk control cable and is normally
used for diskette change status. Bits 0 through 6 are Reserved" — is the kind of
register the walk exists to check.

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
2. ~~`[OMTI]` §1.2~~ — **done**, and **§4.1 amended it**: Table 1-1 lists no
   8621, but the body text says `862X`. See the corrected paragraph above.
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
