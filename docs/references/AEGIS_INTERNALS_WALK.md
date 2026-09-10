# `AEGIS Internals and Data Structures` — walk coverage record

| Tag | File | Pages | Text layer | State |
| --- | --- | --- | --- | --- |
| `[AEGIS]` | `bitsavers/AEGIS_Internals_and_Data_Structures_Jan86.pdf` | 426 | born-digital, heavy OCR damage | **IN PROGRESS — 5 chapters and 1 appendix read, 24 chapters and 2 appendices owed** |

Revision 00, Software Release **9.0**, January 1986. **Cited by title in
`RING.md` and twice in `PROJECT_STATUS.md` and never walked** — which is how it
came to be the second document read off the shelf `SOFTWARE_SHELF_WALK.md`
audits.

**Read `[MAC]`, `002398-04` and `019411-A00` alongside it.** This is an SR9.0
operating-system document describing hardware this project models at SR10.4, and
it disagrees with the hardware manuals in two places recorded below. Where they
conflict, the part's own manual wins; this document's value is that it explains
*why* the machine does what it does, which no datasheet does.

## What it has already settled

**Four of this project's hard-won empirical findings are confirmed here, and two
of them are explained.** Everything below was measured off a running DS5500
first and found in this document afterwards — which is the wrong order, and the
reason `SOFTWARE_SHELF_WALK.md` exists.

| Measured here | Said by `[AEGIS]` |
| --- | --- |
| The live vector table is the boot PROM's own 1 KB trap page copied to RAM and relocated | §26.1.1: "The last RAM page is reserved for the mapped-mode trap page, **which is initially a copy of the PROM trap page (0-3FF)**", and §26.1.2: "The mapped-mode trap page at 100400-1007FF is **mapped to address 0**" |
| The PROM service table sits at ROM offset `$100`: a machine-type word, then entry points | §26.2.2: "A **machine ID exists at address 100** to identify the node model"; §26.2.3: "PROM address **102** indicates auxiliary information"; §26.2.4: "Addresses **104 through 13F or 20F** contain the addresses of several subroutines that can be called from outside the PROM" |
| The DS5500's `$100` longword reads `000E000F` | Two words, not one: machine ID `000E` and auxiliary info `000F`. The word at `$100` is **data in the middle of the trap page**, which is why the relocating copy skips it |
| `MD14` and `/sau14`: the banner's digits are the SAU number | §27.1 step 5: "**n = machine id** if the machine id does not = 0", so the SAU directory *is* the PROM's machine ID. `000E` is 14 |
| The DS5500's boot records occupy blocks 2..11 | §27.1: "SYSBOOT is always located in **physical disk blocks 2-B** of a bootable disk. These are logical blocks 1 through A of the first logical volume. The initialize volume (INVOL) program reserves these blocks for SYSBOOT" |
| The firmware carries **two** stack bases, `01000180` and `7A400180`, and chooses | §26.1.1: the PROM's RAM is at `100000`-`1007FF` and its "static data and supervisor stack area occupy the first one or two pages"; §26.1.2: in mapped mode "**The PROM stack and variable data (100000 - 1003FF) is mapped to E00000**". Two bases because the PROM runs in two modes, and `E00000` is supervisor global on the 16 MB map |

## Chapter 9, Virtual Address Space Layout — read whole (pp. 111-118)

**The page is 1,024 bytes**, and this is the third independent document to say
so — `004977-02` §2.4 says "8,192-bit", its Appendix C says "a multiple of 1,024
bytes", and §9.1 here says "each segment consists of **32 pages**, while each
page consists of **1024 bytes**". A segment is therefore 32 Kbytes.

Two layouts, and the DS5500 is the second: "DNx60 nodes support a virtual
address space of **256 megabytes**, while the other nodes support a
**16-megabyte** address space."

