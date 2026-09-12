# `AEGIS Internals and Data Structures` — walk coverage record

| Tag | File | Pages | Text layer | State |
| --- | --- | --- | --- | --- |
| `[AEGIS]` | `bitsavers/AEGIS_Internals_and_Data_Structures_Jan86.pdf` | 426 | born-digital, heavy OCR damage | **ALL 426 PAGES PASSED OVER — 7 chapters and Appendix A read in full; the other 22 chapters and Appendix B read in a condensed pass** |

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

## Chapter 18, Fault Handling in the Kernel — read whole (pp. 217-230)

**§18.2.1.1 decodes the crash line this session read off a DS5500**, field by
field, from a machine three releases older:

```
FAULT IN AEGIS:
03077B88: SR:2008 PC:S012CCC FF:B008 (8) FA:3FFFFFF SW:0155
CRASH STATUS 00040004 ECB 00000000 PIO 0002
```

against

```
FAULT IN DOMAIN/OS:
7A543FC4: SR:0000  PC:0080000C  FF:7008 (B)  FA:0080000C  SW:0146
Crash_Status 0012004B  PC 7A42D86A pid 0001
```

- The leading address is **the fault frame's own address on the supervisor
  stack** — "you must patch the actual frame on the stack (using the address
  displayed above)". `7A543FC4` is where the frame is, not what faulted.
- "The routine displays the **fault address (FA) and special status word (SW)
  only on bus/address errors**." So their presence is itself the classification.
- "**The frame format word (FF) is followed by the letter error identification
  from the PROM.**" `FF:7008 (B)` is format `$7`, vector offset `$008`, and `B`
  — which `002398-04`'s MD code list, already walked, gives as **bus error**
  (`A` address error, `B` bus error, `S` trap or breakpoint, `U` unimplemented
  instruction, `Z` divide by zero). Two documents, one letter.
- "**All registers except the stack pointer (SP) remain as they were when the
  fault occurred**", and `G,G *+f` returns control to the point of the fault.

**A candidate explanation, since REFUTED — and by the error-code list in a
handbook revision nobody had walked.** `002398-01` chapter 4 pairs every AEGIS
error code with its text, and module **`0012` is the fault module**: `00120001`
odd address error, `00120002` illegal instruction, `0012000C` bus time-out,
`00120011` access violation, `0012001E` memory parity error — and **`00120020`
"supervisor fault while resource lock(s) set"**, which is `fault_$while_lock_set`
by its own words. **The DS5500's `Crash_Status` is `0012004B`, not `00120020`**,
so the lock-check route below is not what happened. The module is right — the
crash *is* a fault — and the specific condition is not this one.

*And `004B` cannot be decoded from anything on this shelf.* The module-12 list
runs to `0020` in Rev 1 (1983), `002C` in Rev 3 (1985) and `003A` in Rev 4
(1987); `004B` is a later addition and SR10.4 is 1992. **A named gap with a
known shape**: one of roughly seventeen fault codes added after 1987.

*The refuted reading is kept below because the reasoning was sound and only the
evidence was missing, which is the difference between a bad guess and an
unverified one.* §18.2.1.1 says `fault_$crash` — the routine
that prints the block above — is called when the fault occurred *in supervisor
mode*, and the DS5500's last frame has `SR:0000`, user mode. §18.2.4 supplies a
second route to the same crash: `fim_$com` "checks to see if the faulting
process holds any mutex or exclusion locks (via `proc1_$inhibit_check`). If this
check fails, **the system crashes** with `fault_$while_lock_set` status, since
there are no circumstances in which the AEGIS kernel should exit to user mode
with a kernel lock held." A user-mode fault can therefore crash the system.
**Decoded 2026-09-12, as far as the documents allow, and the answer is that they
do not reach it.** `002398-04` p. 4-6 names module `12` outright — **`OS / fault
handler`** — and pp. 4-6/4-7 print its codes `0001` through **`003A`**, ending
there and moving to module `13`. **`4B` is past the end of that list.** Rev 4 is
January 1987 and this machine runs SR10.4 of 1992, so the handbook is five years
older than the code; it is not a misprint to hunt for but a range the document
does not cover. `docs/references/002398-04_WALK.md`'s rows for PDF 74-75 carry
the module table.

