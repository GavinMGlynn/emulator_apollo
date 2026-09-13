# EXABYTE EXB-8200 — walk coverage record

The DS5500's SCSI target. Identified 2026-09-13 from the guest's own
`/sys/mgrs/rmt_scsi`, which carries a four-entry INQUIRY vendor+product table
and matches `EXABYTE EXB-8200        `; detail in `docs/PROJECT_STATUS.md`,
*The SCSI target is an EXABYTE EXB-8200*.

| Tag | File | Pages | Text layer | State |
| --- | --- | --- | --- | --- |
| `[EXB]` | `docs/references/exabyte/510006-007_EXB-8200_User.pdf` | 141 | **born-digital** — `pdfimages -list` returns nothing at all | **READ WHOLE — 141 of 141, 2026-09-13.** Every chapter, the appendix and the glossary |
| `[EXBPS]` | `docs/references/exabyte/510005-006_Exabyte_EXB-8200_Product_Spec_Oct1990.pdf` | 74 | 600-dpi JBIG2 scan with an Acrobat OCR layer | **IN PROGRESS** — §8 read whole as page images, 2026-09-13 |

Two more are on the shelf and not yet opened: `510003-001` *Maintenance* (21
pages) and `510007-000` *Theory of Operation* (9 pages).

## Why these are here, and what is owed

`[WD7000]` specifies the host adapter completely and stops at the bus: "the
SCSI bus and its targets" is the last open sub-item of the SCSI plan item, and
a target needs the target's own manual. `fetch-the-parts-own-datasheet`.

**Owed:** `[EXBPS]` everything but §8 — 70 of its 74 pages. `[EXB]` is
finished. Nothing is implemented yet and nothing should be until both are read
— `read-the-whole-document`.

## `[EXB]` chapter map, from the running heads

| PDF | Chapter |
| --- | --- |
| 3-6 | 1 General Information |
| 7-12 | 2 SCSI Physical Description |
| 13-21 | 3 SCSI Physical Path Communications |
| 22-28 | 4 SCSI Commands |
| 29-30 | 5 ERASE (19h) |
| 31-34 | 6 INQUIRY (12h) |
| 35-38 | 7 LOAD/UNLOAD (1Bh) |
| 39-49 | 8 MODE SELECT (15h) |
| 50-59 | 9 MODE SENSE (1Ah) |
| 60-61 | 10 PREVENT/ALLOW MEDIUM REMOVAL (1Eh) |
| 62-66 | 11 READ (08h) |
| 67-68 | 12 READ BLOCK LIMITS (05h) |
| 69-71 | 13 RECEIVE DIAGNOSTIC RESULTS (1Ch) |
| 72 | 14 RELEASE UNIT (17h) |
| 73-82 | 15 REQUEST SENSE (03h) |
| 83-84 | 16 RESERVE UNIT (16h) |
| 85-86 | 17 REWIND (01h) |
| 87-93 | 18 SEND DIAGNOSTIC (Extended) (1Dh) |
| 94-99 | 19 SPACE (11h) |
| 100-101 | 20 TEST UNIT READY (00h) |
| 102-106 | 21 WRITE (0Ah) |
| 107-109 | 22 WRITE FILEMARKS (10h) |
| 110-118 | 23 Usage Notes |
| 119-... | 24 Tape Motion Command Execution Based on Tape Position |

## `[EXBPS]` §8, read whole 2026-09-13 (PDF 51-54, printed 41-44)

**§8**: ANSI SCSI **X3.131-1986 Rev 17B, conformance level 2**, sequential
access. Single-ended uses a **WD33C93/WD33C93A**, differential a
**WD33C92/WD33C92A** — the same SBIC family the ASC carries, so the datasheet
`WD7000_WALK.md` names as "next to fetch" is owed by both ends of the bus.
Asynchronous only; parity configurable through MODE SELECT; multiple
initiators; Disconnect/Reconnect/Arbitration implemented.

**§8.1 Physical Path**: eight-port daisy chain, distributed prioritized
arbitration, **asynchronous up to 1.5 MB/s (12 Mbit/s)**.