| 16 MB (Figure 9-1) | | 256 MB (Figure 9-2) |
| --- | --- | --- |
| `000000` trap and PROM pages, 1 seg | | region 00 seg 0000, same |
| `008000` user global, 63 segs | | `008000` user global, 255 segs |
| `200000` user private, 320 segs | | `0800000` user private, 7680 segs |
| `BC0000` supervisor private, 8 segs | | supervisor private, 15 segs |
| `C00000` reserved to Apollo, 64 segs | | — |
| `E00000` supervisor global, 32 segs | | supervisor global, 240 segs |
| `F00000` I/O space, 32 segs | | `FF80000` I/O space, 16 segs |

256 MB space is "allocated by **region**. Each region consists of **256
segments**" — so a region is 8 Mbytes and there are 32 of them. "Region 31 is
allocated to user and supervisor global address space."

**User private begins with the stack object**, and its shape is the shape of
what a DS5500 process does at startup: process creation record (1 seg), private
read/write storage (5 segs), **guard segment**, procedure call stack (8 segs),
**guard segment**, private KGT (2 segs), then mapped objects. *Guard segments
either side of the call stack are by design*, which is worth having after a
session spent on whether a small stack with nothing below it was a fault.

**§9.3 names what the 68040's G bit is for on this machine.** There are 26
address space identifiers; ASID 1 is the first level 2 process and the display
manager, 2-25 are assigned sequentially, and "The system assigns **ASID 0** to
user and supervisor global address spaces. It identifies pages of global address
space by **setting a hardware global bit in the MMU hardware page tables**." So
`ap_m68040_descriptor`'s `G`, `ap_m68040_atc_flush_nonglobal` and `PFLUSH`'s
non-global variants exist because global space must survive an address-space
switch — and a DS5500 boot issuing 3,515 `PFLUSH`es is doing exactly that.

*Also captured*: **whole cloth pages** are "wired virtual address space *holes*"
with no object behind them, and "if a process generates a page fault to a whole
cloth virtual address, **the system will crash**"; the OS paging file "stores the
pageable sections of the operating system, and is where AEGIS resides when it is
running. At present, its size is **352 pages**" — against SR10.4's INVOL, which
offers a 1000 kB default.

## Chapter 21, Ring Hardware — read whole (pp. 243-246), and it settles nothing

**This chapter looked like it would answer `RING.md` question E and it does
not.** `ap_ring_station.h` carries a `PROVISIONAL` for the forced-transmit
timeout: "`[MAC]` §2.2.1.1 ... The manual specifies *that* there is a timeout and
never says what it is." §21.1.2 describes the same mechanism — "If a node wants
to transmit and the token does not pass through it **after a specified time**,
the node assumes that the token has been destroyed and carries out a **forced
transmit** ... and regenerates the token at the end of the message" — and gives
no number either. **Question E stays open and the `PROVISIONAL` stands.**

**Two conflicts with `[MAC]`, both recorded, neither acted on.**

- §21.1.3: "When a transmitting node is waiting for its packet to come full
  circle, it will only wait **four milliseconds**." That is the stripping
  timeout, which `[MAC]` §2.1 step 7 puts at **10.9 msec (2^14 byte)** — a
  figure that checks itself, since 2^14 bytes at 12 Mbit/s is 10.923 ms.
  `[MAC]` is the protocol specification and its arithmetic is self-consistent;
  `AP_RING_STRIP_TIMEOUT_BYTES` keeps 16384.
- §21.1.5: "each node has a **1- to 2-bit** elastic store buffer". `[MAC]`
  §3.3.2 gives nominally 1 bit with the range `0.5 <= ESB <= 1.5`, which is what
  `AP_RING_ESB_*_CENTIBITS` models and what carries the underflow/overflow
  consequence. `[MAC]` kept.

*Behavioural facts worth having, none of which contradict the model*: a node may
put **one** message on the ring per token circulation; the transmitter "does not
wait to read the ACK byte before it returns the token to the network"; multiple
tokens are handled by the transmitter, which "removes the extra token and
reports the error"; a biphase error is reported by the **next node downstream**,
in the ACK byte; an elastic-store overflow is detected by "the first downstream
node that has the most constricted elastic store buffer", so it cannot be
attributed to the node that caused it. Two figures are round rather than exact —
"about **70 microseconds** to circulate a 700-node network" and "**one
millisecond** for each 1024-byte message packet", where 1024 bytes at 12 Mbit/s
is 683 µs — so neither is a timing source.