**And §18.2.4's crash is a *different* code, which is the useful half.**
`(00120020) supervisor fault while resource lock(s) set` is printed there word
for word — that is `fault_$while_lock_set`, the crash the chapter describes. So
the observed `0012004B` is **not** the one §18.2.4 predicts, and the chapter's
route is not what this machine took. *What would settle `4B`*: an SR10-era
status list, which this project has searched for and not found; the shelf's own
`002398-01` chapter 7 gives the `status_$t` layout but no module `12` codes.
Closed as a documentation-absent decode rather than carried as unchecked.

**§18.2.2 is a clause about this core, and this core already satisfies it.** The
privileged-instruction handler "checks for a **Move from SR** instruction. This
instruction was not privileged on the 68000, but became privileged on the 68010
and 68020. If the handler finds that the instruction that incurred the fault was
a Move from SR, it **ignores the fault (the instruction is No-oped)**." So
Domain/OS depends on the processor faulting a user-mode `MOVE from SR` in order
to emulate the 68000's behaviour. `ap_m68030_single.h` documents exactly that
rule and `single_suite` asserts
`ap_m68030_single_privileged(AP_M68030_SINGLE_MOVE_FROM_SR)`. Confirmed, nothing
owed — but it is the kind of dependency that would have been invisible from the
processor manual alone, which says only that the instruction is privileged.

*Also captured*: the fault interceptor module is two modules, `fim_wired` and
`fim_unwired`, split by whether a handler "must be able to run without taking a
page fault; in particular, the **page fault handler itself**". Memory-management
faults are handled entirely in the kernel and are "generally invisible to
user-mode programs" — the common path runs only if a manager such as
`mst_$touch` reports an error, which is why a DS5500 servicing 362 MMU faults
prints nothing. §18.2.4.2 is a **fault-on-fault** check: a per-process flag
`fim_$in_fim[asid]`, and on re-entry the process is deleted. §18.2.3 sends
address, parity and bus errors to `fim_$abcom` rather than `fim_$com`, which
releases `ec2_$lock` or `pbu_$lock` if held and crashes if "the faulting address
is above the supervisor global boundary". The diagnostic frame is flagged with
the pattern **`DFDF`**, "which makes it easy to identify diagnostic frames within
stack dumps" — a magic word worth having when reading DS5500 memory.

## Chapter 19, SVC Dispatching — read whole (pp. 231-234)

"User-mode code gains access to these modules through the **SVC trap
instruction**... Each trap handler has a table of entry points to the supervisor
subroutines, called the **SVC dispatch table**. The **SVC number passed in `D0`**
is the index into the handler's dispatch table."

That is the arrangement measured today from the other end: Domain/OS replaced
vectors 32-46 — `TRAP #0` through `TRAP #14`, all fifteen — and left `TRAP #15`
to the PROM. **Each trap number is a separate handler with its own dispatch
table**, which is why fifteen of them are taken rather than one, and the DS5500
run's `1 x vector 35` and `1 x vector 39` are calls through handlers 3 and 7.

§19.2 separates two things that look alike: "Running in supervisor mode is not
identical to using ASID 0" — the operating system's code lives in shared
supervisor space, but a process running in supervisor mode still has its own
ASID for its private space.

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

## What the condensed pass over the other chapters gave

Read page by page but not sentence by sentence. Everything here is a quotation
or a figure this project can check; anything that would need the page image is
said to.

