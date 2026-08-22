# OMTI controller manuals — walk coverage record

Three manuals, and the DN3500's controller is an **8621**.

| Tag | File | Pages | Native | Cited |
| --- | --- | --- | --- | --- |
| `[OMTI]` | `omti/OMTI_AT_Controller_Series_Jan87.pdf` | 88 | 800 ppi | throughout `ap_omti.h` |
| `[8640]` | `omti/OMTI_8640_Technical_Reference_Manual_Jun89.pdf` | 61 | 600 ppi | as the sibling, several places |
| `[8000]` | `omti/OMTI_8000_Series_AT_Reference_Jun86.pdf` | 71 | 400 ppi | **once** — effectively unconsulted |

**220 pages total.** Coverage of `[OMTI]`, restated 2026-08-22 after the day's
reading — the entries below are the evidence for each row:

| section | pages | state |
| --- | --- | --- |
| §2, Configuration and Installation | 2-3 to 2-19 (PDF 12-29) | **walked** — jumper allocation's four tables, both COMMON SYSTEM JUMPER SETTINGS tables, the installation procedures, the format flowchart, the 1701 codes, the DOS patch |
| §3, Host Electrical Interface | 3-1 to 3-7 (PDF 33-40) | **walked** — §3.1-§3.4 |
| §4.1–§4.5 | | **derived into the model** |
| §5.1–§5.4 | | **derived**, and §5.4.1, §5.4.2 and §5.4.4 read as images 2026-08-22 after the coverage row's lower endpoint proved wrong |
| §6.3 | | **derived** |
| §6.4 | | **walked** |
| §1, §2-1/2-2, §6.1–§6.2, §7 onward | | **unread** |
| `[8640]`, `[8000]` | 132 pages | **unread entirely** |

*This line said "None is walked" until 2026-08-22, when a citation audit showed
otherwise — and it survived two corrections to the status section below before
`check_docs.py`'s new coverage check found it. It then said "§2, most of §3 ...
unread" for the rest of that day, while five commits were walking exactly those
sections. A coverage header is the part of a walk record that rots first,
because every entry appended below it is a reason to change it and none of them
is a prompt to.*

## STATUS: **§5's command chapter is DERIVED, and this record said it was owed**

**Corrected 2026-08-22, and the correction is the finding.** This record's status
line said "§5 and §6.1–6.3 are still owed", and the plan's item says the manuals
are unwalked. Both would send a reader to re-read §5. **The code already carries
it**: `ap_omti_cdb.h`, `ap_omti.h` and `ap_omti.c` cite **37 distinct §5
subsections** between them — §5.1.1 through §5.1.4, §5.2, §5.3, §5.3.4.2, and
the whole command run **§5.4.3 to §5.4.29** — and they are derivations rather
than references: §5.4.29's ten-byte reply with its three "(-1)" fields,
§5.4.17's START/STOP "valid for ESDI drives only", §5.4.13's identification
block. The chapter was read before this coverage record existed and the record
was opened without auditing what the code already cited.

*That is the failure mode `CLAUDE.md` wants a walk record to prevent — telling a
finished document from a sampled one — occurring in the record itself.* It cost
nothing this time only because the citations were checked before the re-reading
started.

**The audit is wider than §5, and so is the correction.** Grepping every `§`
the OMTI model cites and attributing each to its manual gives, for `[OMTI]`:

| section | evidence | status |
| --- | --- | --- |
| §3.4 | quoted verbatim in `ap_omti.h`'s opening — "This allows full concurrent operations between these two sections" — and cited three more times for the two halves running at once | **derived** |
| §4.1–§4.5 | §4.5 explicitly, "describes the floppy protocol as command phase, busy, result phase"; §4.1–§4.4 already recorded here | **derived** (record said §4.1–§4.4) |
| §5.1–§5.4 | 37 distinct subsections, §5.4.3 through §5.4.29 — **and note where that range starts: §5.4.1 and §5.4.2 were outside it, and §5.4.1 held a 50-second timeout this core did not have. Both walked 2026-08-22** | **derived** |
| §6.3 | the floppy command set — §6.3.2's N/SC/GPL track fill, §6.3.6's step to track 0, §6.3.7's ST0-and-cylinder, §6.3.10's NCN, §6.3.11's INVALID, and more | **derived** (record said "owed") |
| §6.4 | already recorded | walked |
| **Appendix A** | its **code list** is cited in `ap_omti.c` and eight `awd_suite` comments; its **SENSE DATA BYTE FORMAT** was not | **partly derived — and the unwalked half held a defect** |
| §1, §2, most of §3, §6.1–§6.2, §7 onward | no citations | **genuinely unread** |

*The claim is bounded deliberately.* A verbatim quote in a comment is strong
evidence the page was read and the behaviour taken from it; it is **not** proof
the section was walked field by field, which is this project's standard for
"walked". So these are marked **derived** rather than walked, and the difference
is that a field-by-field pass over them could still find something — as §6.4's
did for `ST3`.

**§5.1.2's COMMAND SET SUMMARY is confirmed against the model, PDF 48 (doc
5-2)**: all twenty-four commands common to all models, the ST506/412's
`0C INITIALIZE DRIVE CHARACTERISTICS`, and all three ESDI commands — `10 CHECK
TRACK FORMAT`, `37 READ ESDI DEFECT LIST`, `EC READ CAPACITY` — every code and
every command/data length matching `ap_omti_cdb.h`, including `COPY`'s
**ten**-byte descriptor against everything else's six.

**And the summary is not exhaustive**, which is worth knowing before trusting
it: it omits `1A START/STOP`, which **§5.4.17 of the same manual** documents and
this core implements. A reader checking the model against §5.1.2 alone would
find a command it does not list and conclude the model had invented one.

## Earlier status: 15 pages read plus footer maps for PDF 30–45 and 60–80. **§6.4's status registers are walked and they settle `ST3`.** **§4.1 to §4.4 walked** — both register sets, fixed disk and floppy. Registers confirm the model; the manual's DMA-channel contradiction is resolved below on physical grounds; one defect found (see the plan). The 8621 is not *listed* but the body text addresses **`862X`**; two navigational traps and one unmodelled timing figure are recorded below.

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

## §4.2's register tables, walked — Tables 4-1 and 4-2 confirm the model

Read from PDF 41 (doc 4-2) at native 800 ppi.

**Table 4-1, I/O Port Addresses** — and it **answers the question left open last
commit**: port `321H` on *write* is the RESET function.

| Port | Read | Write |
| --- | --- | --- |
| `320H` | DATA IN | DATA OUT |
| `321H` | STATUS | **RESET (Function)** |
| `322H` | CONFIGURATION | SELECT (Function) |
| `323H` | N/A | MASK |

`ap_omti.h` has all four with the read/write asymmetry stated — `DISK_STATUS = 1u,
/* read STATUS, write RESET (a function) */`, `DISK_CONFIG = 2u`,
`DISK_MASK = 3u, /* read N/A, write MASK */`. **So the RESET register is decoded**,
and the narrow `0x321` grep that found nothing was looking for an absolute
address where the code holds an offset — exactly why it was recorded as
"to check" rather than as a finding.

