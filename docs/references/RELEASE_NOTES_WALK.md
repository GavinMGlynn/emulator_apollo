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
| `019534-A00` SR10.4 addendum | 5 | **none** | **read whole, 5/5 as 400 dpi images (2026-09-15)** -- below; *was "owed"* |

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

## The SR10.4 addendum, `019534-A00`, read whole

Five pages, March 1992, "information that was unavailable when ...
`018901-A00` was printed". Page 1 is a *read me first* sheet and page 5 the
back cover. Pages 2-4, item by item:

| Item | Against this project |
| --- | --- |
| SysV validated to OSF AES/OS Revision A | none |
| the Software Release Bulletin is shipped `compress(1)`ed in `/install/doc/apollo/os.v.10.4__software_release_bulletin.Z`, read with a `zcat` link to `/usr/apollo/bin/compress` | a file on the installed volumes, not needed |
| "Warnings to ignore": installing onto a disk that holds the authorized area prints `can't copy a file or tree to itself (US/file utility)` for two documents, and they "are indeed installed" | **worth recognising on an install console here** -- a warning this project's install scripts could otherwise stop on |
| `/usr/X11` link-to-copy conversion fails; delete the links first or install twice | none |
| the `/lib/x*.r4` X libraries are stamped `RUNTYPE=bsd4.3` and run under SysV too | none |
| **Series 425e with 425 MB disks intermittently fails to boot with crash status `8002B`** | a disk-manager status (`0008002B` by `002398-01` ch. 4's layout, past that 1983 list's `00080024`); a 68040 HP machine, not a model here. Recognisable if a DS5500 boot ever prints it |
| `/usr/include` is not installed by the `sys5.3_medium` templates | none |
| **`018901-A00` §2.8 is corrected: an a88k magtape kit is six tapes, `MT_STD_SFW_1`-`6`, plus `CRTG_STD_SFW_BOOT_1`** | the cartridge boot volume beside magtape media that `008860-A03_WALK.md` records from chapter 1 |
| installing from an `mrgri`-merged authorized area onto a Series 10000 needs a `xar`-combined `x11lib` before reboot | none -- a88k |

**Nothing for the core**, and one installer message and one crash status are
now recognisable.

## SR10.0 Beta 2, SR10.1 and SR10.2: the hardware-bearing sections, read as images (2026-09-15)

Each document's contents pages were read whole and every section whose title
could carry a hardware, boot, device, tape or floating-point fact was rendered
at 250 dpi and read. The rest are named by their titles: shells, DM, fonts,
registry, printing, mail, TCP/IP, NCS, X11, compilers, installation selection
files and documentation changes.