**Chapter 4, the disk, reconciles this project's volume units.** §4.1: "The
AEGIS system defines a disk block as **1024 bytes of data plus a 32-byte disk
block header**. (Floppy disk blocks do not have disk block headers.)" That is
**1,056 bytes**, which is what this project measured, and it settles the
relationship §9.1 leaves open: the disk block stayed 1,056 bytes while the
*memory* page grew from 1,024 bytes to the DS5500's 4,096, so a page went from
one block to **four**. "A record is a page; a DS5500 page is four 1056-byte
sectors" is that arithmetic.

The block header carries the owning object's UID, the block's page number within
the object, the last-written time, the block type (data `0`, or a level 1, 2 or
3 file map), the object type (file `0`, directory `1`, system directory `2`), a
software checksum "used only if read-after-write checksumming is turned on", and
the block's own physical disk address. It exists so that "**SALVOL can
reconstruct the disk even if the volume table of contents has been destroyed**".

**And one measured number looked wrong until the image was read.** §4.2.1:
"INVOL always creates the PV label on the **first block (physical DADDR 0)**",
it "is a single disk block", it has "a canned UID of **200.0**", and §4.3: "The
first block of the logical volume (**physical block 1** for the first logical
volume, logical block 0) contains the logical volume label." This project had
measured the DS5500's PV label at sectors 0-3 and its LV label at 4-7 — four
units each where the document gives one block each.

*Settled offline against `media/dn5500-invol-done.awd`, and the document is
right about its own release.* Every 1,056-byte sector begins with the 32-byte
header §4.1 describes, and its first field is the owning UID:

```
sector 0  00 00 02 00 00 00 00 00 00 00 00 00  A4 61 60 72 ...   data: ..APOLLODN5500
sector 1  00 00 02 00 ... (same UID, same timestamp)             data: empty
sector 2  00 00 02 00 ...                                        data: empty
sector 3  00 00 02 00 ...                                        data: 4 bytes
sector 4  00 00 02 01 00 00 00 00 00 00 00 00  A4 61 60 3D ...   data: ..DN5500
sector 5-7  00 00 02 01 ... (same UID)                           data: empty, empty, 8 bytes
sector 8  A4 61 5F 51 30 01 23 45 ...                            a real UID, not a canned one
```

`00000200` is **the canned UID 200.0 this section names**, and it owns four
consecutive blocks whose bodies differ — so they are four *pages of one object*,
not four copies. `00000201` owns the next four. **The labels occupy one page
each, and a DS5500 page is four blocks.**

So the two accounts are the same account in different units: at SR9.0 a page was
one 1,056-byte block, so "physical DADDR 0" and "sector 0" named the same thing;
on a DS5500 the page is four blocks and the addressing unit followed it. **The
same conversion settles §27.1's SYSBOOT**: "physical disk blocks 2-B" is pages
2-11, which is sectors 8-47 — exactly what this project measured. Nothing was
wrong; one unit had changed underneath the word "block", which is the kind of
thing only reading both a document and an image catches.

Also §4.2: a physical volume is the PV label, one or more logical volumes, a
**badspot cylinder** ("usually one of the last two cylinders") and a
**diagnostics cylinder** ("typically the last or next-to-last"); INVOL writes an
**alternate logical volume label**, "a copy of the logical volume label", and
records its address in the PV label.

**Chapter 10-13, virtual memory.** Three address spaces: object (network-wide,
**96-bit** UIDs), virtual (**32-bit**), physical. ASIDs 2-25 for level 2
processes, 1 always the display manager, 0 supervisor global. §10.7.1 gives the
reverse-mapped hardware — a **page translation table of 1,023 physical page
numbers**, mapping "1 megabyte of virtual memory at a time", plus a page frame
table — which is what Appendix A's LED code `D - PTT enabled` refers to. §10.7.2
gives the forward-mapped one: 32 regions of 256 segments of 32 pages, a segment
map per region, 32 hardware region registers. §12.1.2 confirms the guard
segments from the other end: `mst_$set_guard` "sets the guard bit in the MSTE
... **The process manager (PM) calls this routine when it sets up the stack
object**". §13 gives the fault path — FIM → `mst_$touch` → `ast_$touch` →
`pmap_$touch` — and one sentence that matters here: on forward-mapped MMUs there
is no install step, because "**the MMU hardware will read in**" the tables
itself. §13.6: a reverse-mapped MMU "can only recognize **one**
virtual-to-physical page association at a time", a restriction the DS5500's
hardware does not have.