**Table 8-1, eleven messages**, with the sentence that bounds them: the
EXB-8200 "does not support the extended message format or the use of linked
commands". `00` Command Complete (in), `02` Save Data Pointer (in), `03`
Restore Pointers (in), `04` Disconnect (in), `05` Initiator Detected Error
(out), `06` Abort (out), `07` Message Reject (both), `08` No Operation (out),
`09` Message Parity Error (out), `0C` Bus Device Reset (out), `80`-`FF`
Identify (both).

**Table 8-2, the whole command set — 18 Group 0 commands**, with their ANSI
class: `00` TEST UNIT READY (O), `01` REWIND (M), `03` REQUEST SENSE (M), `05`
READ BLOCK LIMITS (M), `08` READ (M), `0A` WRITE (M), `10` WRITE FILEMARKS
(M), `11` SPACE (O), `12` INQUIRY (E), `15` MODE SELECT (O), `16` RESERVE UNIT
(O), `17` RELEASE UNIT (O), `19` ERASE (O), `1A` MODE SENSE (O), `1B`
LOAD/UNLOAD (O), `1C` RECEIVE DIAGNOSTIC RESULTS (O), `1D` SEND DIAGNOSTICS
(O), `1E` PREVENT/ALLOW MEDIA REMOVAL (O). **RESERVE and RELEASE are marked
"available with 2600-level MX code and above only"** — a firmware-revision
choice a model has to make, the same question `[WD7000]`'s errata page raised
at the other end.

## `[EXB]` chapters 1 to 4, read whole 2026-09-13

**Ch. 1** names the standards: ANSI X3.131-1986, ANSI Helical-Scan X3B5/89-136
Rev 6, and both WD33C9x datasheets. The manual "applies to EXB-8200s
containing **2600-level MX code and above**", with an appendix for Level 1 MX.
Capacity up to **2.5 GB formatted**.

**Ch. 2** is cabling, connectors and electrical levels — 50-pin, single-ended
6 m or differential 25 m, 220/330 Ω termination. **Nothing here is modelled**:
the ASC's own Z80 firmware hides the bus from the host, and this core's
interface to a target is the SCB, not a wire.

**Ch. 3, the message system.** The same eleven messages, each defined.
**LUN is always 0** — "the EXB-8200 does not support multiple devices". The
Identify message is `80h` or `C0h`: bit 7 identifies, **bit 6 DiscPriv** grants
disconnect privilege, bits 5-3 reserved, bits 2-0 the LUN. §3.2's sequence:
the initiator asserts ATN before `SEL` true and `BSY` false, the target answers
with Message Out, and **the first message after Selection is Identify** —
though "under some exceptional conditions" Abort or Bus Device Reset may come
first. On reconnection the target's first message is Identify, and an
**implied Restore Pointers** must be taken before it completes. §3.4: a
**one-second reselection timeout** is retried indefinitely until the request is
reset or the initiator sends Bus Device Reset. Abort terminates READ and WRITE;
for SPACE and ERASE it costs a variable delay (ERASE: **at least 60 seconds**
unless PEOT arrives first); for everything else the command runs to completion.
Bus Device Reset aborts everything and re-initialises.
§3.3's parity recovery is a page of rules this core cannot reach — the ASC
does its own retries — and is recorded rather than implemented.

**Ch. 4, the command shape.** All eighteen are **Group 0, six bytes**: opcode,
LUN in byte 1 bits 7-5, a three-byte field the command defines, and a control
byte whose **Flag (bit 1) and Link (bit 0) must both be 0** — linked commands
are not supported. A reserved bit that is not zero is **Check Condition, sense
key Illegal Request (5h)**, and so is an unsupported opcode or group code, and
so is a set Flag or Link. A bus reset, a new cartridge or a power interrupt
gives **Unit Attention (6h)** on the next command.
**Table 4-2, the four status bytes**: `00` Good, `02` Check Condition, `08`
Busy, `18` Reservation Conflict; bit 0 is always zero and bits 7, 6, 5 are
reserved or zero. Deferred error reporting is explicit: an error after status
has already been reported — a buffered write, or a command with Immed set —
comes back as Check Condition **on the next command from that initiator**.

