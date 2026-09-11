# `QIC-02 Rev D` walked whole — coverage record

*Proposed 1/4 Inch Cartridge Tape Drive Intelligent Interface Standard*,
September 23, 1982. **29 PDF pages, 28 numbered.** Obtained from the Internet
Archive's bitsavers mirror (`bitsavers_archiveQICSep82_1276068`) and kept at
`docs/references/bitsavers/QIC-02_Rev_D_Specification_Sep82.pdf`, which is
gitignored like every other PDF here.

## Why this file exists

`CLAUDE.md`: *a document that turns out to contain one unimplemented thing is a
document that must be read WHOLE — page by page, line by line.* This one had
been quoted from without ever being held, and the quotations came from
elsewhere. **Both Apollo tape documents defer to it and neither reproduces it**:
`08845` §12.1.10 and `[SC499]` §1.13.1 each say only that the device "transfers
the standard six bytes", and the standard is where the six bytes are defined.

It was opened to settle one question — does the drive have a bit for "read found
no data" — and §5.1 to §5.4 gave two defects and a correction on the first
reading. So it is a document to be derived in full rather than queried.

**What sent us here** was `002398-04` p. 4-14: Domain/OS module `28`, the
cartridge tape manager, spends twenty-eight status codes on this drive, and its
decode of them is §5.3's exception summary read from the software side. The
machine's own driver is written against this standard.

## How to read this record

One row per page range, **including the ones that yielded nothing** — a walk
whose record only lists the hits cannot be told from a walk that sampled. Page
numbers are the PDF's; the printed page is one less (PDF 2 is page 1).

Render as IMAGES, never a text layer:

    pdftoppm -f N -l M -r 150 -png \
      docs/references/bitsavers/QIC-02_Rev_D_Specification_Sep82.pdf \
      /home/gavin/apollo-scratch/c151/qic02/p

## Resume here

**READ WHOLE — 29 of 29 PDF pages, every section, every timing diagram, every
row of every table.** Nothing is owed.

Five defects and a closed open question came out of it, all in `ap_qic`; the
detail is in `docs/PROJECT_STATUS.md` under *The tape drive's status bits, from
the standard both its manuals defer to*. What the document leaves genuinely
open is at the bottom of this file.

## What it has already yielded

Five defects and a correction, all in `ap_qic`. From §5:

- **`NDT` was defined and never set.** §5.2 byte 1 bit 5 and §5.4 item 8 define
  it as "No recorded data found on tape", which is exactly a read past the last
  block of a `.ct`. Domain/OS spends three status codes on it.
- **`DEC` and `URC` were never cleared.** §5.2 says "These bytes shall be
  cleared by a Read Status Sequence" of each in turn; `ap_qic_read_status`
  cleared the two latches beside them and left the counters standing.
- **`NDT` does not travel alone.** §5.3 row 8 prints it with `UDA` and `BNL`,
  which is why the fix sets three bits and not one — and why a test that reached
  the "End of media" row by reading one block too many was reaching row 9.

From §3.5, §4.1 and §4.2:

- **SELECT's low nibble is a drive mask**, not part of the opcode. Selecting
  drive 2 was reported as an unimplemented command; it is a legal SELECT of a
  drive that is not there.
- **A reset defaults selection to drive 0**, said by pin 32 and again by §4.2.1.
  This drive came up deselected and refused every command until a SELECT
  arrived.
- **`USL` was unreachable**, and two of `ILL`'s six causes were recorded as out
  of reach when the mask makes both reachable.

Detail, with the reasoning, is in `docs/PROJECT_STATUS.md` under *The tape
drive's status bits, from the standard both its manuals defer to*.

## Coverage