**Table 4-2's STATUS register** matches bit for bit: bit 5 **IREQ** = `ST_IREQ
0x20`, bit 4 **DREQ** = `ST_DREQ 0x10`, bit 3 **BSY** = `ST_BSY 0x08`, bit 2
**C/D** = `ST_CD 0x04`. Bits 7 and 6 are "Not Used (Set to 1)".

**p. 4-3 completes the register set, and settles both things carried forward.**

- **Status bits 1 and 0 are documented after all**, on the next page: **bit 1
  I/O** ("1 = Direction of transfer is from the controller to the Host") and
  **bit 0 REQ** ("1 = Request transfer of one byte or Word"). `ap_omti.h` has
  `ST_IO 0x02` and `ST_REQ 0x01` with those senses. Not concluding they were
  undocumented was correct.
- **RESET**: "Writing any value to this register will cause the controller to be
  reset", with the 100 µs warning repeated — it appears **twice** on this page,
  under RESET and again under §4.3.
- **CONFIGURATION**: the drive-configuration jumpers, bits 7-4 unused (set to 1),
  and bits 3-0 the straps — **`W20`-`W23` on an 8620/8627**, `W1`-`W4` on an
  8120/8127. `ap_omti.h` already carries the 862x form.
- **SELECT**: "Writing any value ... will cause the controller to begin a
  Selection Sequence and request a command transfer."
- **MASK**, and this is the valuable one: **bit 1 INTERRUPT ENABLE, bit 0 DMA
  ENABLE**. `ap_omti.h` records that `002398-04` p. 12-9 draws the same register
  **the other way round** — `dma` at 1, `int` at 0 — and that this project
  followed the part's own manual with the oracle agreeing. **This page is that
  manual's own statement of it**, so a decision taken between two disagreeing
  sources is now verified from the primary. Bit 0's text even names the
  consequence: "DREQ is gated onto system bus on DRQ3 and DREQ set in STATUS
  register."

**§4.3's six logical states** — RESET, IDLE, SELECTION, COMMAND, DATA, STATUS —
are named as the sequence a host steps through. A grep for a matching state
enumeration in `ap_omti.h` found none; **whether the model represents these
explicitly or implicitly is not yet checked** and should be, since the protocol
is what the boot drives.

Two things to carry forward:

- **Bits 1 and 0 are not described on this page.** The definition stops at
  bit 2's note. Either they are covered later or they are undocumented; do not
  conclude either until §4.3 is read.
- The page names the **system-bus lines**: IREQ "is set with **IRQ14** on the
  System", and DREQ is set "along with **DRQ3** on the System Bus". Those are
  board wiring rather than part behaviour — check where `ap_board` places them,
  and note `002398-04` gives the DN3000 an OMTI interrupt of its own, which may
  or may not be 14.

## **`[OMTI]` contradicts itself on the DMA channel: DRQ3 or DRQ7**

Found by walking §4 in order. Three statements, two of them disagreeing with the
third:

| Page | Text |
| --- | --- |
| 4-2, Table 4-2 bit 4 DREQ | "this bit is set along with **DRQ3** on the System Bus" |
| 4-3, MASK bit 0 DMA ENABLE | "DREQ is gated onto system bus on **DRQ3** and DREQ set in STATUS register" |
| **4-4, DATA STATE** | "it will set the **DRQ7** bit on the system bus, requesting a DMA cycle ... **DACK7** from the system will clear DRQ7" |

**This core uses DRQ7** — `ap_omti.h`'s DMA-request accessor and `ap_board.c`'s
"the Winchester on DRQ7, from the controller's own `DREQ`". That agrees with
p. 4-4 and disagrees with pp. 4-2 and 4-3.

**A citation in `PROJECT_STATUS.md` is wrong and should be narrowed.** Its OMTI
row says "IRQ14 and DRQ7 wired, both derived from the STATUS register **as §4.2
and §4.3 give them**". §4.2 does not give DRQ7; it gives DRQ3. Only §4.3's DATA
STATE paragraph gives DRQ7. IRQ14 is unaffected — p. 4-2 and p. 4-4 agree on it.

**RESOLVED for this machine, and not by preferring a page.** Table 2-4 was
checked as the record said to: `ap_dma.h` carries
`AP_DMA_WINCHESTER_UNIT 1u /* DRQ7, and 16-bit */`, and the walk record for
`008778-03` p. 34 has the arrangement — **`DRQ0`-`DRQ3` are 8-bit on controller
1; `DRQ5`-`DRQ7` are 16-bit on controller 2**.

**That makes DRQ7 the only possible answer, on physical grounds.** §4.3's DATA
STATE describes the fixed-disk transfer as "**DMA word mode**" and says the
controller requests a cycle "when the controller requires a **word** to be
transferred". A word transfer cannot run on an 8-bit channel, so **`DRQ3` is
excluded by the transfer width**, whatever pp. 4-2 and 4-3 say. The core's DRQ7
is right, and now for a stated reason rather than a coin toss between two pages
of one manual.

**The `PROJECT_STATUS` citation still needs narrowing**: its OMTI row credits
"§4.2 and §4.3" for DRQ7, and §4.2 says DRQ3. The right citation is §4.3's DATA
STATE **plus** `008778-03` Table 2-4 for the channel's width — which is the
sentence that actually justifies the constant.

## §4.3's protocol, in full — six states, and the model's representation unchecked

p. 4-4 walks the sequence, and it is worth having in one place because the boot
drives it:

- **IDLE** — "the only time the controller will respond to a select request".
  Writing SELECT (port 322) enters selection.
- **SELECTION** — the controller asserts **BSY** (bit 3) in STATUS, then enters
  command state.
- **COMMAND** — **C/D** (bit 2) is set, then **REQ** (bit 0), asking for the
  first command byte to be written to DATA OUT (port 320) **in BYTE mode**.
  Writing it de-asserts REQ and moves the byte to the buffer; repeated for every
  command byte; C/D is then de-asserted and DATA entered.
- **DATA** — "if no data is required, the status state is entered". Programmed
  I/O handshakes like the command transfer, REQ per word, direction by the
  **I/O** bit. DMA mode uses DRQ7/DACK7 (see the contradiction above).
- **STATUS** — the controller places the status byte in DATA IN bits 0-7, sets
  **C/D** and **I/O**, and if interrupts are enabled sets **REQ** along with
  **IRQ14**. Reading the byte clears IREQ and IRQ14, clears C/D, I/O and BSY,
  and returns to idle.

**Checked, and the answer is more interesting than yes or no.** `ap_omti.h` has
`ap_omti_phase_t` and `ap_omti.c` quotes this very section verbatim ("The IDLE
STATE is the only time the controller will respond to a select request"). My
earlier grep found nothing because it looked for `_STATE`, and the enum is
`AP_OMTI_PHASE_*` — **the third false negative from a narrow grep in this
session**, and the reason none of them was recorded as a finding.

But **the two sixes are not the same six**:

| §4.3 | model |
| --- | --- |
| RESET | **absent** |
| IDLE | `PHASE_IDLE` |
| SELECTION | **absent** |
| COMMAND | `PHASE_COMMAND` |
| DATA | split — `PHASE_DATA_IN`, `PHASE_DATA_OUT` |
| STATUS | `PHASE_STATUS` |
| — | `PHASE_EXECUTING`, added for drive access time |

Splitting DATA is finer than the manual and harmless — the `I/O` bit
distinguishes the directions anyway — and `EXECUTING` is a documented modelling
addition. **RESET and SELECTION being absent is defensible too**: the manual has
both fall through immediately ("The controller then enters the command state";
"It will then enter the idle state"), so a transient state can reasonably be
collapsed into the write that causes it. **What is missing is the argument.**
The enum's comments explain where `EXECUTING` sits and why, and say nothing
about the two states that were dropped.

**And this joins up with the other gap.** §4.3's RESET state is exactly where
the **100 µs wait** lives — "the host must wait 100 usec after a -RESET before
issuing a SELECT", stated twice. A model with no RESET phase has nowhere to put
a duration, so the missing state and the missing wait are **one gap, not two**:
implementing the wait means giving RESET a phase with a length, and that is the
shape the fix should take.

### CLOSED 2026-08-21 — and the page image added a third entry path

Both pages re-read as images at 300 ppi before implementing, which is what the
resolution order asks for and which paid: the RESET STATE has **three** entries,
not the one the register table implies —

> "The RESET STATE is entered by applying power to the controller
> (power - on -reset), by the reset signal on the system bus, or by writing the
> RESET Register (port 321). During this phase, the controller will initialize
> itself, will set default parameters (ST412) to the LUNs, will de-assert all
> control functions and clear all bits in the STATUS register. It will then
> enter the idle state."

So a **power-on** controller is in the reset state before any register is
touched, which is the case a model built around the register write would have
missed entirely. `AP_OMTI_PHASE_RESET` is appended to the enum (the values are
hashed) with `AP_OMTI_RESET_TIME`, `ap_omti_disk_reset` enters it from all three
paths because all three run through that one function, and `ap_omti_advance`
retires it to idle. The refusal needs no new code: the SELECT guard already
required `PHASE_IDLE`.

**p. 4-4 also settles the DRQ contradiction from the other side.** Its DATA
STATE reads "it will set the **DRQ7** bit on the system bus", in the same
paragraph as DACK7 — against p. 4-3's MASK bit 0, "DREQ is gated onto system
bus on **DRQ3**". Two sections of one manual, and only §4.3 can be cited for
the constant this core uses. `PROJECT_STATUS`'s OMTI row said "§4.2 and §4.3"
and now says which.

*Verification: `omti_suite` 27 → 31 — the reset state outlasting one time unit
short of the deadline, a SELECT inside the window refused and the same write
honoured after it, the register write restarting the window from itself rather
than from power-on, and the deadline being offered to the scheduler. Three
suites had to learn the host's half of the protocol: `awd_suite` and
`afd_suite`'s builders now wait out the window, and four assertions that read a
**deadline** were rewritten to read a **duration**, since the controller is no
longer handed its first command at time zero.*

## §4.4, the floppy register set — confirms, and one address set is absent

p. 4-5. **Table 4-3** gives five 8-bit registers, each at a **primary or
secondary** address "selectable (re: Section 3)":

| Primary | Secondary | Read | Write |
| --- | --- | --- | --- |
| `3F2H` | `372H` | N/A | Digital Output |
| `3F4H` | `374H` | Main Status | N/A |
| `3F5H` | `375H` | Data | Data |
| `3F6H` | `376H` | N/A | Additional Control |
| `3F7H` | `377H` | Digital Input | Diskette Control |

`ap_omti.h` has the primary set with the `3F6`/`3F7` split explicit — and
records that Table 4-3 is what *found* that split, writes to `3F7` having
previously shared `3F6`'s byte. The **Digital Output** bits match one for one
(5 Drive B motor, 4 Drive A motor, 3 interrupts and DMA, 2 reset-when-zero,
0 select-drive, 7/6/1 reserved), the "**All bits are cleared when a channel
reset occurs**" sentence is quoted, and **Digital Input**'s bit 7 from "pin 34
of the floppy disk control cable" with bits 0-6 reserved is there too.

**The secondary set `372H`-`377H` has no hits in `src/`.** Recorded as an
observation and **not** as a defect: the DN3000's floppy is placed at ISA `3F0`
by `002398-04`, so this board is presumably strapped to primary and modelling
only primary is correct *for this machine*. What is unverified is whether the
Apollo could be strapped to secondary at all — a jumper question, §2.3, and one
for the walk to answer when it reaches that section rather than to guess now.

> **ANSWERED 2026-08-22, from §2.4.3 rather than §2.3.** The strap is **`W14`**,
> and the board can indeed sit at either base. Detail below under §2.4. The
> guess held; what was wrong was the section — the jumper that selects the
> floppy base is documented in the *installation procedure*, not in the jumper
> allocation table, which is why a reader who had walked §2.3 alone would still
> not have found it.

**Another family wildcard**: §4.4's heading is "(not applicable for 812x)" and
its text says "the floppy disk portions of the **OMTI 8x2x** controller" — the
same generic form as §4.1's `862X`, and further support for reading this
manual's normative text as family-wide.

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
3. ~~**`[OMTI]` §4**~~ — **done**, §4.1 to §4.5.
4. ~~**`[OMTI]` §5 and §6**~~ — **derived, not walked** (2026-08-22 citation
   audit): §5.1–§5.4 and §6.3 are in the model with verbatim quotes, §6.4 is
   walked. A command-by-command pass against `ap_omti.c` would still be worth
   doing — that is what turned up `ST3` — but it is a re-check rather than a
   first reading.
5. **`[8640]`** — the sibling, **and partly used already**: §5.6.4 was read to
   settle whether `ST3` bit 4's name-versus-description contradiction was one
   manual's slip or the vendor's. It is the vendor's; both manuals carry the
   identical sentence.
6. **`[8000]`** — last; different product line.
7. **`[OMTI]` §1, §2, most of §3, §6.1–§6.2, §7 onward** — the genuinely unread
   remainder, and the honest first target now.

## §6.4 walked — `ST3` is settled, and it explains `ST0`

Queried ahead of the in-order walk because `ST3` was a named open item.
*(This sentence continued "; §5 and §6.1–6.3 are still owed" until 2026-08-22,
when the citation audit showed both derived. It sat in body prose, where the new
`check_walk_coverage` deliberately does not look — per-page rows have to be able
to say a section is unread — so a summary check cannot be the only guard.)* Footer map: **PDF 72 = 5-26, 75 = 6-2, 79 = 6-6,
80 = 6-7** — and §5 runs to at least 5-26, far past what the contents implies.

**§6.4.4's `ST3`**, verbatim: bit 7 not used always zero; **bit 6 Write Protect**;
bit 5 not used; **bit 4 Track 0 — "Status of the 'ready' signal"**; bit 3 not
used; **bit 2 Head Address — "Status of the 'side-select' signal"**; bit 1 not
used; **bit 0 not used - always 1**. `ap_omti.h` matches exactly, including
bit 4's name contradicting its own description, which the header already flags.
**The `ST3` `PROVISIONAL` is confirmed from the primary source.**

**And it resolves the `ST0` question one page earlier.** §6.4.1 calls `ST0`
bits 3 and 2 "Not Used - Always zero"; §6.4.4 puts **ready at `ST3` bit 4** and
**head at `ST3` bit 2**. The OMTI does not *drop* those two signals — it **moves
them**. The generic 765 reports them in `ST0`; this board reports them in `ST3`.
So the two manuals describe different silicon rather than disagreeing about the
same silicon, and `ap_omti.h`'s long argument for following the part's own
manual was right. See the plan item for what that means for
`AP_OMTI_ST0_NOT_READY` and `AP_OMTI_ST0_HEAD`.

Also captured: **§6.4.1 `ST0`** (interrupt code 00/01/10/11, seek end, equipment
check "if the 'track-0' signal fails to occur after **77 step pulses**", unit
select), **§6.4.2 `ST1`** (end of cylinder, data error, overrun, no data, not
writeable, missing address mark) and **§6.4.3 `ST2`** (control mark, data error
in data field, wrong cylinder, scan equal hit, scan not satisfied, bad cylinder,
missing address mark in data field).

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


## Appendix A's format table, walked 2026-08-22 — and the half nobody read

PDF 81, doc A-1. **SENSE CODE SUMMARY AND DESCRIPTION.**

This appendix is the sharpest example yet of *consulted is not walked*. Its code
list has been cited for years — `ap_omti.c` names it for the sense bytes, and
eight `awd_suite` comments quote `17 Write Protected`, `19 Bad Track
Encountered`, `21 Illegal Disk Address`, `22` and `23 Volume Overflow` verbatim.
Three lines above those codes sits the **byte format table**, and it had never
been read.

    byte 0   AV | 0 | TYPE | SENSE CODE
    byte 1   C10 | 0 | LUN | HEAD NUMBER
    byte 2   C09 | C08 | SECTOR NUMBER
    byte 3   CYLINDER LOW (C00-C07)

Everything in it was modelled **except bit 5 of byte 1**: the refusal path built
the byte from the head and `C10` and left the LUN clear, so every refusal
reported LUN 0 whatever unit the command addressed. Fixed.

*And it is currently unreachable*, which the fix's test says rather than hides:
the only refusal carrying an address is one against a drive, and
`ap_omti_attach` fits a single drive at LUN 0. A command to LUN 1 is refused
`04 DRIVE NOT READY` with no address at all. So the field is right per the
manual and exercised by nothing — the standing `AP_OMTI_ST3_READY` has — and the
test pins the reachable half so that a second drive turns it into a real test
rather than finding a field nobody had thought about.

## Appendix A page A-2, the sense code table, walked code by code

Twenty-nine codes in four type groups, and the eight this core can produce match
the table **exactly**, type bits included:

| code | table | produced by |
| --- | --- | --- |
| `04` | Drive not selected/not ready | no drive at the LUN |
| `17` | Write protected | a write to a read-only image |
| `19` | Bad track encountered | the bad-track list |
| `1C` | Unable to read Alternate Track data / Illegal access to an alternate track | a direct access to an alternate |
| `20` | Invalid Command | an opcode outside the ESDI set |
| `21` | Illegal Disk Address | an address past the drive |
| `22` | Illegal Function for Drive Type | a floppy-only function on the fixed disk |
| `23` | Volume Overflow | the end of volume *after* a multiblock command started |

**The other twenty-one are deliberately never set**, which `ap_omti.c` states as
a rule rather than an omission: "Only the codes this core can genuinely produce
are ever set; everything else would be inventing a failure mode." They are the
failures of a *mechanism* this core does not have — no index pulse (`01`), no
ECC (`10`, `11`, `18`), no seek (`02`, `15`), no sequencer (`16`), no firmware
checksum (`31`).

*The type bits are right for free*, because every constant was taken from this
table: `04` is `00` drive, `17`–`1C` are `01` data, `20`–`23` are `10` command.
Nothing computes the type field and nothing has to.

Two gaps in the table — **and one of them is the table's mistake, not the
part's.** A-2 lists no `05` and no `07`; **A-3's descriptions define
`07 Multiple Drives Selected`**, "the controller detected multiple DRIVE
SELECTED signals when it attempted to select the specified Logical Unit
Number". So the summary table omits a code the same appendix documents two
pages later, exactly as §5.1.2's command summary omits `1A START/STOP`.

*Recorded as a correction of this record*: the entry above said "type 0 defines
no `05` or `07`" after A-2 alone. Only `05` is genuinely undefined, and `1B` in
type 1 remains to be checked against A-4's descriptions, which do not list it
either. **A summary table in this manual is not a census** — that is now the
second instance and the safer assumption.

**Page A-4, the type 1 descriptions, walked too** — and it names two CDB
*fields* rather than only outcomes, which is where the LUN omission came from
one page earlier. Both are already modelled: `18 Correctable ECC` turns on "a
READ command ... issued with the **DISABLE ECC bit SET**", which
`ap_omti_cdb.h` carries as bit 6 of byte 5 with the note that the same bit means
something else on other commands; and `1A Illegal Interleave Factor` turns on a
FORMAT's **interleave factor**, which is byte 4 doubling as the block count and
is documented as doing so. So this page confirms.

*One naming difference inside the appendix*: A-2's table calls `14` "Sector not
found" and A-4's description calls it "Record Not Found". Same code, two names,
in one document — recorded because this project has been caught by that pattern
three times now (`ST3` bit 4, `EC` READ CAPACITY/CONFIGURATION, and this).

**Pages read in this appendix: A-1, A-2, A-4.** A-3 and A-5 onward are not read,
and the type 0, 2 and 3 descriptions live among them.

Also confirmed on the page and already modelled: **AV**, "if set, indicates that
the error code in byte 0 applies to the sector address in bytes 1,2,3"; and
**SENSE TYPE** in bits 5-4 of byte 0 — `00` drive, `01` data, `10` command, `11`
diagnostic — which every sense constant in `ap_omti_cdb.h` already encodes
correctly, `04` as a drive error and `20`/`21` as command errors, because they
were taken from the code list. A **SENSE DATA WORD FORMAT** is given beside the
byte one; this core presents the byte form, which is what the 8-bit data port
carries.


## Appendix A page A-3 — and a documented behaviour this core does not have

Type 0's descriptions, plus type 1's `10`. Everything confirms except the first
line of the page, which is a **GAP**:

> `00` **No error or no sense information**. "... If a REQUEST SENSE command is
> issued when there is no error, the Sense information reported specifies **the
> last Sector Address processed**."

`finish()` writes `sense[0] = error ? sense : 0` and zeroes bytes 1-3, so a
REQUEST SENSE after a successful command reports an all-zero address where the
part reports the last one it handled. A driver using that to confirm where a
command landed would be told sector 0 of cylinder 0.

**Not implemented here, and the reason is a choice the page does not make**: it
says the *sense information* specifies the last address and does not say whether
`AV` is set with it, and `AV`'s own definition — "the error code in byte 0
applies to the sector address" — reads oddly when byte 0 is `00 no error`.
Modelling it means choosing, and this project's rule is to name the gap instead.
The state it needs is small: the CHS of the last command that touched a surface,
which `refuse()` already computes for its own path.

*Also confirmed on the page*: `04`'s two causes — no DRIVE SELECTED **or** no
DRIVE READY, which is why this core reports it for an unfitted LUN; `06`'s
recalibration bound of "5 steps more than the total number of cylinders", which
is `ap_omti.h`'s equipment-check argument from §6.4.1's 77 step pulses seen from
the other side; and `09 Cartridge Changed`, "may only occur on Removable type
drives".


## Appendix A page A-5 — the appendix finished, and a number that had no source

Type 1's tail (`1F`), all of type 2 and all of type 3. **Every description
confirms** what this core produces: `20`'s "decoded a command code that it does
not support", `21`'s "Sector Address beyond the capacity of the drive", `22`'s
"a Change Cartridge command (HEX 1B) ... issued to a LUN assigned as a Fixed
drive type" — which `awd_suite` already quotes — and `23`'s "after the
commencement of a multiblock command".

**The finding is in `30 RAM error`**: "the controller detected a data error with
its internal RAM buffer of **8K bytes**". `ap_omti.h` reports **32K** in the
identification block's byte `14`, and the comment justifying it read "32K, per
the table above" — which is not a citation. The table gives the *encoding*
(`0-0` 2K, `0-1` 8K, `1-0` 16K, `1-1` 32K) and says nothing about which an 8621
reports.

The oracle writes the same value with the same bare comment —
`m_sector_buffer[0x14] = 0xc0; // 32K buffer size` — so two implementations
agree and neither cites anything, which is not evidence when both may have read
one table the same way. Now `PROVISIONAL`, with what would settle it: an 8621
identification block read off hardware, or an Apollo document naming the buffer.
The boot cannot discriminate — the PROM's Winchester test reads the error bytes
at `10`-`13` and never looks at `14`.