| Document, section | What it says | Against this core |
| --- | --- | --- |
| SR10.0β2 §1.3.11, SR10.1 §1.4.6 | full UNIX tty support "for remote login over SIO lines, as well as **siomonit and siologin**"; `/dev/sioX` and `/dev/ttyXX` are one device, except DCD is ignored on open through `/dev/sioX` | the DCD rule is a software fact the siologin thread (`FINDINGS.md` C222) can use; nothing in the core |
| SR10.0β2 §1.3.25, SR10.1 §1.4.1 | the OS file is now `domain_os`, started with **`EX DOMAIN_OS`**; "DN5xx-T systems with FPX boards and with MD revisions earlier than 5.7 must still use `EX AEGIS`" | the boot command this project types; `MD.md` has it |
| SR10.0β2 §1.3.26, SR10.1 §1.4.2 | `/etc/sys.conf` decides whether a library loads into global or user space, at initialisation or on first use, and whether it is optional | the file `014962-A00_WALK.md` reads the `not_64mb_va` flag from |
| SR10.0β2 §1.3.30, SR10.1 §1.4.16 | `` `node_data `` gains `system_logs`, `systmp` and `etc` | where siomonit's log would sit |
| SR10.0β2 §2.1 | SR10 drops DN100/400/420/600, needs **2 MB** minimum, 500 KB free disk per user process | every modelled machine exceeds it |
| SR10.0β2 §2.4 | **SR10 changes the on-disk structures**: a disk must be re-`invol`ed before SR10 is installed, and no SR10 volume mounts on a pre-SR10 system | **the reason `002398-01` and `002398-03`'s VTOC entry does not describe an SR10.4 volume** -- the change `002398-01_WALK.md` dated to "some point after Feb 1985", now dated to SR10 |
| SR10.1 §1.4.3 | paging statistics redefined: data-file and executable faults count pages read from disk or network, touch-ahead included, zero-filled pages and sharing faults excluded | the `netstat -l`/`pst` counters the two-node runs read (C294) |
| SR10.1 §1.4.14 | `netman` runs `/sys/net/netman.rc` when a remote node boots diskless | the diskless route `008860-A03_WALK.md` records |
| SR10.1 §2.2 | cartridge tape: SR9.7 writes an explicit end-of-tape marker, SR9.7.1 and later do not; a "hybrid" tape added to under SR10.1 cannot be read past that marker by SR9.7; `wbak` may write only at file 1 or after the last file, else `cannot write at this tape position (library/tfp)`; "read no data (OS/cartridge tape manager)" ends an index scan | **software rules above the `ap_sc499` drive**, consistent with `[SC499]`'s write-only-at-end behaviour; recognisable messages |
| SR10.1 §2.7, SR10.2 §1.8.1 | the `fpp_$` service routines: rounding, trap enables, IEEE underflow, accrued exceptions "in 881/fpa/fpx/dn10000-based machines", and `fpp_$set_mc68881_precision_mode` -- extended, single, double, reserved -- "no effect on any other Apollo machine" | the four precision-mode values are the 68881/68882 FPCR's rounding-precision field in its own order; the FPU model is walked whole (`M68881_WALK.md`) |
| SR10.1 §2.8.1 | SLIP "must use SIO 1 on nodes which have more than one serial line" | none |
| SR10.2 §1.4.1 | the Series 2500: 20 MHz 68030, 4-16 MB, 100/200 MB internal disks, external CTAPE, 15" 1024x800 or 19" 1280x1024 | the `DN2500` row |
| SR10.2 §1.4.2 | **the DVS is a 1280x1024 controller, 8- or 40-plane, "a 2-board set that consists of a Transform Processor board connected to an ... Array board", using two AT slots**; 8-plane on DN3500/3550/4500, 40-plane on DN3550/4500 | agrees with `[CFG]` p. H-15 and `5952-2149` p. 10's HSI ribbon; no DVS is modelled |
| SR10.2 §1.4.3 | first major release to include the WD Multifunction Peripheral Controller, the SCSI 8 mm, ½" reel and ¼" cartridge drives and the 70 Hz monitor, previously in PSKs | the SCSI stack `ap_wd7000`/`ap_exb8200` runs under SR10.4 |
| SR10.2 §1.4.4 | `scsi_$` for user SCSI drivers, ANSI X3.131-1986, through GPIO | none |
| SR10.2 §4.1.5 | **"any of the 3 SIO lines on a Series 2500"**; its UARTs tolerate less baud-rate error, 1220 against 1200 fails, two stop bits tolerate over 5% | a DN2500 has three SIO lines; no DN2500 serial timing is modelled |

**Nothing for the core**, one dating (the SR10 on-disk format change) and one
confirmation of the DVS as an HSI-attached two-board set.

## What is owed

**Nothing hardware-bearing.** *The three documents' hardware sections were read
next (above); this said "SR10.0 Beta 2 and SR10.2 beyond their indexes, and
everything in SR10.1 outside §2.1."* *The addendum was read next (above); this also listed "the five-page SR10.4
addendum — which has **no text layer** and is therefore five renders, the
cheapest scanned document on the shelf and the one closest to the release this
project runs."*