## `[EXB]` chapters 5 to 8, read whole 2026-09-13

**Ch. 5 ERASE (19h).** Byte 01 bit 0 **Long, and only 1 is supported** — a
clear Long bit is accepted and does nothing. Erases from the current valid
position (LBOT, blank tape, or the start of a long filemark) to PEOT, then
rewinds; **about two hours for a 112 m cartridge**, erase running at read/write
speed. Runs in disconnect mode. Wrong position is Illegal Request (5h); a
write-protected cartridge is **Data Protect (7h) with the WP bit**; unloaded
but present is **Not Ready (2h)**.

**Ch. 6 INQUIRY (12h).** **56 bytes** (`38h`): 5 standard plus `33h` = 51
vendor-unique. Byte 00 device type **`01` sequential access**, or **`7F` if the
LUN is not 0**; byte 01 bit 7 **RMB = 1**, removable; byte 02 **`01`, ANSI
SCSI-1**; byte 04 additional length **`33h`**. Vendor identification at bytes
08-15, product identification at 16-31, **firmware revision level at 32-35**
(ASCII, e.g. "4.25"), 36-55 blanks. INQUIRY is answerable while another
initiator holds a reservation, and a Check Condition on INQUIRY **never builds
sense data** — the sense from before the INQUIRY stays valid.
> **The product string is taken from the guest, not from this manual.** The
> PDF's text layer drops hyphens — it renders the part's own name as "EXB
> 8200" in body text throughout — so bytes 08-31 are taken from `rmt_scsi`'s
> matching table, which is a byte-exact artefact: `EXABYTE ` then
> `EXB-8200        `.

**Ch. 7 LOAD/UNLOAD (1Bh).** Byte 01 bit 0 **Immed** — status on acceptance
rather than completion, with **Busy returned to every initiator until the
operation finishes**; byte 04 bit 0 **Load**, 1 load and 0 unload. Load
positions at LBOT, flushing the buffer to tape first if there is data in it,
and does nothing if already at LBOT. Unload rewinds to PBOT, unloads and
ejects — **unless PREVENT MEDIUM REMOVAL is set**, in which case it unloads
but does not eject. Unloading an already-unloaded drive is not an error; the
state afterwards is **Not Ready (2h)**. Loading with no cartridge is Not Ready
with the **TNP** bit; loading within **three seconds** of power-on or a
previous unload may raise a servo error, Hardware Error (4h) with **SSE**.

**Ch. 8 MODE SELECT (15h), and the defaults a model has to carry.** The
parameter list is a 4-byte header, an optional 8-byte block descriptor and up
to 5 vendor-unique bytes; Table 8-1 enumerates every legal Parameter List
Length from `00h` to `11h`, and **only 0, 4-9, and `0Ch`-`11h` are legal**.
Header byte 02 bits 6-4 **Buffered Mode**, `000` non-buffered or `001`
buffered and nothing else, **default buffered**; bits 3-0 Speed, **0 only**.
Block descriptor: **density code 0 only**, **number of blocks 0 only**, and a
block length where **0 means variable**; the **power-on default is 1,024 bytes
(`400h`)**, the maximum 240 KB (`3C000h`) or 160 KB (`28000h`) with ND set, the
minimum 1 byte in fixed mode.
Vendor-unique byte 00: **CT** cartridge type (with **P5** in byte 01 bit 0 —
`00` P6 Domestic, `01`/`11` P5 European, `10` PI International), **ND** no
disconnect during data transfer, **NBE** no busy enable (default 0, report
Busy), **EBD** even byte disconnect (default 0), **PE** parity enable (default
**0, disabled**), **NAL** no autoload (default 0, autoload **enabled**).
Byte 02 **Motion Threshold, default `80h` = 128 KB**, limits `20h`-`D0h`; byte
03 **Reconnect Threshold, default `A0h` = 160 KB**, same limits, and pinned at
160 KB when ND is set; byte 04 **Gap Threshold, default `07h`**, anything above
`07h` treated as `07h`.
§8.5's errors are all Illegal Request (5h), and one of them is a *cross-command*
rule worth carrying: **READ or WRITE with Fixed set and a block length of 0, or
Fixed clear and a block length other than 0, is an error** — the two must
agree.