**Chapter 15 makes a checkable claim about interrupts**: "The M680x0 processor
supports seven interrupt levels (IL). **Every interrupt service routine (ISR)
runs at IL 6.** ... The AEGIS system does not support interrupt priority levels
for interrupt routines." A DS5500 boot takes vectors 160, 161, 165 and 174, so
this is observable — the status register's mask inside each handler.
**CHECKED 2026-09-12, and this core agrees, from a measurement taken
independently of the claim.** `AP_INTR_CPU_LEVEL` is **6**: the level the 8259
master's `INT` output drives, measured in `FINDINGS.md` C12 by sweeping the
CPU's interrupt mask — a mask of 6 permits only level 7 and blocks it, a mask of
5 lets it through — with the control experiment that makes the reading sound
(nothing able to interrupt, so a forced `SR` stays where it is put). Neither
`008778-03` nor `019411-A00` states the level, so C12 had to measure it, and
§15 is a third party arriving at the same number.
**Every device reaches the CPU through those two parts** — the slave cascades
into the master on `AP_INTR_CASCADE_LINE` — so every ISR dispatched through the
interrupt controller runs at IL 6, which is the claim. **The one exception is
not an ISR**: `AP_BOARD_PARITY_LEVEL` is **7**, and `ap_board.c` takes it first
because "it is level 7 and nothing the 8259s can raise" — a parity NMI, not an
interrupt service routine. *Verified; nothing owed.* The original text of this
row said "has not been checked. *Not verified.*", and
`a-recorded-check-is-not-a-done-check` is why it did not stay that way.

**Chapter 20-24, the network.** §20.2.1: "The ring hardware sets up **two DMA
channels** to receive a single packet; one channel receives the packet header,
while the other receives the packet data." §22.1.1: "the ring hardware **ANDs
the type bits with its hardware type mask register**" — which is the DN3xx/DN5xx
`9800`-page `TMASK` at `+04` that `RING.md` findings 55 and 92 already carry, and
**not** the AT-board generation this core models, whose mask findings 132b-133a
established is software state signalled with an eventcount. So `[AEGIS]` is a
third witness for the older controller and leaves finding 133b open exactly as
it stands. §22.1.1.2's **early acknowledge byte** and the message separator are
already modelled from `[MAC]` Figure 2-7. Sockets are numbered 1-30; the network
buffer pool is "**192** at present" virtual pages; a network number is 32 bits
and there can be at most **64** networks in an internet.

**Chapters 14-17, processes.** Eight of the 32 level 1 processes are reserved to
the kernel at initialization and 24 remain; new level 2 processes get priority
bounds 3 to 14; a process is bound, waiting, suspended, suspend-pending, or at
time-slice end with a resource lock held. The mutex manager guarantees
first-in/first-out grant.

**Chapters 28-29, initialization.** `COLD_START` then `os_$init`, then the
bootshell, then ENV, then DM/SH/SPM. `pm_$init_first` "initializes the user
global space read/write storage (RWS) by creating a backing file for the storage
(`'node_data/global_data`)" and "attempts to load the correct library, based upon
the **machine type ID and the PEB kind ID**", falling back to `syslib`. The
bootshell's command list (Table 29-1) includes `DMTVOL`, `IN` (invoke the loader
to install a named file), `LD`, `LO` and "copies of the MD debugging commands,
including the assembler/disassembler".