| PDF pages | Section | Yield | Notes |
| --- | --- | --- | --- |
| 1–2 | cover, title | `none` | "PROPOSED 1/4 INCH CARTRIDGE TAPE DRIVE INTELLIGENT INTERFACE STANDARD", September 23, 1982 |
| 3–4 | table of contents | **the shape of the document** | §3's six interface sections plus **eight timing diagrams** (§3.6.1–3.6.8: read status, reset, select, BOT/initialise/erase, write data, read data, write-file-mark, read-file-mark) — this core has the command layer and, of the timings, only the ones `[SC499]` restates. §4.2's **ten** standard commands and §4.3's **twenty-three** optional ones. `ap_qic.h` called its twelve "the whole command set, no gaps", which is true of `[SC499]`'s list and not of this one — and the gap turned out to be inside an opcode rather than beside it, in SELECT's drive nibble |
| 5 | §1 scope, §2 definitions | **`RETENSION` is `INITIALIZATION`**, and Apollo says so too | Nineteen definitions. "cartridge initialization - an operation which restores normal tension by wind and rewind of the cartridge" is what `AP_QIC_CMD_RETENSION` (`0010 0100`) does, so the §4.2.5 command this core appeared to lack is one it has under `[SC499]`'s name. **Confirmed by Apollo's own parenthesis**: `002398-04` p. 10-9 lists the DN5xx tape controller's command `17` as "Initialize (retension) tape". Also `continuable` and `fatal` as §5.4's two classifications, `file mark` "an identification mark following the last block in a file", and `early warning` as the marker `EOM` is detected by |
| 6 | §3.0–§3.4, interface, levels, terminations, loading | `none` | 50-conductor edge connector, 3M 3415-0001; TTL levels; 220 Ω/330 Ω termination; 3 m maximum cable; **up to four devices on the interface**, which is the sentence the drive mask exists for |
| 7–8 | §3.5, pin assignments | **the two pins this model is short of, and one correction** | `ONL-` (28) is "activated prior to transferring a READ or WRITE command and deactivated to terminate that READ or WRITE command" — the pin `ap_qic.h` names as the reason two `ILL` causes are out of reach, and the *termination* the other two need. **`RST-` (32) "causes device initialization to be performed, default selection to device 0, EXCEPTION asserted"** — the reset defaults selection, which this core had backwards. `RDY-` (38) lists seven meanings and `EXC-` (40) obliges the host to "issue STATUS COMMAND and perform a STATUS INPUT to determine cause", which is `ap_sc499`'s READY/EXCEPTION exclusivity from the drive's end |
| 9 | §3.6.1, READ STATUS timing | **CORRECTED 2026-09-09: this is the per-byte handshake, and it was recorded as a confirmation** | The row below called the page a confirmation of one constant and listed the other numbers as trailing detail. **The 22 events are the whole READ STATUS exchange, and eleven of them are the per-byte handshake this core did not have.** T7 `CONTROLLER RESETS READY`, 20 < T5→T7 < 100 µs, is the device answering the host's release of REQUEST; T8 turns the bus; T9 puts the first byte up; T10 raises READY; **T11 `HOST SETS REQUEST` is how a byte is taken** and T12 drops READY within 1 µs; T14 releases REQUEST and T16 brings the next byte and READY with it; T21 turns the bus back and T22 comes ready for the next command. `ap_sc499` modelled T3 and T4 and stopped, so READY rose once and never fell, and `ap_tape` advanced the block on the host's *read* rather than on its REQUEST. Both fixed; `FINDINGS.md` C264 |
| | *original row, kept because it is what the code was written against* | *confirms a modelled constant* | *22 numbered events. **T3→T4 > 10 µs, "controller resets EXCEPTION" → "controller sets READY"** — the same figure as `AP_SC499_T_EXCEPTION_TO_READY`, which came from `[SC499]` Figure 1-8. Also T2→T4 > 20 µs with **500 µs nominal**, 20 < T5→T7 < 100 µs, T11→T12 < 1 µs* |
| 10 | §3.6.2, RESET timing | **the wrong end of the wire, and that is the finding** | T1→T2 < 1 µs disable ACK, T1→T3 < 1 µs disable READY, **T1→T4 < 3 µs assert EXCEPTION**, T1→T5 < 3 µs disable DIRC, T1→T6 > 25 µs host disables RESET — the last being `AP_SC499_T_RESET_MIN_HOLD`. The 3 µs does **not** close `AP_SC499_T_RESET_TO_EXCEPTION`: that constant is the host side of the SC-499, where §1.8.1's power-on confidence test reports "within five seconds". A card that merely passed EXC- through would make that ceiling meaningless |
| 11–12 | §3.6.3 SELECT, §3.6.4 BOT/initialise/erase timing | **CORRECTED 2026-09-09: `none` was wrong — this is the plainest statement of the four-edge command handshake** | §3.6.3's eight events are one SELECT, and READY moves **four** times: T3 down when REQUEST rises (< 1 µs), T4 up when the command is done (> 50 µs, 500 nominal), **T7 down when the host releases REQUEST** (20 < T5→T7 < 100 µs), T8 up again for the next command (> 20 µs). The row below called that "the trailing READY pulse" and recorded the section as yielding nothing; the trailing pulse *is* the half of the interlock this core lacked, and `[SC499]` Figure 1-26's driver waits for its falling edge. §3.6.4 still yields nothing beyond the unbounded motion gap. `FINDINGS.md` C264 |
| | *original row* | *`none`* | *Both 8 events, both with T3→T4 nominal 500 µs and 20 < T→T < 100 µs on the trailing READY pulse. §3.6.4 draws the tape motion as an unbounded gap, which is why `AP_SC499_T_COMMAND_EXECUTION` is bounded from `[SC499]` and not from here* |
| 13–14 | §3.6.5 WRITE DATA, §3.6.6 READ DATA timing | `none` **(the byte handshake, which lives a layer down)** | 40 and 39 events, landscape. T10→T11 > 40 **nanoseconds**, 0.5 < T11→T13 < 100 µs. Write ends with "controller will automatically write file mark and rewind to hub (mechanical delay)"; read ends with the controller setting EXCEPTION at a filemark and the note "system must issue read status command". This is the XFER/ACK byte protocol, which `ap_sc499` models from `[SC499]`'s figures rather than from here |
| 15–16 | §3.6.7 WFM, §3.6.8 RFM timing | **confirms the WFM refusal's shape** | "CONTROLLER WRITES **INTERNALLY GENERATED** FILE MARK ON TAPE" — the host never supplies file-mark content, so refusing WFM for want of a place to put one is refusing the whole command rather than half of it. RFM "reads data blocks until file mark block found", then EXCEPTION |
| 17–19 | §4.0, §4.1 command summary | **the drive mask, and the whole opcode space** | "All device commands are single byte ... Devices shall implement all standard (S) commands ... **All unimplemented commands shall return illegal command status**." Then the space: `0000 0001/0010/0100/1000` SELECT DRIVE 1-4, `0001 DRIVE` the same with lock, `0010 0001` BOT, `0010 0010` ERASE, `0010 0100` INITIALIZE CARTRIDGE, `0100 0000` WRITE, `0110 0000` WFM, `1000 0000` READ, `1010 0000` RFM, `1100 0000` READ STATUS. Every nibble the standard does not name is `V(n)`, vendor unique — including the multi-drive SELECTs that §5.2 makes illegal |
| 19–21 | §4.2, the ten standard commands | **`RESET` defaults to drive 0, said twice** | §4.2.1: "When the power-on reset times out or when the reset pulse terminates, the device initializes operating parameters and **defaults to drive 0 for subsequent commands**." §4.2.2 titles SELECT `(0000 DRIVE)`. §4.2.5 INITIALIZATION "moves the tape ... to BOT, then to EOT and then back to BOT"; §4.2.6 ERASE "also fulfills the requirements of initialization". §4.2.7 and §4.2.8: a WRITE or READ "following cartridge insertion or RESET shall commence at BOT", which load and reset both satisfy by zeroing the position, and both terminate "by deactivating ONLINE" |
| 21–23 | §4.3, the twenty-three optional commands | `none` **(none implemented, and none should be)** | SELECT AUTO CARTRIDGE INITIALIZATION, WRITE WITHOUT UNDERRUNS, ENTER 6 BYTE PARAMETER BLOCK, WRITE/READ N FILE MARKS (`0111 NNNN`/`1011 NNNN`), SPACE FORWARD/REVERSE, READ REVERSE, SEEK END OF RECORDED DATA, the reduced-track-density variants of six of them, and the file-mark reverses. All optional; an SC-499 that does not implement one must answer `ILL`, which is what this core does |
| 24 | §4.3.19–4.3.22, extended status 1–3 and self test | `none` **(read out of order, en route to §5)** | READ EXTENDED STATUS 1 (`1100 0001`) transfers status bytes 6–11, tabulated per command: good-block counts for WRITE and READ, last file mark number for WFM and RFM, remaining data blocks in buffer for WRITE. RUN SELF TEST 1 (`1100 0010`) forbids writing in the recording area and 2 (`1100 1010`) permits it; the result code is vendor-unique except `0001 0001`, "selftest OK", and `0000 0000`, "may not have been performed" |
| 25 | §4.3.23, §5.0, §5.1, §5.2 opening | **confirms every bit name and position** | READ EXTENDED STATUS 3 (`1110 0000`) transfers 64 vendor-unique bytes. §5.1's summary matches `ap_qic.h` name for name and position for position, including the two that are **`RES - Reserved`**: byte 1 bit 2 "reserved for bus parity error" and bit 1 "reserved for end of recorded media". Reserved is a stronger statement than "a fault this core cannot produce" — no drive sets them. Bytes 2–3 `DEC`, bytes 4–5 `URC`, as modelled |
| 26 | §5.2, status byte 1 | **`NDT`, and `ILL`'s six causes** | `POR` reset by a Read Status Sequence; `BOM` "set whenever the cartridge is logically at beginning of tape, track 0", the **only** bit in the byte that neither sets EXCEPTION nor is reset by a status read — this core derives it from the position, which is that behaviour. `MBD` needs a retry count. **`NDT`** is the defect: "set when an unrecoverable data error occurs due to lack of recorded data". `ILL`'s six causes are the list `ap_qic.h` already carries |
| 27 | §5.2, status byte 0 | **the two counters must be cleared** | `FIL`, `BNL` ("Block in error Not Located"), `UDA`, `EOM` — the last "will not be reset by a Read Status Sequence", which this core gets right by deriving it from the position. `WRP` and `CNI` are operator-correctable conditions, not latches, which is what made `WRP` implementable earlier. **`USL` is not "no SELECT yet"**: "set if the selected drive is not physically connected or is not receiving power" — see the open question below. And the two trailing paragraphs, each ending "These bytes shall be cleared by a Read Status Sequence" |
| 28 | §5.3 exception summary, §5.4 items 1–6 | **the table Domain/OS decodes** | Fourteen rows of byte pairs. Rows 1–14 line up one for one with `002398-04` p. 4-14's module `28` codes: "No cartridge" ↔ `(00280010)`, "No drive" ↔ `(00280011)`, "Write Protected" ↔ `(00280012)`, "End of Media" ↔ `(00280013)`, "Read or Write abort" ↔ `(00280014)`, and so on through "Marginal block detected" ↔ `(0028001C)`. Row 8 is what fixed `ap_qic`. Row 1 also settles `USL`: "No cartridge" prints it as a hard `0`, so an empty drive is *not* an unselected one |
| 29 | §5.4 items 7–14 | **`ILL` has a seventh cause here** | Item 8: "READ ERROR, NO DATA - No recorded data found on tape. CONTINUABLE." Item 12 lists **seven** ILL causes where §5.2 bit 6 lists six — it adds "Attempt to BOT, INITIALIZE CARTRIDGE, or ERASE simultaneously" and rewords two others. Each row is marked FATAL or CONTINUABLE, a classification this core does not carry |