## `[EXB]` chapters 9 to 22, read whole 2026-09-13

**Ch. 9 MODE SENSE (1Ah)** returns **17 bytes** (`11h`): the same header, block
descriptor and five vendor-unique bytes MODE SELECT takes, read back. The
header gains two fields MODE SELECT does not have: byte 00 **Sense Data
Length**, and byte 01 **Medium Type**, the autosized cartridge — `81h`-`85h`
for P6-15 through P6-120 and `C1h`-`C4h` for P5-15 through P5-90, with Tables
9-1 to 9-3 giving each one's blocks from LBOT to LEOT and from LEOT to PEOT.
Byte 02 bit 7 is **WP**, and **0 when no cartridge is loaded**. The block
descriptor's Number of Blocks is no longer forced to zero on the way out: it is
**the physical 1,024-byte blocks from LBOT to LEOT plus `500h`, the blocks LBOT
itself occupies** — and the manual warns the count may read one high before
anything has been written. Block Descriptor Length always comes back `08h`.
A cleaning cartridge is recognised as such and runs a preset cleaning cycle.

**Ch. 10 PREVENT/ALLOW MEDIUM REMOVAL (1Eh)**: byte 04 bit 0 **Prevent**, which
also **disables the front-panel unload button**. The condition ends only on an
ALLOW **from the initiator that set it**, a Bus Device Reset message, or a SCSI
bus reset.

**Ch. 11 READ (08h)**: byte 01 bit 1 **SILI**, bit 0 **Fixed**; bytes 02-04 are
a **byte count when Fixed is 0 and a block count when it is 1**, and zero is
legal and moves nothing. SILI in fixed mode is Illegal Request (5h). The
terminations are the model's whole read state machine: a **filemark** leaves
the head on the EOT side with **FMK, sense key No Sense (0h)**; **PEOT** gives
**EOM + PEOT, Medium Error (3h)**; **blank tape** gives **Blank Check (8h)**;
a length mismatch gives **ILI with No Sense (0h)** and a signed difference in
the Information bytes — *and SILI suppresses only the last of those four*.
**LEOT does not terminate a read.** §11.4: a failed block is reread **nine
times, ten attempts in all**, with the servo varying its track offset on each,
before Medium Error (3h).

**Ch. 12 READ BLOCK LIMITS (05h)**: six bytes, **maximum `03C000h` (240 KB)**,
or `028000h` (160 KB) when ND is set, **minimum `0001h`** — one byte.

**Ch. 13 RECEIVE DIAGNOSTIC RESULTS (1Ch)**: six bytes — additional length
`0004h`, two overflow bits **Rovfl/Tovfl**, then the **Tracking**, **Reread**
and **Write Recovery** counters. Valid only after a SEND DIAGNOSTIC. The
first two reset on load, unload, rewind or reset, or on a REQUEST SENSE with
**RC**; the third only on a drive reset.

**Ch. 14 RELEASE UNIT (17h)** and **Ch. 16 RESERVE UNIT (16h)**, both
**2600-level MX and above only**. A reservation holds until another RESERVE or
a RELEASE **from the same initiator**, or a Bus Device Reset or hard reset. A
reserved drive answers **only INQUIRY and REQUEST SENSE** from anyone else;
everything else is **Reservation Conflict (18h)** — and so is a REQUEST SENSE
**with RC set**, which is the one case where the RC bit changes who may issue
it. Third-party reservation is **not implemented**: attempting it is Check
Condition.