**Appendix B is seven pages of figures and is owed a page-image read.** Physical
and virtual memory layouts per node — `400-3FFF`, `4000-7FFF`, `8000-1FFFFF`,
`200000-AFFFFF`, `700000-7FFFFF` are legible in the text layer and the rest is
not. No address from it is used here.

## The second document, and it is a hardware manual

pp. 371-426 are **not part of AEGIS Internals**. They are a chapter of a
hardware manual — "This chapter describes the hardware architectures of the
memory organization and memory management schemes, **as they are implemented in
DN330, DN560, and DSP90 nodes**" — bound into the same PDF, with its own chapter
numbering starting at 2.

It carries seven tables of virtual and physical memory maps for those three
nodes, a **Memory Control/Status Register** with a frozen parity-error address,
byte parity error flags at bits 11-8, a bit that "forces any memory write to
generate a bad parity bit in memory" and one that "enables an MC68020 level-7
interrupt" on a parity error, and the MMU's mapped-mode behaviour: it
"translates the **26-bit** virtual address into a **22-bit** physical byte
address".

**None of it is this core's hardware** — those are 68020-era reverse-mapped
nodes, where the DN3500 and DS5500 are forward-mapped — so nothing here is owed
to the model. It is recorded because a 56-page hardware manual invisible inside
a document named for something else is exactly what a coverage record exists to
surface, and because if a reverse-mapped node is ever modelled this is where its
registers are.

## What is owed

**Full sentence-by-sentence reads**, which is the standard the other walk
records hold themselves to, are owed for everything not marked **done**. The
condensed pass above is a genuine page-by-page read and is enough to say what
each chapter contains and to quote it; it is not enough to promise that no table
row was missed.

| Chapters | Pages | State |
| --- | --- | --- |
| 1-8 design, overview, object storage, locks, naming | 20-110 | condensed |
| **9 virtual address space layout** | 111-118 | **done** |
| 10-13 virtual memory, its data structures, mapping/activation/purification, page fault resolution | 119-198 | condensed |
| 14-17 process management, level 2 processes, eventcounts | 199-216 | condensed |
| **18 fault handling in the kernel** | 217-230 | **done** |
| **19 SVC dispatching** | 231-234 | **done** |
| 20 network overview | 235-242 | condensed |
| **21 ring hardware** | 243-246 | **done** |
| 22-24 IPC data structures, network support, internet | 247-276 | condensed |
| 25 introduction to system initialization | 277-278 | condensed |
| **26 the bootstrap PROM** | 279-286 | **done** |
| **27 SYSBOOT, NETBOOT, CTBOOT** | 287-294 | **done** |
| 28-29 AEGIS initialization, user mode initialization | 295-308 | condensed |
| **A boot LED codes** | 309-311 | **done** |
| B address space figures | 313-320 | condensed — **owed a 600-dpi read**, the figures do not survive the text layer |
| glossary and index | 321-370 | condensed |
| **a second document bound in: *Memory Organization and Management*** | 371-426 | **condensed, and not previously known to exist** — see the section above |

**The bound-in document at pp. 371-426 is the find to flag.** Fifty-six pages of
memory-management hardware architecture — registers, tables, mapped-mode
operations — inside a PDF whose name says nothing about it. It describes the
*reverse-mapped* MMU, which is not the DN3500's or DS5500's, so it is not
urgent; it is recorded here because a document that hides another document
inside it is exactly what a coverage record is for.

## Fidelity

Chapters 9, 18, 19, 21, 26, 27 and Appendix A read in full from the text layer, which is
born-digital and heavily OCR-damaged in the figures — Figure 9-1's segment
labels arrive as "Prlv.t. Re.d/Wrlt. 8tor.ge (I .egm.nt.)" — so **every figure
address and segment count above was cross-checked against the prose**, and where
the prose does not repeat a number it is not claimed here. No page was read as
an image; the two figures that would most repay it (9-1 and 9-2) are owed a
600-dpi read before any address in them is used as a source rather than as
context.