*A fourth name difference inside this manual*: A-2's table calls `31` "Z8
firmware checksum" and A-5 calls it "EPROM Checksum". After `ST3` bit 4, `EC`,
and `14` sector-versus-record, this stops being remarkable and becomes a
property of the document.

**Appendix A is now walked: A-1 through A-5, which is all of it.**


## Appendix B, INTERLEAVE SCHEME — walked, and it separates two things

PDF 86, doc B-1. The physical-to-logical sector map for interleave factors
`0/1` through `8` on 512-byte sectors (17 a track) and `0/1` through `4` on
1024-byte sectors (9 a track), with two rules under it:

> "Interleave factor of zero will be set to one."
>
> "Interleave factors greater than one half the total number of sectors per
> track are not recommended."

**`ap_omti.c` is right to ignore the placement** and says so with an argument
this page confirms: interleave is "a placement of sectors around a rotating
surface, and this model has no rotation to place them on". Appendix B is exactly
that placement — a permutation table — and a core with no rotational position
has nothing to apply it to.

**But validating the factor is a different thing from using it**, and that half
is missing. A-4's `1A Illegal Interleave Factor` fires when "a FORMAT/CHECK
TRACK FORMAT command was issued with an INTERLEAVE FACTOR **greater than the
number of sectors on the track**", and this core accepts any value. The
permissive direction: a driver with a bad factor gets a formatted track here and
a refusal on hardware. Named as a plan item rather than fixed in passing, with
the rule and the sense code both in hand.

*The `0` normalisation is not a gap*: "zero will be set to one" only matters to
a model that places sectors, and this one does not.


## Appendix B-2 and B-3, and the tail is finished

The abbreviations and mnemonics list, ending the manual at PDF 88. No facts a
model can hold — but two entries are worth having been read, because they name
fields rather than describe hardware:

**`D LUN` Destination Logical Unit Number** and **`S LUN` Source Logical Unit
Number**. Those belong to `COPY`'s ten-byte descriptor, and after the sense
byte's LUN turned out to be unset it was worth checking they were not the same
omission. They are not: `ap_omti.c` decodes the destination address by handing
bytes 5-7 to the same CDB decoder the source uses, so the destination LUN is
extracted with it, and §5.4.21's "Source and Destination LUN's may be the same"
plus one attached drive is why nothing acts on it. Reasoned, not missed.

`WSI`, "equivalent to: Reduced Write Current", is a drive signal with no place
in an image-backed model.

**PDF 81-88 — Appendix A entire and Appendix B entire — are now walked.** With
§3.4, §4.1-§4.5, §5.1-§5.4, §6.3 derived and §6.4 walked, what remains unread in
`[OMTI]` is **§1, §2, most of §3, and §6.1-§6.2** — the introduction, the
installation and jumper chapters, and the floppy chapter's opening. The two
sibling manuals remain, `[8640]` partly used and `[8000]` untouched.


## §6.2, the floppy chapter's symbol glossary — one encoding this core lacked

PDF 75, doc 6-2. A glossary of the FDC's field names, and most of it names
things the model already carries under the same names: `N`, `SC`, `R`, `NCN`,
`PCN`, `MT`, `MF`, `SK`, `ND`, `ST0`-`ST3`, `US0`-`US1` "encoded the same as
bits 0 and 1 of the digital output register (DOR)".

**The yield is `SRT`.** `ap_omti.c` cited §6.3.8 for "step rate, head load and
head unload times" and said "nothing in this core is timed off them yet" —
which was true and gave a later reader nothing to work from. §6.2 gives the
encoding:

    1.2 MB drive    1111 = 1 ms   1110 = 2 ms   1101 = 3 ms
    320 KB drive    1111 = 2 ms   1110 = 4 ms   1101 = 6 ms

with `HUT` in 16 ms increments and `HLT` in 2 ms on the 1.2 MB drive.

**And the encoding predicts what this machine should program.** `008778-03`
Table 7-7 gives the drive **3 ms track-to-track minimum**, which is `SRT = 1101`
exactly — the slowest of the three and the only one the mechanism can meet.
`AP_OMTI_FDC_TRACK_TO_TRACK` is that same 3 ms, so a model honouring `SRT` would
agree with the fixed figure for any correctly-programmed driver and diverge only
for a wrong one. That is the permissive direction, which is what makes it a gap
worth naming rather than a harmless simplification.

*Not implemented, and the reason is a measurement this project cannot make*:
Phase A established that the reference boot **never touches the floppy
controller** — not one register access in 350 M instructions — so no run here can
say what `SRT` the firmware writes.

*Also recorded*: `HLT` is moot on this machine whatever its encoding, because
`008778-03` §7.7.5 says "The *Domain System* does not require a head load
solenoid" — already the reason head load time is documented-and-not-modelled.


## §6.1, the floppy command summary — and the one summary here that *is* a census

PDF 74, doc 6-1. **FLOPPY DISK FUNCTIONS**, and §6.1's list of "commands that
may be issued to the Floppy section", headed **(8620/8627 only)** — which agrees
with Table 1-1 giving the 8120/8127 no flexible disks.

Eleven items: READ DATA, FORMAT A TRACK, SCAN EQUAL, SCAN LOW OR EQUAL, SCAN
HIGH OR EQUAL, RECALIBRATE, SENSE INTERRUPT STATUS, SPECIFY, SENSE DRIVE STATUS,
SEEK, INVALID.

**`ap_omti.h` has exactly those** — `SPECIFY 03`, `SENSE_DRIVE 04`, `READ_DATA
06`, `RECALIBRATE 07`, `SENSE_INTERRUPT 08`, `FORMAT_TRACK 0D`, `SEEK 0F`,
`SCAN_EQUAL 11`, `SCAN_LOW_EQUAL 19`, `SCAN_HIGH_EQUAL 1D`, and INVALID as the
default arm. Ten plus the invalid case, matching item for item.

**Including the surprise: there is no WRITE DATA.** A floppy section that can
FORMAT A TRACK and READ DATA documents no write command, and the generic 765's
`05` is absent from both the manual's list and this model. That is not an
omission in the summary — §6.3's command descriptions, which the model is
derived from, do not describe one either. A driver issuing `05` gets §6.3.11's
INVALID here, which is what the manual's own command set implies.

*Recorded because it inverts this record's own warning.* Two summary tables in
this manual have proved not to be censuses — §5.1.2 omitting `1A START/STOP`,
A-2 omitting `07 Multiple Drives Selected` — and the natural next move was to
distrust this one too. It holds, and the way to tell was comparing it against a
model derived from the *descriptions* rather than against the summary alone.


## §3.1-§3.2, HOST ELECTRICAL INTERFACE — what the chapter is, and its one lead

PDF 33, doc 3-1. §3 is **not** a register chapter: "The OMTI 8000 Series Data
Controllers are electrically and mechanically compatible with the bus or
Input/Output channel used in the IBM AT computer", followed by pin assignments
for the two card-edge connectors. §3.4, the one part of this chapter already
derived, is the concurrency sentence and sits among electrical material.

**So the yield here is low by nature**, and that is worth recording before
anyone budgets the remaining pages by count: §1 and §2 are introduction and
installation, §3 is connectors and signals. The register interface is §4, the
commands §5 and §6, and both are done.

**The one lead is that two pins on this connector are open plan items.** The
62-pin component side carries **`A1 -I/O CH CK`** and **`A10 I/O CH RDY`** —
which are exactly `AP_BOARDREG_STATUS_IO_PARITY`'s missing source and
`AP_ATBUS_IO_CH_RDY_MAX`'s unenforced ceiling. The `IO_CH_CK.L` item says
"closing it needs an AT-bus device that can fail", and the disk controller is
the AT-bus device this machine actually has.

*Not read far enough to answer it.* The pin table's Input/Output column marks
both `I`, but a direction column on a connector table does not by itself say
whether this controller ever **drives** a channel check — that would be in §3's
signal descriptions, which are not read. Flagged as the next thing to look at
for that item rather than reported as an answer.


## §3.5's signal descriptions — and they settle §4's contradiction with itself

PDF 37, doc 3-5. Signal descriptions for the AT channel, and two of them matter.