## What the document leaves open

`USL` is **closed**: §5.2's definition and §5.3 rows 1 and 2 settle it, and the
drive mask made the state reachable. What remains:

- **FATAL vs CONTINUABLE.** §5.4 classifies all fourteen exceptions and neither
  `ap_qic` nor `ap_sc499` carries the distinction. Whether the SC-499 acts on it
  is a question for `[SC499]`, not for this document.
- ~~**The byte handshake of §3.6.5 and §3.6.6.**~~ **CLOSED 2026-09-09, walked
  event by event, and the two cannot disagree because they are different
  wires.** §3.6.6's 39 events and §3.6.5's 40 move each data byte on **XFER and
  ACK** — the controller puts a byte up and sets ACK (T12), the host sets XFER
  to take it (T13), ACK falls 0.5–3 µs later (T15), XFER falls (T17), next byte
  — and the mirror for a write. **Neither line has a bit in the SC-499's host
  registers**: `002398-04` p. 12-5 and `[SC499]` §1.9 give the status register
  `irq rdy exc don dir` and the control register `rst req ien dni`, and there is
  no XFER and no ACK among them. So XFER/ACK is the *card-to-drive* cable, run
  by the card's own sequencer, and what a host sees of a data transfer is DMA —
  which is what the SR10.4 boot firmware uses, one DMAGO per 512-byte block
  (`FINDINGS.md` C265).
  What **is** host-visible in these two figures is modelled: READY marks the
  *block* boundary (§3.6.6 T10/T11 "1ST DATA BLOCK READY", T24 for the next;
  §3.6.5's "READY FOR 1st BLOCK"/"2nd BLOCK"/"NEXT BLOCK"), which is
  `ap_sc499_block_boundary`; DIRC turns at T9 and back at T39 on a read and
  never moves on a write, which is `ap_tape`'s bus direction; and T38
  "CONTROLLER SETS EXCEPTION" at a file mark is the end-of-read condition
  **-- read again from the page image 2026-09-11, and the order is what
  matters.** The last panel's DATA BUS carries `FILEMARK` as a *valid-data*
  segment between two hatched ones, drawn exactly like `LAST BLOCK`, and T38
  fires **after** it. So the controller transfers the mark across the bus and
  *then* excepts. **One byte, not a block**: in the neighbouring panels a bus
  segment is a single byte (`LAST DATA BYTE`, `1ST BYTE NEXT BLOCK`), and one
  byte-handshake (T34-T37) separates `LAST BLOCK` from `FILEMARK`. Reading it
  as a 512-byte transfer is a mistake this project made for an hour on
  2026-09-11 and caught before writing code; MAME's `dack_read` delivers
  exactly one byte of the mark and drops DRQ, and `FINDINGS.md` C266's
  `count 01FE (base 01FF)` is this core doing the same. This core stops the read while the position is still on the
  mark and never transfers it, which makes every such read short by
  construction -- the cause behind both `FINDINGS.md` C266 and the restore's
  `0028001E`. The row above was not wrong; "read ends with ... EXCEPTION" is
  what the figure says, and the load-bearing word was `ends`
  `ap_tape_read` raises when the cartridge is spent.
  **One documented behaviour with no route to it**, recorded rather than
  implemented: §3.6.5 ends "CONTROLLER WILL AUTOMATICALLY WRITE FILE MARK AND
  REWIND TO HUB (MECHANICAL DELAY)", triggered by the host dropping **ONLINE**
  at T138 — and ONLINE is pin 28 of the drive cable with no host register
  either, so nothing a driver writes to this card can reach it.
- **`AP_SC499_T_RESET_TO_EXCEPTION`.** §3.6.2 gives < 3 µs for the *drive*; the
  constant models the *card*, whose self-test `[SC499]` §1.8.1 bounds at five
  seconds and times nowhere. Still PROVISIONAL, and now with the two ends of the
  wire told apart.