## Chapter 26, The Bootstrap PROM — read whole (pp. 279-286)

Beyond the table confirmations above: the PROM "also called the **mnemonic
debugger (MD)**" runs in normal or service mode, chosen by the node service
switch; it "uses two to three pages of RAM memory", at `100000`-`1007FF` on
everything but a DNx60, where it is `200000`-`200BFF`.

**Physical against mapped mode**, §26.1.2, which is why the firmware carries two
of every address: in mapped mode the initial trap page at `0-3FF` "becomes
inaccessible", the static procedure and data section `400-3FFF` is mapped
one-to-one, I/O objects move "to their AEGIS locations (starting at `FA0000`)",
the stack moves to `E00000`, the mapped-mode trap page moves to `0`, and "the
rest of physical memory is mapped one-to-one". The `P` and `M` commands switch
modes. And the reason it must work in both: "If you enter the PROM from AEGIS,
for example through a crash, you should be able to use the PROM **without
disturbing the contents of the MMU**."

**Reverse-mapped nodes must enter mapped mode to touch the disk or the network**
"because all DMA goes through the I/O map (IOMAP), and, on older node models,
the I/O map is inaccessible unless the MMU is enabled. The PROMs on
**forward-mapped** MMUs perform disk and network I/O in **physical mode**" — the
DN3500 and DS5500 are forward-mapped, so this is the documented reason their
boot I/O runs untranslated.

**Machine IDs, §26.2.2**, as of SR9.0: 0 = DN400/old DN420, 1 = DN420/DN600,
2 = DN300/DN320/DN330, 3 = DSP80/DSP90, 4 = DN460/DN660/DSP160, 5 = DN550/DN560.
The DS5500's `000E` is far outside this list, which dates the list rather than
the machine. **Auxiliary information at `102`**: bit 1 set means log-error and
crash entry points exist, bit 2 means an M68020-based processor board. The
DS5500's `000F` has both and two more this edition does not define.

## Chapter 27, SYSBOOT/NETBOOT/CTBOOT — read whole (pp. 287-294)

**The stand-alone program header, and it differs from SR10.4's.** §27: every
stand-alone program "must reside in a SAUn directory ... and must **start with
three integer values**: a low address at which the program is loaded; the start
address ...; the machine type identifier of the target machine, or 0 if the
program can run on any machine type." That is the `low: … high: … start: …` line
a DS5500 prints. It is **three integers here and a processor tag string at
SR10.4** — where this project found `M68K_4K` against ` M68K    ` — so the
header gained a field between the releases. Recorded as a difference, not
reconciled.

**Why SALVOL runs**, §27.1 step 3: SYSBOOT "checks two places in the logical
volume label: 1) the **BAT header volume trouble bit**, and 2) the **shutdown
state word**. If the volume trouble bit is set or the shutdown state word
indicates that the disk is mounted (that is, the volume was never properly
dismounted), then salvaging is required" — and then only if it has not just
tried, the file begins with `AE`, and the node is in normal mode.

**SAU selection**, step 5: `n` = machine id, except `n = 1` when the machine id
is 0.

NETBOOT writes a period per page loaded and a byte total every eight pages, and
after loading asks its partner for the UIDs of the OS paging file, the network
root directory and the disk entry directory.

## Appendix A, Boot LED Codes — read whole (pp. 309-311)

The steady-state codes the PROM loads as it initialises, which is what a hung
node shows: `F` power-on (and reset held), `0` first instruction at `init`
("If this fails to happen, there is probably no clock signal to the CPU"),
`1` memory passed its existence test, `4` VME modifiers set/ring id
loaded/SIOs initialised, `5` IOMAP cleared and multibus initialised, `6`
display initialised, `7` display init took a bus error, `8` in service mode
waiting for keyboard input, `9`/`A`/`B` character received from
keyboard/line 1/line 2, `C` banner printed, `D` PTT enabled, `E` MMU loaded,
`F` MMU enabled. "**Note also that one value — F — has two meanings.**"