**`DRQ0`-`DRQ3` and `DRQ5`-`DRQ7`**: "`DRQ0` through `DRQ3` will perform
**8-bit** DMA transfers; `DRQ5` through `DRQ7` will perform **16-bit**
transfers. `DRQ4` is used on the system board and is not available on the
Input/Output channel."

**That closes the DRQ3-versus-DRQ7 question from inside this manual.** §4.2's
MASK register says `DRQ3` and §4.3's DATA STATE says `DRQ7`; this project chose
`DRQ7` on the physical grounds that the transfer is word mode and `DRQ3` is
8-bit — an inference from `008778-03` Table 2-4, because `[OMTI]` was thought to
say nothing more. It says exactly this, three sections earlier. **§4.2 is the
error**, and the width argument is now a citation.

*The pattern is the one this walk keeps meeting*: a document that contradicts
itself in one chapter resolves it in another, and the resolution was always
there to be read. `ST3` took the sibling manual; this took the same manual's
own §3.

**`Input/Output CH RDY`**: "pulled low (not ready) by a memory or Input/Output
device to lengthen Input/Output memory cycles ... Machine cycles are extended by
an integral number of clock cycles (**167 nanoseconds**). This signal should be
held low for **no more than 2.5 microseconds**." That 2.5 µs is
`AP_ATBUS_IO_CH_RDY_MAX` exactly, now confirmed from a second document —
`008778-03` §2.3.2 was the first. *The 167 ns is the generic AT's 6 MHz and says
nothing about a DS3500*, which is the open bus-clock question and stays open.

Also on the page and already modelled: the IRQ priority order and the rule that
"an interrupt request is generated when an IRQ line is raised from low to high.
The line must be held high until the microprocessor acknowledges" — the same
sentence `008778-03` §2.3.2 gives and `ap_intr` implements.


## §3.3-§3.4's remaining signals — a negative answer and a second source

PDF 36 and 38, doc 3-4 and 3-6.

**The negative first, because it was the reason for reading these pages.**
`-I/O CH CK` sits at pin `A1` in §3.2's table and has **no entry in §3.3's
signal descriptions**. The list runs SA, LA, CLK, RESET DRV, SD, BALE, I/O CH
RDY, IRQ, IOR, IOW, MEMR, MEMW, DRQ, DACK, AEN, REFRESH, T/C, SBHE, MASTER, MEM
CS16 — and skips it. So this manual **cannot** say whether the OMTI drives a
channel check, and the `IO_CH_CK.L` item stays blocked with one more source
explicitly checked and recorded as silent rather than merely untried.

*That is a fourth list in this manual that is not a census*, and the first where
the omission cost a question rather than hid a feature.

**`-MASTER` confirms all three of `ap_master.h`'s figures**, in its own words:
"After `-MASTER` is low, the Input/Output microprocessor must wait **one system
clock period** before driving the address and data lines, and **two clock
periods** before issuing a Read or Write command. If this signal is held low for
**more than 15 microseconds**, system memory may be lost because of a lack of
refresh." One and two clocks at 167 ns are Table A-1's #75 and #76; the 15 µs is
§2.3.2's threshold. A third-party controller manual and Apollo's own reference
agreeing exactly is worth having for figures this core cannot yet enforce.

**`CLK` is the generic AT's**: "the **6-MHz** system clock ... cycle time of 167
nanoseconds ... **not intended for uses requiring a fixed frequency**." Which
settles that the 167 ns on the previous page is the standard AT and not a
DS3500 figure — the open bus-clock question is untouched by this manual.

Also confirmed and already modelled: `T/C` "provides a pulse when the terminal
count for any DMA channel is reached"; `-SBHE`/`SA0`'s encodings, `00` word and
`01`/`10` byte on the two halves; and SD's "16-bit microprocessor transfers to
8-bit devices will be converted to two 8-bit transfers", which is
`atbus_suite`'s wide-to-narrow test.


## §2.3, JUMPER ALLOCATION — and it explains a byte this project had only measured

PDF 14, doc 2-4. The **8620 DRIVE CONFIGURATION TABLE**, with "0 = No jumper
installed, 1 = Jumper installed":

| `W20 W21` (LUN 0) / `W22 W23` (LUN 1) | drive |
| --- | --- |
| `1 1` | **ESDI DRIVES** |
| `0 1` | VERTEX/PRIAM V170, 987 cyl, 7 heads |
| `1 0` | MAXTOR XT1140, 918 cyl, 15 heads |
| `0 0` | MINISCRIBE 3425, 612 cyl, 4 heads |

**This explains `ap_omti_disk_reset`'s `configuration = 0xFC`.** That byte was
measured off the oracle years ago as part of the idle-controller reading
`FF C0 FC 00` and has been carried as a measurement ever since. §4.2 gives the
register's layout — bits 7-4 "not used (Set to 1)", bit 3 `W20`, bit 2 `W21`,
bit 1 `W22`, bit 0 `W23` — so `FC` is `1111 1100`, which is `W20 W21 = 1 1` and
`W22 W23 = 0 0`.

`1 1` is **ESDI**. So the Apollo controller is strapped for an ESDI drive on
LUN 0, which is exactly what every other part of this model already assumes:
`ap_omti_cdb_accepted_by_esdi`, the three ESDI-only commands, and `008778-03`
§6.3's ESDI drive list. LUN 1's `0 0` is the un-jumpered state, and no drive is
fitted there.

*A byte read off the oracle now agrees with a jumper table read off the manual.*

> **BOUNDED 2026-08-22, from `[8000]` doc 2-3.** Its jumper table gives the same
> four bits' meaning as "**(Host assigns jumper value)**", and its §4.2 says the
> register is "typically ... used by a **BIOS** to specify the type of drive(s)
> attached". §2.3's own table is headed `BIOS #1002579 / BIOS AT3` — one BIOS's
> convention, printed under that BIOS's number.
>
> The **decomposition** is certain: four jumpers, two fitted, two not. The
> **meaning** is a convention between the strap and whatever firmware reads it,
> and here that firmware is Apollo's boot PROM rather than OMTI's AT3 BIOS. So
> "LUN 0 is ESDI" is what OMTI's BIOS would conclude, it agrees with everything
> else this model knows about the drive, and that agreement is corroboration —
> not a statement the manual makes about *this* machine. Nothing depends on it;
> the value is measured either way. The correction is here so a later reader
> does not derive something new from "the manual says ESDI on LUN 0".
That is the strongest confirmation available for a strap this project cannot
probe, and it came from the installation chapter — the one this record had
written off as low-yield two commits ago.


## §2.2's PCB diagram — mechanical, but it resolves a naming conflict

PDF 13, doc 2-3. **Figure 2.2, OMTI 812X PCB Diagram and Connector, Jumper
locations** — board dimensions, connector positions J1-J4, and jumper locations
W1-W13, W20, W27, W28, W29, W30-31. Physical, and **for the 812X**, not the
862X this machine has, so the positions are not ours.

*The yield is the parts list drawn on it*: **OMTI 5050**, **OMTI 5060**, **OMTI
5015**, **OMTI 5090**, a **Z8**, a **FIRMWARE** device and a separate **BIOS**,
and an "OMTI SDM ENC/DEC/VCO".

**That resolves the fourth name conflict this record logged.** Appendix A-2's
table calls sense code `31` "Z8 firmware checksum/internal diagnostic error" and
A-5's description calls it "EPROM Checksum/Internal Diagnostic error", which
looked like the same in-document inconsistency as `ST3` bit 4 and `14`
sector-versus-record. It is not: the figure shows a **Z8** microcontroller and a
**FIRMWARE** part beside it, so the Z8's firmware lives in an EPROM and both
names describe one checksum over one device. Two names, one thing, and the board
layout is what says so.

*So of the four name conflicts recorded in this manual, one is now explained
rather than merely noted* — and it took a mechanical figure in the installation
chapter to do it, which is the second time §2 has paid after being triaged as
low-yield.


## §2.3's other three tables — and the low nibble names the controller family

PDF 15-17, doc 2-5 to 2-7. §2.3 is **four** tables, not the one this record
walked: **8620** (2-4), **8120** (2-5), **8627** (2-6), **8127** (2-7), across
two BIOS revisions — **AT3, #1002579** for the 8620/8120 and **AT4, #1002580**
for the 8627/8127.

| controller | LUN 0 / LUN 1 jumpers | `1 1` | `0 1` / `1 0` / `0 0` |
| --- | --- | --- | --- |
| 8620 (AT3) | `W20 W21` / `W22 W23` | **ESDI DRIVES** | Vertex/Priam V170, Maxtor XT1140, MiniScribe 3425 |
| 8120 (AT3) | `W1 W2` / `W3 W4` | **RESERVED** | the same three |
| 8627 (AT4) | `W20 W21` / `W22 W23` | **ESDI DRIVES** | Seagate ST277R, ST4144R, ST238 |
| 8127 (AT4) | `W1 W2` / `W3 W4` | **RESERVED** | the same three Seagates |

**Two findings, and the first is a property of the byte this core answers with.**

*The low nibble identifies the controller family.* The 86xx pair strap `W20`-`W23`
and the 81xx pair strap `W1`-`W4`, but both are the same four bits in the same
register, and `1 1` means **ESDI DRIVES** on one family and **RESERVED** on the
other. So `FC` — `W20 W21 = 1 1` — is not merely "LUN 0 is ESDI": it is an
encoding that *only an 862X can legally report*. A host reading `FC` off an
812X would be reading a reserved configuration. The byte does double duty, and
`ap_omti.h`'s note that this register is the 8620/8627 straps is the reason it
can be read at all.

*And every jumperable drive type carries a geometry except ESDI.* All four
tables print `#CYL`, `#HEADS` and `WRITE PRECOMP` per row, filled in for the six
named ST506 mechanisms and **blank on both ESDI rows**. That is the documentary
form of a split `ap_omti_cdb.h` already implements: `INITIALIZE DRIVE
CHARACTERISTICS` (`0C`) is listed under §5.1.2's "COMMANDS SPECIFIC to the
ST506/412 drives" and is rejected here, because an ESDI drive reports its
geometry to `READ CAPACITY` rather than being told it. The tables corroborate
that from the installation end: on an ST506 controller the geometry is *strapped
into the board*, on an ESDI one there is nowhere to strap it.

The AT3/AT4 drive lists differ entirely — the same two bits select a MiniScribe
3425 under one BIOS and a Seagate ST238 under the other — which is a third
reason the configuration byte is not a geometry source under any reading.

*Nothing here changes behaviour.* It converts one implementation decision and one
measured byte into cited facts. Coverage: §2.3 is now whole, 2-4 through 2-7.


## §2.4, INSTALLATION PROCEDURE — the strap this walk owed, and the two connectors

PDF 21, doc 2-11. Five subsections, and two of them are hardware facts.

**§2.4.1, Winchester drive configuration (8620 & 8627 only).** Two Winchester
connectors, **`J3`** and **`J4`**, one drive each: "up to two (2) ESDI
Winchester drives **or** up to two (2) ST412". Mixing is allowed but requires
cutting a trace jumper, and the pairing is stated per connector — "if an ESDI
drive is connected on **J4** ... **`W13`** shall be cut", "on **J3** ...
**`W12`**". So the board carries a per-connector ESDI/ST412 strap distinct from
§2.3's per-LUN drive-type bits, and the two are cut rather than fitted.

> **CORRECTED on the next page.** "Two Winchester connectors, `J3` and `J4`, one
> drive each" is true of the **data** cable only, and reads as if it were the
> whole drive interface. §2.4.6 step 4 gives the cabling: "Install the **34-pin
> winchester drive interface cable to the `J2` connector**. Install the **20-pin
> data cable to either the `J3` or `J4`** connector. Install the 34-pin floppy
> drive cable to the `J1` connector" — and §2.4.6 step 1 sizes them, `J1`/`J2`
> 34-pin, `J3`/`J4` 20-pin. It is the standard ST506/ESDI split: **one shared
> control cable on `J2`**, daisy-chained, plus **one radial data cable per drive
> on `J3` or `J4`**. §2.4.7 confirms it from the other side by calling the
> 34-pin cable a "daisy chain" and the 20-pin one "straight through".
>
> *The original text is kept above because the error is instructive*: §2.4.1 is
> titled "drive configuration" and names only `J3` and `J4`, so a reader who
> stops at the section that appears to be about connectors gets a wrong picture
> of the cabling. The correction is one page later, in a procedure.
>
> **And which of `J3`/`J4` is which LUN — left open here — is answered by
> `[8000]` Table 2-2**: "`J2` FIXED DISK (34 pin), `J3` FIXED DISK - **LUN 0**
> (20 pin), `J4` FIXED DISK - **LUN 1** (20 pin), `J7` FLOPPY DISK (34 pin)".
> One shared 34-pin control connector and one radial 20-pin data connector per
> LUN, named by LUN. (The floppy is `J7` on the 8000 series where the 812X/862X
> put it on `J1` — a family difference, like the jumper numbers.)