**Ch. 15 REQUEST SENSE (03h), the biggest structure in the part.** **26 bytes**
(`1Ah`) of **Error Class 7** extended sense; an allocation of **0 transfers
four bytes**, not none. Byte 00: **Valid** in bit 7, error class always `7h`,
error code always `0h`. Byte 02: **FMK**, **EOM**, **ILI**, then the **sense
key** — and the key set is the ANSI one **plus a vendor-unique `9h`, EXABYTE**,
raised by the **TMD** and **XFR** bits alone; `1h`, `Ah`, `Ch` and `Eh` are
unused, `Dh` is **Volume Overflow**. Bytes 03-06 Information, 07 additional
sense length **always `12h`**, 12-13 **ASC/ASCQ** with exactly four defined
pairs (`00`/`00` no additional sense, `04`/`00` volume not mounted, `04`/`01`
rewinding or loading, `30`/`02` incompatible format), 16-18 the **Read/Write
Data Error Counter**, 19-21 the **three Unit Sense bytes** and their nineteen
named bits (`PF BPE FBPE ME ECO TME TNP LBOT` / `XFR TMD WP FMKE URE WE1 SSE
FE` / `PEOT WSEB WSEO`), and 23-25 **Remaining Tape** in 1,024-byte blocks.
Sense is **cleared by the next command from that initiator that is not REQUEST
SENSE or INQUIRY**, or by a Bus Device Reset or bus reset; the key reported is
**the most catastrophic of the simultaneous errors**.

**Ch. 17 REWIND (01h)**: byte 01 bit 0 **Immed**. A rewind after a write
**flushes the buffer first**. After an immediate rewind only a CDB error or a
permanent write error on the flush is reported at once; everything else waits
for the next command.

**Ch. 18 SEND DIAGNOSTIC (1Dh)**: byte 01 bits 2-0 **SelfTest/DevOfL/UnitOfL**
select exactly five legal tests — **000** set up counters (with the two-byte
parameter list, whose only supported Diagnostic Option is **`0001h`**), **100**
power-on without tape, **101** power-on plus write/read/filemark/load without
tape, **110** power-on with tape, **111** power-on plus write/read/filemark with
tape. Any other combination is Illegal Request (5h); a wrong *setup* is too, and
a write-protected cartridge on 101 or 111 is Data Protect (7h). A power-on
failure sets **FBPE** for RAM or **SSE** for servo and ejects the tape; a
functional failure leaves the tape where it stopped.

**Ch. 19 SPACE (11h)**: byte 01 bits 1-0 **Code**, `00` blocks and `01`
filemarks and **nothing else**; bytes 02-04 a **signed 24-bit count**, positive
forward, negative backward in two's complement, zero legal. Terminations:
a filemark while spacing blocks is **FMK + No Sense (0h)**; **PEOT** is
**EOM + PEOT + Medium Error (3h)**; **PBOT or LBOT** is **EOM + LBOT + No Sense
(0h)**; blank tape is **Blank Check (8h)**; a miscount is the vendor-unique
**EXABYTE (9h)** key, and the manual prints the four host recovery recipes for
it. In every case the **Information bytes hold the count not done**. §19.2: a
backward space block is now about **20 seconds** rather than a rewind and a
re-read.

**Ch. 20 TEST UNIT READY (00h)**: Good when a cartridge is loaded and ready.
No cartridge is **Not Ready (2h) + TNP + ASC `04h`/ASCQ `00h`**; present but
not loaded is the same without TNP; and **an unload done with the front-panel
button is Unit Attention (6h) + TNP** — a different key for the same physical
state, which is the kind of distinction only the part's own manual carries.

**Ch. 21 WRITE (0Ah)**: byte 01 bit 0 **Fixed** and the same byte-or-block
transfer length. **Write mode lasts from the first WRITE until a REWIND,
UNLOAD, LOAD, SPACE or WRITE FILEMARKS**, and while it lasts with data in the
buffer **the front-panel button is dead**. A write is legal **only at LBOT, at
blank tape, or at the BOT side of a long filemark** — anywhere else is Illegal
Request (5h). **LEOT switches buffered mode to non-buffered** and ends the
command with **EOM + No Sense (0h)**; **PEOT** gives **EOM + PEOT** and
**Volume Overflow (Dh) if data remains in the buffer**, No Sense (0h) if not.
Read-after-write rewrites a failed physical block elsewhere; **twelve attempts
at one block, or eighteen consecutive failed blocks (about two tracks)**, ends
the command with Medium Error (3h), `ME` and for the first case `WE1`.
Above **246 KB/s**, or writing from LBOT, the buffer fills and the drive
**disconnects** until the reconnect threshold is free again.

