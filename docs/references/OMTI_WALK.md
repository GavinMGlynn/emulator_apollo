# OMTI controller manuals — walk coverage record

Three manuals, and the DN3500's controller is an **8621**.

| Tag | File | Pages | Native | Cited |
| --- | --- | --- | --- | --- |
| `[OMTI]` | `omti/OMTI_AT_Controller_Series_Jan87.pdf` | 88 | 800 ppi | throughout `ap_omti.h` |
| `[8640]` | `omti/OMTI_8640_Technical_Reference_Manual_Jun89.pdf` | 61 | 600 ppi | as the sibling, several places |
| `[8000]` | `omti/OMTI_8000_Series_AT_Reference_Jun86.pdf` | 71 | 400 ppi | **once** — effectively unconsulted |

**220 pages total.** `[OMTI]`'s §3.4, §4.1–§4.5, §5.1–§5.4 and §6.3 are
**derived into the model** and §6.4 is walked; §1, §2, most of §3, §6.1–§6.2 and
§7 onward are unread, as are `[8640]` and `[8000]` entirely. *This line said
"None is walked" until 2026-08-22, when a citation audit showed otherwise — and
it survived two corrections to the status section below before
`check_docs.py`'s new coverage check found it.*

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
| §5.1–§5.4 | 37 distinct subsections, §5.4.3 through §5.4.29 | **derived** (record said "owed") |
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

Two gaps in the table worth noting so a later reader does not think they were
missed: type 0 defines no `05` or `07`, and type 1 no `1B`.

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