**§2.4.2, Floppy Support (8620 and 8627 only).** "The OMTI 8000 controller
provides floppy disk support which is fully AT bus and hardware compatible.
Therefore, you may remove the AT Winchester/floppy controller (if installed) and
connect the floppy cable to connector **`J1`**." *That is this project's combined
controller stated as a product feature* — one card serving both surfaces, which
is the arrangement `ap_omti` models and the reason a single part has Table 4-1
and Table 4-3.

**§2.4.3 closes an open question this record has carried since the §4 walk.**
Where an AT controller is left in place to serve an existing non-ESDI Winchester,
"only one of the two controllers should control the floppy drive. To avoid
conflicts ... the OMTI controller must be strapped for the **secondary** floppy
base I/O address (**`W14`** on)."

So Table 4-3's `372`-`377` is reachable, by a strap, and the question of whether
this board could sit at the secondary base is answered yes. It changes nothing:
a DN3500 has no second floppy controller to conflict with, `002398-04` places
its floppy at `3F0`, and `W14` has no software path — nothing the emulated
machine executes can move it. `ap_omti.h` now records the base as a strap with
its citation instead of as an unexplained constant.

*The method note is the section it was found in.* This walk expected the answer
in §2.3, JUMPER ALLOCATION, and logged it as owed there. §2.3 turned out to
document only the drive-type bits; the base-address strap is in the installation
procedure. A reader who had walked the jumper chapter and stopped would have
concluded the manual was silent — which is the whole-document rule's case,
arriving from the direction that makes it least visible.

> **WRONG, and the correction is worse than the claim.** Doc **2-8**, COMMON
> SYSTEM JUMPER SETTINGS for 8620 and 8627, gives `W14` its own row *with both
> values* — `0` = `03F0h` as shipped, `1` = `0370h`. The strap is in the jumper
> tables after all, one page before the installation procedure, and §2.4.3 only
> says when to move it.
>
> *And this project had already cited that exact page.* `ap_omti_cdb.h` quotes
> "the jumper table under COMMON SYSTEM JUMPER SETTINGS on page 2-8" for the
> `W10 W11` sectors-per-track rows, and has done for as long as the address
> conversion has been modelled. The page was read for one row and not for the
> rest of the table — which is `read-the-whole-document`'s failure in miniature,
> committed by the walk that exists to prevent it, on the same day it wrote the
> rule down. The real lesson is narrower and sharper than the one above: **a page
> already cited is not a page already read**, and a citation to a specific row is
> evidence of the opposite.

**§2.4.4** is AT-specific and inapplicable here: IBM's `SETUP` must report
**zero** hard disks for drives on the OMTI, because the controller's own ROM
BIOS owns them. **§2.4.5** names the BIOS revisions again — AT3 `#1002579`, AT4
`#1002580`, "the latest BIOSES available" — and requires `OMTIDISK` V3.0 or
later for auto-configuration, defect handling and low-level formatting.


## §2.4.6-2.4.7 — the cabling, and the family split stated rather than inferred

PDF 22-23, doc 2-12 and 2-13. Two installation procedures, one drive and two.

**The cabling, which corrects §2.4.1 above.** `J1` and `J2` are 34-pin (AMP
88373-3), `J3` and `J4` are 20-pin (AMP 86904-1). `J2` takes the shared 34-pin
Winchester control cable — "daisy chain" in the two-drive case — `J3` and `J4`
take one 20-pin radial data cable each, and `J1` takes the floppy. Drives are
distinguished on the daisy chain by drive select: one drive is `DS0`/`DS1`, two
drives are "**`DS1` (or `DS0`) on drive `C:`**" and "**`DS2` (or `DS1`) on drive
`D:`**", with the terminating resistor left only on the drive at the end of the
chain and removed from the one in the middle.

**§2.4.7 step 3 states the family split this record inferred yesterday.**
"install jumpers **`W20` to `W23` for the 8620 and 8627**, install jumpers
**`W1` to `W4` for the 8120 and 8127**, according to BIOS drive table." The
§2.3 entry above derived exactly that by comparing four tables and noticing the
jumper names changed with the controller; here it is in one sentence of prose.
*Both routes were needed*: the tables give what each encoding **means** — and
that `1 1` is ESDI on one family and `RESERVED` on the other, which is what
makes the configuration byte family-identifying — and this sentence gives the
rule. Neither page carries both halves.

**And a limit on the drive table.** The jumpers are to be set "if **both drives
are the same** and the drive is listed in the drive table"; if the drives differ,
or either is unlisted, the step is skipped and the low-level format routine's
`"Use defaults (Y/N)?"` prompt is answered `N`. So although the four bits are two
per LUN and could encode two different types, the supported path does not use
them that way.

*Not applicable to this machine, recorded for completeness*: the BIOS low-level
format is entered from DOS `DEBUG` with `g=c800:6`, placing the option ROM at
segment `C800` offset 6; partitioning is `FDISK` and `FORMAT C:/S`. A DN3500 has
no x86 and none of this runs, but it is what §2.4.5's "Rom-resident BIOS
initialization routine" refers to.

**A table-of-contents defect.** §2.4.6 step 5 says "Read section **2.9** entitled
IBM DOS 3.1/3.2 PATCH INSTALLATION NOTE", where the contents page lists that note
as **§2.6**. The body's cross-references and the contents page disagree about §2's
own numbering, so §2 runs further than the contents page suggests and the
remaining subsections must be found by reading rather than by index — the same
trap `002398-04` set with its topical page numbers.


## Doc 2-14's format flowchart — a third printing of the step table, and a range

PDF 24. **LOW-LEVEL FORMAT ROUTINE INSTRUCTIONS**, a flowchart of the BIOS
routine entered from DOS `DEBUG` with `g=c800:6`. Four things in it.

**`CONTROL BYTE STEP OPTIONS`, printed in full**: `0` = 3 ms per step, `1` = 10
µs buffered, `2` = 25 µs buffered, `3` = 50 µs buffered, `4` = 200 µs buffered,
`5` = 70 µs buffered, `6` and `7` = 3 ms per step. That is the **third**
independent printing of this table — §5.2's p. 5-4 and `002398-04` p. 12-10 are
the other two — and it agrees with both, value for value, including the two
duplicate 3 ms rows at the top and bottom of the encoding.
`ap_omti_cdb.h` already carries all eight with `002398-04`'s cross-check and
its one Apollo-specific difference (`001` marked N/A). Nothing to change; the
value of a third source is that a table this project reads off page images is
now confirmed by three typesettings.

**`INTERLEAVE (1-15)`** — the utility's prompt range. It is *not* the
controller's rule, which Appendix A-4 gives as "greater than the number of
sectors on the track", and the difference is why `interleave_ok()` accepts a
factor of zero: the utility never offers it, the controller never refuses it,
and only the second is a fact about the part. The gap between a tool's range and
a device's rule is a standing invitation to invent a refusal.

**`LOGICAL PARTITIONING DESIRED (Y/N)?` → `TOTAL CYLS IN 1ST LOGICAL UNIT`.**
One *physical* drive can be formatted as two logical units. So a second LUN does
not imply a second spindle, and the LUN field is not simply a drive-select. This
core attaches one drive at LUN 0 and has no partitioning path; recorded as a
capability of the part that this machine does not use, not as a gap — Domain/OS
addresses its volume through the descriptor block, and nothing in the boot has
ever selected LUN 1.

**`DRIVE # (0 OR 1)?`** confirms two units, and the defect-entry loop
(`CYLINDER:` / `HEAD:` repeated until a bare `<RET>`) is the host side of the
`ASSIGN ALTERNATE TRACK` path `ap_omti.c` already implements.


## Doc 2-8 and 2-9, COMMON SYSTEM JUMPER SETTINGS — the whole table, both families

PDF 18 and 19. Two tables of the same shape, **2-8 for the 8620 and 8627** and
**2-9 for the 8120 and 8127**, differing only in the jumper numbers. `*` marks
as-shipped.

| function | 862X | 812X | values |
| --- | --- | --- | --- |
| Winchester I/O port base | `W19 W18 W17` | `W5 W6 W7` | `0320h*`, `0324h`, `0328h`, `032Ch` with the third jumper out; `01A0h`, `01A4h`, `01A8h`, `01ACh` with it in |
| BIOS control | `W16` | `W8` | `0*` Enable BIOS, `1` Disable BIOS |
| BIOS base address | `W15` | `W9` | `0*` `C8000h`, `1` `CA000h` |
| Floppy I/O port base | `W14` | — | `0*` `03F0h`, `1` `0370h` |
| ESDI per connector | `W13`, `W12` | — | "See section 2.4.1" |
| Bytes/sector and sectors/track | `W10 W11` | — | `0*0` 512/17, `0 1` 512/18, `1 0` 1024/9, `1 1` 1056/9, the sector count "ST506/412 MFM drives only" |

**This board's strapping is now readable off the table, and it is not the
as-shipped one.** `board/ap_disk.h` measured the fixed disk at Apollo `04D000`,
which its own `Apollo = 0x040000 + AT × 0x80` rule puts at AT **`01A0h`** — the
`W19 W18 W17 = 0 0 1` row, the first of the second bank. The floppy is at
`03F0h`, `W14` out. And `W10 W11 = 0 1` is the 512-byte, 18-sector row that
`ap_omti_cdb.h` had already established from the two Apollo drive geometries.

*So an address this project found by scanning the entire AT I/O window with the
card fitted and with `isa1` emptied is one of eight rows in a jumper table.* The
scan was not wasted — it is what proves which row — but the table is what makes
the result a configuration rather than a coincidence, and it was on disk the
whole time.

**The 812X table has no floppy row, no `W12`/`W13` and no `W10`/`W11`.** That is
the fourth independent statement of the family split: the ST506 boards have no
floppy support (§2.4.2 says so directly, "8620 and 8627 only"), no ESDI
connector straps because they have no ESDI, and no sector-size jumpers.


## §5.4.1, §5.4.2 and §5.4.4 — the two subsections the coverage row excluded

PDF 51-54, doc 5-5 to 5-8. This record's audit row for §5 said "37 distinct
subsections, **§5.4.3 through §5.4.29**", which was accurate and, read as a
coverage claim, silently excluded the first two. Both were unread. One held a
number.

**§5.4.1, TEST DRIVE READY (`00h`).** "This command selects the LUN specified
and returns a zero status in the Status Register to indicate that the unit is
selected, ready and seek (ST drives) or seek/command (ESDI drives) is complete.
In the case of a unit with a removable disk, zero status also indicates that a
cartridge is installed. **The controller will wait up to 50 seconds for the
drive to come ready.**" Implemented — see `PROJECT_STATUS.md`, *TEST DRIVE READY
answered in zero time*. §2.5's `1701-C` prints the sentence a second time, and
finding it there is what sent this walk back to §5.4.1 to check.

**§5.4.2, RECALIBRATE (`01h`).** Two definitions, by drive type. *ST drives*:
stepped toward the outside cylinder until "1. Track Zero signal is detected or
2. **More steps have been issued than available cylinders for the device
type**", the controller "issues one step pulse, waits for seek complete, and
tests the Track 000 signal"; for a removable LUN it issues "step pulses equal to
the number of cylinders specified for this drive **plus 5** at the buffered rate
and then waiting for the Track 000 signal". *ESDI drives*, the whole of it:
"This command selects the LUN specified and issues a recalibrate to cylinder
zero." **This machine is ESDI, so the one-sentence definition is the applicable
one**, and it is what `ap_omti.c` does. The ST paragraph is recorded because it
names a *failure* — a drive whose Track Zero never arrives — that the ESDI text
does not have, and because the step-count rule is the only place the manual says
what terminates an unsuccessful recalibrate.

**§5.4.4's Interleave Factor and Track Skewing**, which corrected an item landed
hours earlier — the fifth self-disagreement in this manual, and the first that
had already been coded the wrong way. "An interleave factor of zero is set equal
to one and is the fastest. **Interleave factors greater than or equal to the
number of sectors per track are illegal.**" Appendix A-4 says "greater than".
And §5.4.4's descriptor table splits byte 4 into `TRACK SKEWING` (bits 7-4) and
`INTERLEAVE FACTOR` (bits 3-0), which the check had compared whole. Detail in
`PROJECT_STATUS.md`.