"After MD enters the command loop, starts running diagnostics, or initiates
program execution, it uses a **short/long two-digit sequence**" — which is the
paired form this project's boot report already decodes (`FB = 04 enable
instruction cache`, `FC = 03 bus error`). **And the appendix names where the
rest live**: "A complete listing of the various LED codes can be found in the
**Engineering Handbook**".

*Checked, and nothing is owed.* `002398-04` is walked whole and its record
carries the tables on four rows — pp. 86-87 the sixteen steady-state values and
the two-digit codes, pp. 88-90 the DN330/DSP90/DN5xx-T/DN560 families marked
`none` because this core does not model them, pp. 91-92 "our family's LED table"
whose Ext./Int. pairs are what `ap_boardreg.c` decodes. So `[AEGIS]`'s pointer
resolves into a walk already done, and the two documents agree. **A pointer from
one document into another is worth following even when the target says walked
whole** — this one came back clean, and that is the result, not an absence of
one.

*And a third agreement fell out of the check*: `002398-04` p. 85's boot errors
read "The SYSBOOT read from **records 2 thru B** did not ...", which is §27.1's
"physical disk blocks 2-B" and this project's measured "boot records 2..11" —
three sources, one number.

## What is owed

| Chapters | Pages | State |
| --- | --- | --- |
| 1-8 design, overview, object storage, locks, naming | 20-110 | owed |
| **9 virtual address space layout** | 111-118 | **done** |
| 10-13 virtual memory, its data structures, mapping/activation/purification, page fault resolution | 119-198 | owed — only §10.6.2's pure/impure page rules read |
| 14-17 process management, level 2 processes, eventcounts | 199-216 | owed |
| 18 fault handling in the kernel | 217-230 | owed — bears directly on this session's frame work |
| 19 SVC dispatching | 231-234 | owed — user/supervisor and ASID |
| 20 network overview | 235-242 | owed |
| **21 ring hardware** | 243-246 | **done** |
| 22-24 IPC data structures, network support, internet | 247-276 | owed |
| 25 introduction to system initialization | 277-278 | owed |
| **26 the bootstrap PROM** | 279-286 | **done** |
| **27 SYSBOOT, NETBOOT, CTBOOT** | 287-294 | **done** |
| 28-29 AEGIS initialization, user mode initialization | 295-308 | owed — only the 20-step init sequence's steps 17-20 read |
| **A boot LED codes** | 309-311 | **done** |
| B address space figures | 313-320 | owed |
| glossary and index | 321-370 | owed |
| **a second document bound in: *Memory Organization and Management*** | 371-426 | **owed, and not previously known to exist** — introduction, virtual space figures for DN330/DN560, "Memory Management Tables and Registers" for the **reverse-mapped MMU**, MMU operations in mapped mode, and an FPU section |

**The bound-in document at pp. 371-426 is the find to flag.** Fifty-six pages of
memory-management hardware architecture — registers, tables, mapped-mode
operations — inside a PDF whose name says nothing about it. It describes the
*reverse-mapped* MMU, which is not the DN3500's or DS5500's, so it is not
urgent; it is recorded here because a document that hides another document
inside it is exactly what a coverage record is for.

## Fidelity

Chapters 9, 21, 26, 27 and Appendix A read in full from the text layer, which is
born-digital and heavily OCR-damaged in the figures — Figure 9-1's segment
labels arrive as "Prlv.t. Re.d/Wrlt. 8tor.ge (I .egm.nt.)" — so **every figure
address and segment count above was cross-checked against the prose**, and where
the prose does not repeat a number it is not claimed here. No page was read as
an image; the two figures that would most repay it (9-1 and 9-2) are owed a
600-dpi read before any address in them is used as a source rather than as
context.
