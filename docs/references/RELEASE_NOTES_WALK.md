# The release-notes shelf — walk coverage record

`docs/references/bitsavers/release_notes/`, six documents, 805 pages. **This
project boots SR10.4**, and these are the only documents on the shelf that
describe the releases by name.

| Document | Pages | Layer | State |
| --- | --- | --- | --- |
| `005809-A00` SR9.7, Nov 87 | 62 | text | **§1.3, §1.4 read** |
| `005809-A01` SR10.0 Beta 2, Apr 88 | 114 | text | index only |
| `005809-A03` SR10.1, Dec 88 | 256 | text | **§2.1 read** |
| `005809-A05` SR10.2, Nov 89 | 166 | text | index only |
| `018901-A00` SR10.4, Mar 92 | 202 | **none** | `018901-A00_WALK.md`, §1.4.1 read |
| `019534-A00` SR10.4 addendum | 5 | **none** | owed |

## SR9.7 §1.4.2 closes a question the Design Principles walk left open

`014962-A00_WALK.md` recorded a question: `/etc/sys.conf`'s `not_64mb_va` flag
names "DN330, DSP90, DN5x0, **some DN3000**" as 64-Mbyte virtual address space
machines, and nothing said which DN3000s or why. SR9.7 says:

> "The **DN3000** product line will now support a **256-MB virtual address
> space**, when used in combination with **new PMMU hardware**. Note, however,
> that a DN3000 with **DMMU hardware** will continue to support **64-MB**
> virtual address space."

**So a DN3000's virtual address space depends on which MMU board it carries**,
and "some DN3000" is the DMMU-equipped ones. Two documents, three years apart,
and neither is intelligible about this without the other.

**A question this raises and does not answer**: `ap_model.c` has **one** DN3000
row, `AP_MMU_M68851`, "68020 with external PMMU". Whether the modelled DN3000 is
a PMMU or a DMMU machine is not stated anywhere here, and no walked hardware
document — `008778-03`, `002398-04`, `019411-A00` — uses the term DMMU at all.
**Not acted on**: the address-space size is an operating-system property and
nothing in this core selects on it, so this is a gap in the record rather than
in the model. It becomes real if a DN3000 image ever behaves as though it has 64
Mbyte of virtual space.

## SR10.1 §2.1 is the DN3500 and DN4500's own introduction

> "§2.1.2 ... The Series **DN3500** is a **SAU7-compatible** Personal
> Workstation featuring a **25-MHz MC68030** microprocessor equipped with
> **separate instruction and data caches**, up to **32 MB** of [memory]"

> "§2.1.1 ... [DN4500] memory, expandable to **32 MB**. The DN4500 is available
> in several graphics configurations, including **1280 x 1024 8-plane color**.
> Mass storage options include full-height **170, 380, or 760 MB ESDI
> winchesters**, half-height ¼ in. **60 MB SCSI cartridge tape**, half-height
> floppy, and external ESDI disk subsystems containing either 1 or 4 drives."

> "§2.1.4 ... The **FPA** is based on the **Weitek 3164** floating point chip.
> It accelerates both scalar and vector processing and is available as an
> **option** to the DN4000, DN3500, and DN4500 Personal Workstations."

**Everything here agrees with `ap_model.c` and one thing was new**: the FPA's
part. This project had the address range `F8000000`-`FFFFFFFF` and the firmware's
probe-and-bus-error behaviour, and no name. `PROJECT_STATUS.md` now carries it,
with `018901-A00` §1.4.1's later removal of the board on the DS5500.

*The disk capacities are marketing figures*: 170/380/760 MB against the 155 and
348 Mbyte formatted capacities `ap_omti.h` derives from `008778-03` Tables 6-4
and 6-5. Consistent, and not a second source for the same numbers.

**And `SAU7-compatible` is stated outright** for the DN3500, which this project
had from the `MD7C` banner and `/sau7`.

## SR9.7's other content

§1.4's new-hardware list for that release: the DN4000, the DN590-T, the 256-MB
virtual address space above, an IEEE/802.3 Network Controller-VME, VMEbus
devices in DN5xx-T systems, a DN3000 15-inch monochrome monitor, and the Domain
Dial Box. §1.4.1 notes that DN4000 support came in SR9.6.1 and DN590-T in
SFW-DN590 before being merged. §1.4.3 carries an ECO warning that prints
`***Please request ECO #14202 for your CPU PCB***` and halts — a firmware
message worth recognising if it ever appears on a console here.

**And MD's boot sequences**, which gave `docs/references/MD.md` a command it did
not have: `UA`, a microcode load, between two resets. Recorded there as a
command that exists and is not needed on the machines this core models.

## The missing handbook, cited a second time

§1.4.2 refers the reader to "the *Domain Series 3000/Series 4000 Hardware
Architecture Handbook* (**Order No. 007861, Rev 02**)". `000959-A00`'s Related
Manuals cites the same document. **This shelf holds only `019411-A00`, its
addendum.** Two independent citations, a revision number, and no copy — which
makes the absence a documented fact rather than an inference, and sharpens
`apollo-documentary-universe-is-exhausted` from "no public DN3500 hardware
document" to "the handbook exists, is cited twice by other Apollo manuals, and
is not public".

## What is owed

SR10.0 Beta 2 and SR10.2 beyond their indexes, everything in SR10.1 outside
§2.1, and the five-page SR10.4 addendum — which has **no text layer** and is
therefore five renders, the cheapest scanned document on the shelf and the one
closest to the release this project runs.