*Track skewing is defined here and nowhere else*: "a scheme implemented to
improve access time when switching heads while transferring multiple blocks ...
avoids loosing a disk revolution when switching heads. With a track skewing of
zero, the first sector after index is always sector zero. With a track skewing
different than zero, only on head zero is the first sector after index the
sector zero. The physical location of the sector zero on the subsequents heads
is offset by the skew value from the previous head." A worked example follows
for skew 1, interleave 3, over three heads. Decoded and inert in this model, for
the one reason that also makes the interleave value inert: there is no rotation.

**A gap this page opens.** §5.4.3's `SENSE DATA WORD FORMAT` gives a 16-bit view
of the four sense bytes — word 0 is `C10 | 0 | LUN | HEAD | SENSE CODE`, word 1
is `CYLINDER LOW | C09 C08 | SECTOR` — beside the byte format this core builds.
Whether the word view is a different *packing* or the same bytes read as words
is not stated on the page, and the two are not the same: the byte order within
each word decides it. Named as an open question rather than guessed.

**The method note.** A coverage row that names a *range* is a claim about its
endpoints, and this one's lower endpoint was never checked — it was written from
what the code happened to cite, and the code cited §5.4.3 first because REQUEST
SENSE is what a driver reaches for. *Sections the model never needed are exactly
the sections a citation-derived coverage claim cannot see.*


## §3, HOST ELECTRICAL INTERFACE — generic ISA, and one sentence that matters

PDF 33-40, doc 3-1 to 3-7. Walked. Most of it is the IBM AT I/O channel
reproduced: §3.1 introduction, §3.2's four pin tables (62-pin component and
solder sides, 36-pin component and solder sides — `IRQ14` at `D7`, `DRQ5`/`6`/`7`
at `D11`/`D13`/`D15`, the standard AT assignment), and §3.3's signal
descriptions. It is the host's bus, not the controller's behaviour, and this
core models the register interface rather than the edge connector.

**Two things in it are worth having.**

*The 167 ns is confirmed as generic.* §3.3's `CLK`: "This is the **6-MHz** system
clock ... a cycle time of **167 nanoseconds**." That is the IBM AT's number
reproduced in a third-party manual, not a measurement of anything Apollo built,
and this record already reasoned so from `008778-03` §2.3.2. It is now confirmed
from the OMTI's own side, and it does **not** unblock the DMA-transfer-duration
item, which needs the DS3500's bus clock and not the AT's. §3.3 also gives `OSC`
as 14.31818 MHz with a 70 ns period, and §3.1's electrical rule — "a maximum of
two low-power Shottky (LS) loads per line".

*§3.4's first sentence, which this project had never quoted and which changes
how its own model reads.* The header of `ap_omti.h` quoted "This allows full
concurrent operations between these two sections"; the sentence before it is:
"The OMTI 8000 series is partitioned into **three** distinct sections - the
floppy disk logic and the Winchester disk logic and the **QIC 36 section**. **The
first two sections share the same physical PCB board** but are otherwise
independent."

So "these two sections" is two of three, and the qualifier "the first two ...
share the same physical PCB board" is precisely what licenses this core's
two-half model — quoting only the second sentence removed the justification and
left the conclusion. The third section is absent from this board rather than
unmodelled: §2's connectors are `J1` floppy, `J2` Winchester control, `J3`/`J4`
Winchester data, and Figure 2.2 draws the same four with no tape connector. The
distinction matters because "absent from the part" needs no `PROVISIONAL` and
"unimplemented" does.

*And the tape is a different interface anyway*: QIC-**36** is a drive interface,
QIC-**02** is a host interface, and Apollo's tape is the latter on its own part.

**Coverage note.** §3.5 onward, if any, has not been reached; doc 3-7 ends with
§3.4's three sentences and the page is otherwise blank, which usually means a
section boundary. §3.1-§3.4 are walked.


## §6.1 and §6.2 — the command list confirms, and the symbol list opens a gap

PDF 74-75, doc 6-1 and 6-2. §6 is FLOPPY DISK FUNCTIONS and these two are its
front matter; §6.3 was already derived and §6.4 walked.

**§6.1, FLOPPY DISK COMMAND SUMMARY (8620/8627 only) — a clean confirmation.**
Eleven entries: READ DATA, FORMAT A TRACK, SCAN EQUAL, SCAN LOW OR EQUAL, SCAN
HIGH OR EQUAL, RECALIBRATE, SENSE INTERRUPT STATUS, SPECIFY, SENSE DRIVE STATUS,
SEEK, INVALID. `ap_omti_fdc_command_t` has exactly the ten and the INVALID path,
and — the part worth checking — **the list has no WRITE DATA**, which is the
reading `ap_omti.h` already argued for from §6.3 and the 8640's §5.3 against the
general 765 command set. A third statement of it, and this one is a summary
written to be complete.