**Ch. 22 WRITE FILEMARKS (10h)**: bytes 02-04 the count, **zero meaning "flush
the buffer and write nothing"** and not an error; byte 05 bit 7 **Short**.
**Two filemark kinds**: a short one occupies **480 KB (60 tracks)** and cannot
be written over except from LBOT or a previous long filemark; a long one
occupies **2,160 KB (270 tracks)** and carries a long erase gap that a write
may append into, erasing the filemark and everything after it. Same position
rule as WRITE. LEOT keeps writing to the requested count and then reports
**EOM + No Sense (0h)**; PEOT stops and reports **Volume Overflow (Dh)** with
the unwritten count.

## `[EXB]` chapters 23 and 24 and the appendix, read whole 2026-09-13

**Ch. 23's numbers are the ones a model needs and no command chapter states.**
After a power-on reset, SCSI reset or Bus Device Reset the drive **cannot
answer the bus for at least 300 ms**; it then accepts commands during
initialization, **executing those that do not need the tape and answering Busy
to those that do**, and **TNP is not valid for at least five seconds**. The
first command after power-on other than REQUEST SENSE or INQUIRY is **Unit
Attention (6h)**. The **physical block is 1,024 bytes** and a logical block that
is not a multiple of it wastes the remainder as gap bytes — §23.5 works the
1,536-byte case out to a quarter of the cartridge. A **track is eight physical
blocks**; a short track is padded with **gap blocks**, and a **whole gap track**
follows the last data track, with the tape backing up **nine tracks** to
re-synchronise on the next write. A first write at LBOT takes about **37
seconds** to reach tape. **Tape tension is released after 5 seconds idle at
LBOT or 15 seconds elsewhere, and the drum stops after 75 seconds**; recovering
costs **1.5 seconds** from tension release and **5 seconds** if the drum also
stopped, and a drum that will not restart is Hardware Error (4h) with **SSE**.
§23.9's **eight DIP switches** are the power-on defaults: memory test (65 s) or
bypass (8 s), parity, even-byte disconnect, no-busy-enable, **fixed or variable
block mode**, no-disconnect, reserved, and P6 or PI cartridge type. §23.12: a
cartridge written in EXB-8500 format reads back as **Blank Check (8h) with
ASC `30h`/ASCQ `02h`**, and the same table gives blank tape **Blank Check with
EOM and LBOT and ASC/ASCQ `00`/`00`** — while WRITE and WRITE FILEMARKS
succeed on both.

**Ch. 24 is the position state machine**, twelve sections of "after X, command
Y does Z", and it is the chapter a target model is checked against rather than
written from — every rule in it is a consequence of ch. 11, 19, 21 and 22's
terminations plus one fact those chapters leave implicit: **after a write or a
write-filemarks, a READ or forward SPACE block is Illegal Request (5h)**, not
Blank Check, because the head is inside the gap track rather than at end of
data. A backward space filemark past the first filemark lands at **LBOT with
EOM + LBOT + No Sense (0h)**.

**The Appendix** is the Level 1 MX card's eight jumpers, the same eight choices
as the Level 2 card's DIP switches with **J2 and J4 reading the opposite way
round** from switches 2 and 4 — a trap for anyone deriving defaults from the
wrong card.

**The glossary** defines LBOT, LEOT, PBOT and PEOT precisely, which the command
chapters use without defining: **PBOT is where the tape meets the leader**,
**LBOT is where a load or rewind leaves it**, **LEOT is the warning point
before PEOT**, and **PEOT is where the tape meets the trailer**.