**§6.2, DESCRIPTION OF SYMBOLS — and `SRT` is a gap.** Most of it defines terms
the model already uses (`C`, `D`, `DTL`, `EOT`, `GPL` as "the length of gap 3
(spacing between sectors excluding the VCO synchronous field)", `H`, `HD`, `MF`,
`MT`, `N`, `NCN`, `ND`, `PCN`, `R`, `SC`, `SK`, `ST0-ST3`, `US0-US1` "encoded the
same as bits 0 and 1 of the digital output register"). Three do not.

`SRT` — "This 4 bit byte indicates the stepping rate for the diskette drive" —
with two tables, `1111`/`1110`/`1101` mapping to 1/2/3 ms on a **1.2M-byte**
drive and 2/4/6 ms on a **320K-byte** one. `HLT`, head load time, 2 to 256 ms in
2 ms increments (1.2M) or 4 to 512 ms in 4 ms (320K). `HUT`, head unload, 0 to
240 ms in 16 ms or 0 to 480 ms in 32 ms.

*So the floppy step rate is software-programmable and this core treats it as a
constant.* `AP_OMTI_FDC_TRACK_TO_TRACK` is `008778-03` Table 7-7's 3 ms drive
minimum — right for this machine, since the composition reproduces that table's
own 94 ms average to 0.9%, which is evidence Domain/OS programs the rate that
gives 3 ms. The mechanism is what is missing. Marked `PROVISIONAL` in the header
and opened as a plan item, blocked on two facts §6.2 does not supply: it prints
three of sixteen `SRT` rows, and it does not say which of its two drive types
this board's floppy is — a choice that doubles or halves every seek.

> **CLOSED the same day, and both blockers were self-inflicted.** The drive type
> was already recorded in `ap_omti.h`'s own floppy section — 360 rpm, 96 TPI,
> and the oracle's `DSHD`, which is the 1.2 Mbyte drive — so the file that
> raised the blocker contained its answer. The thirteen unprinted `SRT` rows
> follow by arithmetic from the three printed ones, `(16 - SRT)` ms, and
> `[8640]` §5 was read first and prints the same three and no more. Implemented
> with Table 7-7's 3 ms as a floor; detail in `PROJECT_STATUS.md`.
>
> *Fourth time in one day.* A question was opened by reading a new document and
> closed by re-reading something this project had already written — after the
> SIO line-0 question, the OMTI coverage row, and doc 2-8's jumper table. The
> pattern is specific enough to state as a rule: **before recording a blocker,
> grep this core's own headers for the fact.** A blocker is a claim about what
> is not known anywhere, and it is cheapest to falsify at home.

`STP`, the scan test parameter, is the fourth thing here: "If STP is 1, the data
in contiguous sectors is compared with the data sent by the processor during a
scan operation. If STP is 2, then alternate sectors are read and compared."

*§6 is now walked whole*: §6.1 and §6.2 here, §6.3 derived, §6.4 walked.


## `[8640]` — a sibling for the floppy only, and its Winchester chapter is a trap

Surveyed 2026-08-22. **It has a text layer**, which makes it far cheaper to work
than `[OMTI]`, and that is a reason to be careful with it rather than a reason to
trust it.

**Its Winchester interface is not this controller's.** §4's registers and
commands are the **AT task file** — "Sector Count Register IF2 (172)", `20H`
Read Sector, `30H` Write Sector, `50H` Format Track, `40H` Read Verify, `90H`
Diagnostic, `91H` Set Parameters — where the 862X takes six-byte Command
Descriptor Blocks through a single data port. Two different host interfaces to
similar drives. *So §4 must never be used to settle an 862X question*, and
anything this record or the model cites from `[8640]` has to come from its floppy
chapter or its drive-side material. Recorded because the text layer makes §4 the
easiest thing in either manual to grep, and a grep does not say which controller
it landed in.

**Its §5 is the sibling, and it confirms three things without adding any.**

- §5.4's `SRT` table is **word for word** `[OMTI]` §6.2's, three rows of sixteen
  and no more. Checked before the `(16 - SRT)` arithmetic was relied on; the
  sibling route is exhausted, not skipped.
- §5.6.1's `ST0` gives "Bit 3 and 2 Not Used - Always zero", which is the
  reading `ap_omti.h` argues at length against `002398-04` p. 8-13's
  head-address bit — and this is a third document agreeing with `[OMTI]` §6.4.1.
- §5.6.1 bit 4, Equipment Check: "Set if a 'fault' signal is received from the
  diskette drive, or if the 'track-0' signal fails to occur after **77 step
  pulses**". `AP_OMTI_ST0_EQUIPMENT`'s comment already carries that number.

**And one sentence from §2.4** (Winchester Track and Sector Format, soft-sectored
ESDI): "The number of sectors per track on most ESDI drives **varies by
vendor**." A fourth statement that ESDI geometry is interrogated rather than
configured — after §2.3's blank `#CYL`/`#HEADS` on the ESDI rows, §5.1.2's
placing of `INITIALIZE DRIVE CHARACTERISTICS` under the ST506 commands, and
§5.4.29's `READ CAPACITY`.

*So `[8640]` has yielded no unimplemented fact, and the whole-document rule is
not triggered by it.* That is a claim about what a survey found, not a guarantee;
it is recorded with what was read — §2.4, §4's section list, §5.3-§5.6 — so a
later reader can see the shape of the search rather than only its result.


## `[8000]` — the walk is open, and its first two pages have already paid

Opened 2026-08-22, **mandatory** under the whole-document rule: this manual was
recorded as "cited once — effectively unconsulted", and the first page read in
it settled a question `[OMTI]` had left unanswerable.

**What it is.** Document **3001241, Revision D, 20 June 1986** — a *different
manual* from `[OMTI]`'s 3001483 of January 1987, not an earlier printing of it.
Its revision note reads "List of changes from previous revision: - Addition of
the READ ESDI DEFECT LIST command". Contents show the same six-section shape —
Introduction, Configuration and Installation, Host Electrical Interface,
Host/Controller Software Interface, Fixed Disk Functions, Floppy Disk Functions
— plus Appendices A (Sense Code Summary), B (Interleave Scheme) and C (**BIOS
installation procedure**, which `[OMTI]` does not have).

**No text layer**: 71 bytes for 71 pages. The scan is faded and needs 200 dpi.
Doc `A-1` is PDF 60, so the appendices sit at the end as usual; §6.4 is doc 6-7
at PDF 59.

| page | section | yield |
| --- | --- | --- |
| PDF 5 | contents | structure, and Appendix C is unique to this manual |
| PDF 59, doc 6-7 | §6.4.3 `ST2`, §6.4.4 `ST3` | **closes a route** — see below |
| PDF 60, doc A-1 | Appendix A, sense byte and **word** formats, `SENSE TYPE` | **two findings**, detail in `PROJECT_STATUS.md` |
| everything else | | *owed* |

**§6.4.4 closes the documentary route on `ST3` bit 4.** The stopped-spindle plan
item named "a third OMTI manual" as one of three things that could settle
whether bit 4 is Track 0 (its name) or drive-ready (its description). This is
that manual, and it carries the identical eight lines — bit 5 "Not used - always
zero", bit 4 "Track 0 (TO) - Status of the 'ready' signal from the diskette
drive", bit 0 "Not used - always 1". *Three documents, three years, two product
families, one wording.*

That cuts both ways and is recorded as such. It weakens the "typesetting slip"
reading, which would not survive being reset for a different product twice; and
it equally kills the hope that a fourth OMTI manual would help, because they are
evidently one source text. What is left is a driver that reads the bit or a
machine to probe — and Domain/OS never issues `SENSE DRIVE STATUS`.

*§6.4.3's `ST2` was read on the same page and matches `[OMTI]` §6.4.3 field for
field* — `CM`, `DD`, `WC`, `SH`, `SN`, `BC`, `MD`, bit 7 not used — including
the two Scan-command bits, which is a confirmation that this core's `ST2`
constants are the OMTI's rather than the generic 765's.


## `[8000]` §4 and §5.1.2 — and the finding that changes how to read all three

PDF 21 and 28, doc 4-2 and 5-2. Doc `4-N` is PDF `N+19`, doc `5-N` is PDF
`N+26`, doc `6-N` is PDF `N+52`.

**Table 4-1** gives the four ports at `320H`-`323H` with the same read/write
asymmetry `ap_omti.h` models — DATA IN/DATA OUT, STATUS/RESET, CONFIGURATION/
SELECT, N/A/MASK. `320H` is §2's as-shipped base, so the table is written for
the unjumpered board.

**Table 4-2 confirms the sense-word packing from a second passage.** Data In:
"When C/D is 0 all 16 bits are valid with **bits 8-15 containing byte 1 and bits
0-7 containing byte 0**", and Data Out says the same. That is Appendix A-1's
bit-numbered layout independently restated, so the packing recorded in
`ap_omti_cdb.h` now rests on two passages rather than one.

*And a third passage on the same page contradicts both.* The NOTE under Status
bit 2: "When C/D is 0 then all 16 bits ... are valid. In this case **byte 0 is
in bits 8-15 and byte 1 is in bits 0-7**." Exactly inverted. Two against one,
with the two including the only bit-numbered table, so the majority reading is
taken and the outlier is recorded rather than reconciled.

**Two sentences worth keeping.** Data In "is used to read data a word at a time,
or status (**not SENSE data**) a byte at a time" — so sense travels as *data*,
in words, not as status bytes; unreachable here, since Apollo decodes the
registers as bytes, but it is the behaviour behind the word format existing at
all. And the Status description names the system lines: `IREQ` "is set with
**IRQ14** on the System", which `board/ap_disk.h` had only from `008778-03`
Table 2-3, so the controller's own manual now agrees with Apollo's.

**§5.1.2's COMMAND SET SUMMARY confirms the command set** — twenty-four common
commands, `0C INITIALIZE DRIVE CHARACTERISTICS` alone under ST506/412, and
`10`/`37`/`EC` under ESDI, with every code and length matching `ap_omti_cdb.h`,
including `READ ESDI DEFECT LIST`'s **256 bytes** and `READ CAPACITY`'s ten. It
**also omits `1A START/STOP`**, exactly as `[OMTI]` §5.1.2 does — the same
summary missing the same command in two manuals a year apart.

## The finding: these three manuals are one source text, so agreement is cheap

Three things line up today and they say the same thing.

1. `ST3` bit 4's name-versus-description contradiction appears **verbatim** in
   `[8000]` (1986), `[OMTI]` (1987) and `[8640]` (1989).
2. `[8000]` Table 4-2 says the controller asserts **`DRQ3`**, which is exactly
   `[OMTI]` §4.2's statement — the one `[OMTI]` §4.3 contradicts and §3.5's
   8-bit/16-bit width rule overrules. A year older, and copied forward.
3. Both `[8000]` and `[OMTI]` omit `1A START/STOP` from the same summary table.
4. `[8640]` §5.4's `SRT` table is word for word `[OMTI]` §6.2's, three rows of
   sixteen in both.

*So when the resolution order says "the sibling manuals already on disk", this
vendor's siblings are a weaker step than that rule assumes.* They resolve
**typesetting** questions — a table that is illegible in one scan is legible in
another — and they do **not** independently corroborate a claim, because the
claim is the same sentence set twice. Where two OMTI manuals agree, that is one
witness.

The practical rule: **prefer a different passage in the same manual over the
same passage in a sibling.** `DRQ7` is settled by §3.5's width rule and
`008778-03`'s Table 2-4, which are independent of each other and of the copied
sentence; the sibling's agreement with the error added nothing. And it is why
the `ST3` route is closed rather than merely unexhausted: a fourth OMTI manual
would print the fifth copy of the same eight lines.


## `[8000]` §4.3 to §4.5 — one table captured, everything else confirmed

PDF 22, 24 and 25; doc 4-3, 4-5 and 4-6.

**Doc 4-3, §4.2's tail and §4.3.** The `CONFIGURATION` register's bits are
`W4`/`W3`/`W2`/`W1` here, where `[OMTI]` gives `W20`-`W23` for the 862X and
`002398-04` calls them `j1`-`j4` — the same four bits under a third naming, and
consistent with §2.3's family split. `MASK` is bits 7-2 unused, bit 1 INTERRUPT
ENABLE, bit 0 DMA ENABLE, matching `ap_omti.h`. §4.3 lists the **six** logical
states — RESET, IDLE, SELECTION, COMMAND, DATA, STATUS — which is the set this
core models, and prints the 100 µs warning **twice on the page**, once under the
RESET register and once under the protocol, exactly as `[OMTI]` p. 4-3 does.

*One sentence not modelled and not applicable*: on reset the controller "will
set **default parameters (ST412)** to the LUNs". This is an ESDI board whose
`INITIALIZE DRIVE CHARACTERISTICS` is rejected and whose geometry comes from the
image, so there is no parameter set for a default to occupy. Recorded as
inapplicable rather than as a gap.

**Doc 4-5, Table 4-3, the floppy registers.** Five registers at primary `3F2`,
`3F4`, `3F5`, `3F6`, `3F7` and secondary `372`, `374`, `375`, `376`, `377` —
offsets 2 and 4 through 7 from base `3F0` or `370`, which is exactly
`ap_omti_fdc_reg_t`. **This settles the secondary-base question completely**:
the secondary set is base `0370h` plus the same offsets, so `W14`'s two values
and Table 4-3's two columns are one fact stated twice. Digital Output's eight
bits and Digital Input's bit 7 "from pin 34 of the floppy disk control cable"
match field for field, including "All bits are cleared when a channel reset
occurs".

**Doc 4-6 has the one thing this core did not hold: the FLOPPY WRITE
PRECOMPENSATION TABLE.** Eight codes against three data rates, in nanoseconds.
`ap_omti.h` named the three `WP` bits and stated why their effect is not
modelled — an `.afd` image has no bit cells whose timing could shift — but did
not carry the values. It does now, with two features that would otherwise look
like scan damage: the encoding is **not injective** (`100`/`101` and `110`/`111`
are identical, so six values occupy eight codes), and each rate column is the
500 KBPI column scaled by the bit period.

*The Diskette Control data rates confirm* — `00` 500 Kbit/s, `01` 300, `10` 250,
`11` reserved — but that is one witness twice: `ap_omti.h` took them from
`[8640]` §5.1 and this is the same text. `002398-04` p. 12-14 is the independent
check.


## `[8000]` §2 — the same eight addresses under different jumper numbers

PDF 11, doc 2-3, Table 2-1 **Jumper Assignments**. `*` marks as shipped.

| function | jumpers | values |
| --- | --- | --- |
| Drive configuration | `W4 W3 W2 W1` | `0 0 0 0` as shipped, meaning "**(Host assigns jumper value)**" |
| Winchester I/O base | `W6 W5 **W22**` | `0*0 1` `0320H`, then `0324H`, `0328H`, `032CH`; with `W22 = 0`, `01A0H`, `01A4H`, `01A8H`, `01ACH` |
| Floppy I/O base | `W7` | `0` = `0370H`, `1*` = `03F0H` |

**Three notes, and the first is the one that matters.**

*The drive-configuration bits are host-assigned here*, where `[OMTI]` §2.3 prints
a fixed drive table — under a specific BIOS's part number. That bounds this
record's own explanation of `FC`; see the correction inserted above.

*The Winchester selection is three jumpers, and the third picks the bank.*
`W22 = 1` gives the `032x` group and `W22 = 0` the `01Ax` group, with `W6 W5`
choosing within it. This board is `01A0H`, so `W6 W5 W22 = 0 0 0`. `[OMTI]`'s
862X table spells the same eight values with `W19 W18 W17`.

*The floppy jumper's polarity is inverted between the families.* `W7 = 1` is
`03F0H` here; `[OMTI]`'s `W14 = 0` is `03F0H`. Same two addresses, same
as-shipped choice, opposite sense — so a jumper number and a polarity read off
one manual must not be carried to the other. That is the family split showing up
in a place where the *values* agree and only the encoding does not, which is the
easiest kind to get wrong.


## `[8000]` doc 2-4 — the page that explains a word taken from the oracle

PDF 12. Four things, and the first is the best result this manual has given.

**The drive configuration is three orthogonal straps per LUN**, not `[OMTI]`
§2.3's four-row drive table:

|  | LUN 0 | LUN 1 | `0*` | `1` |
| --- | --- | --- | --- | --- |
| Sector type | `W10` | `W13` | Soft sectored | Hard sectored |
| Drive class | `W11` | `W14` | ST412 compatible | **ESDI compatible** |
| Drive type | `W12` | `W15` | Fixed | Removeable |

*This explains `READ CAPACITY`'s drive configuration word.* `ap_omti_cdb.h`
returns `02 44` for it, and `0x44` is **ESDI FIXED MEDIA** and **ESDI SOFT
SECTORED** — which is exactly *drive class ESDI, drive type fixed, sector type
soft*, the three straps read back. **That word was taken from `omti8621.cpp`
because no document this project held defined it for this drive.** It now has a
documentary account of every set bit, and the oracle and the manual agree
without either having been fitted to the other.

**Table 2-2 settles the connector-to-LUN mapping** this record left open this
morning: `J2` FIXED DISK (34 pin), `J3` FIXED DISK - **LUN 0** (20 pin), `J4`
FIXED DISK - **LUN 1** (20 pin), `J7` FLOPPY DISK (34 pin). One shared control
cable, one radial data cable per LUN, named by LUN. Correction inserted above at
the §2.4.1 entry.

**`W9 W8` gives bytes per sector** — 512 (17 sectors/track), 512 (18), 1024 (9),
1056 (9) — the same four rows as `[OMTI]`'s `W10 W11`, under different jumper
numbers and **without** the "ST506/412 MFM drives only" qualifier that manual
puts on the sector count. `W21`, `W23`, `W24`, `W25` are reserved here, where
the 862X uses `W20`-`W23` for drive configuration: the jumper numbering is
per-family throughout and nothing may be carried across.

**LED Meaning**: "Self test diagnostics are run when the unit is powered on. If
the LED goes out the diagnostics have passed. If the self test detects a failure
the LED will remain on." So the controller runs a power-on self-test. **No
duration is given anywhere in this manual**, and this core's only power-on
timing is §4.3's 100 µs reset window. Recorded as a named behaviour without a
number rather than modelled — inventing a self-test duration would put a figure
in the emulator that nothing supports, and there is no LED for a driver to read
in any case.


## `[8000]` §1.3 — a confirmation of the address fields, and two tensions

PDF 7, doc 1-2. §1.3.1's feature lists.

**Winchester, confirming three things this core derived elsewhere.**
"**Addresses up to 2048 tracks and 16 heads**" — 2048 is exactly the eleven-bit
cylinder `ap_omti_cdb.c` reassembles from bytes 1, 2 and 3, and 16 heads is
`AP_OMTI_CONVERSION_HEADS`. "ESDI drives may transfer data at a rate of up to
**10 Mbit/sec**", which is `AP_OMTI_TRANSFER_BYTES_PER_SEC`'s 1.25 MB/s. "Word
(16 bits) width data transfer on AT bus" and "Supports both programmed I/O and
DMA data transfers". And "**programmable sector interleave including a 1:1
interleave**", which agrees with §5.4.4's "an interleave factor of zero is set
equal to one".

**Tension one, and it is small.** "Supports **256**, 512, 1024 or 1056
bytes/sector" — but doc 2-4's `W9 W8` jumper offers only 512 (17 sectors), 512
(18), 1024 and 1056. There is no strap for 256. Either the feature list is
aspirational or 256 is reachable another way; nothing here needs it, since this
board's images are 512, and it is recorded rather than resolved.

**Tension two, and it is a named plan item.** "**Host has direct access to
floppy disk controller chip (NEC765 or equivalent)**." `ap_omti.h` argues from
three command summaries — `[OMTI]` §6.3, `[8640]` §5.3, `[8000]` §6.1, all ten
commands plus INVALID — that **there is no `WRITE DATA` command**, and
deliberately declines to invent one from general 765 knowledge. That reasoning
is about the *documents*; this sentence is about the *silicon*, and a host with
direct access to a real 765 can issue `05` and have it work. A floppy that
cannot be written is not a floppy, so this is the unlisted command that matters.
Not changed on this evidence — "or equivalent" is the phrase that stops a
datasheet's command set being transferable — and opened as an item with a
discriminator this core can run.

**Floppy, otherwise confirming**: one or two drives, 48 and 96 TPI plus high
density, 250/300/500 Kbit/s "including **dual rotational speed floppies**",
which is what the Additional Control Register's `PN2` is for.

*§1.3.2 is physical*: 3.9 × 13.25 × 0.75 inches.


## `[8000]` Appendix A-2 — the sense codes grouped by type, and every one of ours confirmed

PDF 61. Where `[OMTI]`'s Appendix A prints a flat code list, this manual groups
the same codes under the four **types** A-1 defines, with the six-bit value
spelled out as `5 4 | 3 2 1 0`. That makes the type field structural rather than
something inferred from the numbers.

**TYPE 0 — drive errors** (`00`): `00` No error, `01` No Index, `02` No
seek/command complete, `03` Write/Drive Fault, **`04` Drive not selected/not
ready**, `06` No track or cylinder zero found, `08` Seek/command in progress,
`09` Cartridge changed. *`05` and `07` are absent here too* — the same gap
`[OMTI]` A-2 has and A-3 fills with `07 Multiple Drives Selected`, so that
observation survives into a second manual and is one more shared-text alignment.

**TYPE 1 — data errors** (`01`): `10` ID CRC error (ESDI)/ID ECC error (ST412),
`11` Uncorrectable Data Error, `12` ID address Mark not found, `13` Data address
mark found, **`14` Sector not found**, `15` Seek error, `16` Sequencer/DMA
failure, **`17` Write protected**, `18` Correctable Data Error, **`19` Bad track
encountered**, **`1A` Illegal Interleave Factor**, **`1C`** Unable to read
Alternate Track data / Illegal access to an alternate track, `1D` Alternate or
Bad Track Already Assigned, `1E` No Alternate Track Found, `1F` Alternate
Assigned To Itself. `1B` is unassigned.

**TYPE 2 — command errors** (`10`): **`20` Invalid Command, `21` Illegal Disk
Address, `22` Illegal Function for Drive Type, `23` Volume Overflow** — all four,
and only those four.

**TYPE 3 — diagnostic errors** (`11`): `30` RAM error, **`31` Z8 firmware
checksum/internal diagnostic error**.

**Every code this core emits appears, under the type the new `omti_cdb_suite`
test asserts.** That test was written from A-1's type *definitions* and the
values this project had chosen from flat code lists; this table confirms it row
by row, which is a stronger result than the test could give on its own.

*And `31`'s name here is the `Z8` form*, which independently supports the
reading taken from `[OMTI]` Figure 2.2's board diagram — that A-2's "Z8 firmware
checksum" and A-5's "EPROM Checksum" name one checksum over the Z8's firmware
EPROM.

**Two wordings to flag.** `13` reads "Data address mark **found**", which is
almost certainly "not found" — it is the `MD` condition of `ST2`, and a sense
code for successfully finding an address mark is not an error. Recorded as a
probable defect in this printing rather than corrected silently. And `14` is
"**Sector** not found" here, which is one side of the sector-versus-record
wording this record logged as an inconsistency in `[OMTI]`.


## `[8000]` Appendices B and C — and the contents page is wrong about B

PDF 65-71; doc `B-N` is PDF `N+64`, `C-N` is PDF `N+68`.

**Appendix B is not what the contents page says it is.** That page lists
"APPENDIX B  Interleave Scheme"; **doc B-2 is an ABBREVIATIONS/MNEMONICS
glossary**, and **doc B-4 is blank apart from its footer**. This is the same
class of defect as `[OMTI]`'s §2.4.6 cross-referencing "section 2.9" for what
its contents page calls §2.6 — the body and the index of these manuals disagree
about their own structure, which is a second reason page numbers must come from
reading rather than from the index. *B-1 and B-3 are not yet read, so whether an
interleave table survives somewhere in B is open.*

**Two glossary entries worth having, and one is a self-inconsistency.**

`C/D` is expanded "**Control** Data" — where Table 4-2 of *this same manual*
writes "Bit 2 C/D (**Command/Data**)" and `[OMTI]` Table 4-2 gives "1 = byte is
a command or status byte", which is `ap_omti.h`'s `AP_OMTI_ST_CD`. The register
description is the one that says what the bit does and is taken; the glossary is
recorded as the outlier. That makes **six** places `[OMTI]`-family text
disagrees with itself.

`FDC` is "**Flexible** Disk Controller", not Floppy — a period spelling worth
knowing when searching these scans. `D LUN` is "Destination Logical Unit
Number", which is `COPY`'s second LUN and something `ap_omti.c` already decodes
from bytes 5-7 of the ten-byte descriptor.

**Appendix C, BIOS INSTALLATION PROCEDURE — host-side, and inapplicable here.**
The 8000 series ships an optional pair of EPROMs, labelled "EVEN ADDRESSES" and
"ODD ADDRESSES", that go into empty sockets **on the PC motherboard** — `U17`
and `U37` on an IBM AT, `U60` and `U58` on a Tandy 3000. A DN3500 has no such
sockets and no x86 BIOS.

*It does corroborate the floppy strap from a third angle.* "The 8200 and 8600
controllers provide floppy disk support ... connect the floppy cable to
connector **J7**", and for the two-controller case "one of the two controllers
must be strapped for the secondary floppy base I/O address and the other for the
primary" — `W7` **installed** is the primary base, `W7` **removed** is the
secondary. That matches Table 2-1's `W7 = 1*` → `03F0H`. The page also says "to
enable floppy support, jumper `W7` must be installed", which conflates enabling
with selecting; Table 2-1 is the precise statement and is the one taken.


## `[8000]` §5.2 — the shared-source finding turns a judgement call into a proof

PDF 29, doc 5-3. The CONTROL BYTE, and it confirms `ap_omti_cdb.h` field for
field: `R | E/B | C | x | x | S | S | S`, with bits 4 and 3 don't-care and bits
2-0 the STEP option. That is the layout `[OMTI]` §5.2 gives, and it is a second
statement that §5.1.1's "bits 4,3,2,1,0 are not used" is the loose summary.

`DISABLE RETRY`: "the controller will perform **up to 8 retries** ... The retry
algorithm is **4 retries, 1 recalibration, 4 retries**" — the phrase this core
already quotes, now with the total. `DISABLE ECC` and `ENABLE FORMAT BUFFER`
share bit 6 by command, as modelled.

**And `ENABLE SECTOR ADDRESS CONVERSION` settles a question this record has
carried as a judgement call.** `ap_omti_cdb.h` records that `[OMTI]` disagrees
with itself about which jumpers set sectors per track — §5.2's prose says "`W10`
and `W9`", its page 2-8 table says "`W10` and `W11`" — and takes the table
because the table carries values. Reasonable, and not proven.

`[8000]` §5.2 says "**`W10` and `W9`**" as well. *The same two names in the same
sentence, a year earlier* — while **its own** table, doc 2-4, calls them **`W9`
and `W8`**. So:

| | prose | that manual's own table |
| --- | --- | --- |
| `[8000]`, 1986 | `W10` and `W9` | `W9` and `W8` |
| `[OMTI]`, 1987 | `W10` and `W9` | `W10` and `W11` |

**The prose is identical across products and disagrees with each product's own
table.** That is exactly what copied body text plus re-typeset tables looks like,
and it makes the table authoritative rather than merely better-evidenced. The
choice `ap_omti_cdb.h` made now has a reason.

*It also gives the conversion's purpose*, which `[OMTI]` §5.2 does not: the
conversion "is useful when there is a different number of sectors per track
(ESDI) than **the DOS is using (17)**". Seventeen is the as-shipped `W10 W11`
row; this board is jumpered to 18, and `AP_OMTI_CONVERSION_SECTORS` is the
jumper's value rather than DOS's default. The two agree on 16 heads per cylinder.


## `[8000]` §5.3 — the completion byte, confirmed field for field

PDF 30, doc 5-4. The register `ap_omti.c` calls the *completion byte*:

    Bit: | 7 | 6 |  5  | 4 | 3 | 2 | 1 | 0
         | 0 | 0 | LUN | 0 | S | S | e | 0

Bit 1 `e` is command status, "a value of zero indicates a successfully completed
command"; bit 5 "indicates the LUN address of the device associated with this
command"; bits 7, 6, 4 and 0 are set to zero. All three are what
`COMPLETION_ERROR`, `COMPLETION_LUN` and the zero bits already are.

Bits 3-2 are **Error Recovery Status**, "valid only for commands which read data
from the disk" — `00` no error recovery, `01` one retry accomplished
successfully, `10` more than one retry, `11` error correction done successfully.
`COMPLETION_ERROR_RECOVERY_MASK` and `_NONE`/`_CORRECTED` match.

*A rule this core cannot exercise and should still record*: "On multiple sector
transfers, these bits will reflect the **worst** ECC Retry Count/Correction ...
If ECC was applied on any sector regardless of the Retry count, bits 3, 2 = 1 1."
This model has no medium that fails, so the field is always `00` — which is the
same reason `DISABLE RETRY` and `DISABLE ECC` are decoded and inert. It becomes
reachable together with them, if media errors are ever injectable.

**The STEP table is printed here for the fourth time**, all eight rows, agreeing
with `[OMTI]` §5.2 p. 5-4, `002398-04` p. 12-10 and doc 2-14's format flowchart.
Four printings, one of them from Apollo rather than OMTI, so this is genuinely
two independent witnesses rather than four.

*One warning, inapplicable here*: "The INITIALIZE DRIVE CHARACTERISTICS command
(`0CH`) must include the **true** number of heads value of the drive (not 16 as
used for the conversion)." `0C` is the ST506-only command this core rejects, so
there is no path on which the distinction arises — but it is the sharpest
statement either manual makes that the conversion's sixteen heads are a *host*
geometry and not the drive's.


## `[8000]` §6.1 and §6.2 — verified, after being cited unread

PDF 53, doc 6-1. **Read because a claim had been made about it without reading
it.** The `WRITE DATA` plan item, opened earlier the same day, says "`[OMTI]`
§6.3, `[8640]` §5.3 and `[8000]` §6.1 all list the same ten plus INVALID" — and
at that moment this record had `[8000]` §6.1 marked *owed*. The claim was made
from the pattern of the other two.

**It holds.** §6.1 lists exactly eleven entries: READ DATA, FORMAT A TRACK, SCAN
EQUAL, SCAN LOW OR EQUAL, SCAN HIGH OR EQUAL, RECALIBRATE, SENSE INTERRUPT
STATUS, SPECIFY, SENSE DRIVE STATUS, SEEK, INVALID. **No `WRITE DATA`**, in a
third manual, which is what the item asserted.

*Recorded anyway, because being right is not the same as having checked.* This
is the same failure as citing a page for one row and calling it read — and it
landed in a plan item, where a later reader would take it for a verified
citation. The item's reasoning is unchanged and its evidence is now real.

§6.2's symbol list is `[OMTI]` §6.2's word for word, down to `GPL`'s "spacing
between sectors excluding the VCO synchronous field" — one more shared-text
alignment, and the reason the `SRT` table's three-of-sixteen rows are three in
every manual.


## `[8000]` §3 — where the two manuals genuinely differ, and it matters twice

PDF 18-19, doc 3-6 and 3-7. §3.3's signal descriptions are `[OMTI]` §3.3's word
for word — `CS16`, `OSC` at 14.31818 MHz and a 70 ns period, `-OWS` — and then
the chapter **ends at §3.4**. Doc 3-7 has blank space below it, and the contents
page lists §3.1 to §3.4 and no more.

**So `[OMTI]` §3.5 does not exist here, and that is good news for DRQ7.**
`ap_omti.h` resolves `[OMTI]`'s internal `DRQ3`-versus-`DRQ7` contradiction on
§3.5's width rule — "`DRQ0` through `DRQ3` will perform **8-bit** DMA transfers;
`DRQ5` through `DRQ7` will perform **16-bit**". This record established earlier
today that these manuals are largely one source text, which would have made a
§3.5 in both worth little. It is in **one**: added between June 1986 and January
1987, and therefore an *independent* statement rather than a copied one. The
conclusion that rested on it rests on firmer ground than the shared-text finding
first suggested.

*(The `DRQ3` error, by contrast, **is** shared — `[8000]` Table 4-2 carries it a
year early. So the contradiction is inherited and its resolution is not, which
is the best arrangement available.)*

**And §3.4 says two sections here, where `[OMTI]` says three.** In full: "The
OMTI 8000 Series are partitioned into **two** distinct sections - the floppy
disk logic and the Winchester disk logic. **These two sections share the same
physical PCB board** but are otherwise independent." `[OMTI]` §3.4 has the same
sentence with "**three**", "and the QIC 36 section", and "**The first two**
sections share the same physical PCB board".

*So the QIC-36 section was added to the series between the two manuals.*
`ap_omti.h` records the third section as absent from this board and cites
`[OMTI]`'s "first two" qualifier for it; that stands, and this page makes the
stronger statement — the architecture this core models **is** the 1986 8000
series as originally described, two sections on one board, with the third a
later member of the family. Two manuals differing is rare enough here to be
worth the entry on its own.
