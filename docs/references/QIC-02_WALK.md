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

**§5 is read end to end — PDF 25–29, all of `5.0` through `5.4`.** Front matter
and both contents pages read (PDF 2–4). Everything else — §1 scope, §2
definitions, §3 interface and its eight timing diagrams, §4's ten standard and
twenty-three optional commands — is **unread**.

**Next unread page: 5.** 8 pages of 29.

## What it has already yielded

Two defects and a correction, all in `ap_qic`, all from §5:

- **`NDT` was defined and never set.** §5.2 byte 1 bit 5 and §5.4 item 8 define
  it as "No recorded data found on tape", which is exactly a read past the last
  block of a `.ct`. Domain/OS spends three status codes on it.
- **`DEC` and `URC` were never cleared.** §5.2 says "These bytes shall be
  cleared by a Read Status Sequence" of each in turn; `ap_qic_read_status`
  cleared the two latches beside them and left the counters standing.
- **`NDT` does not travel alone.** §5.3 row 8 prints it with `UDA` and `BNL`,
  which is why the fix sets three bits and not one — and why a test that reached
  the "End of media" row by reading one block too many was reaching row 9.

Detail, with the reasoning, is in `docs/PROJECT_STATUS.md` under *The tape
drive's status bits, from the standard both its manuals defer to*.

## Coverage

| PDF pages | Section | Yield | Notes |
| --- | --- | --- | --- |
| 1–2 | cover, title | `none` | "PROPOSED 1/4 INCH CARTRIDGE TAPE DRIVE INTELLIGENT INTERFACE STANDARD", September 23, 1982 |
| 3–4 | table of contents | **the shape of what is unread** | §3's six interface sections plus **eight timing diagrams** (§3.6.1–3.6.8: read status, reset, select, BOT/initialise/erase, write data, read data, write-file-mark, read-file-mark) — this core has the command layer and none of the timings. §4.2's **ten** standard commands include `CARTRIDGE INITIALIZATION`, which `ap_qic` does not have under that name. §4.3 lists **twenty-three** optional commands: SPACE FORWARD/REVERSE, READ REVERSE, WRITE N FILE MARKS, ENTER 6 BYTE PARAMETER BLOCK, WRITE WITHOUT UNDERRUNS, SEEK EOD, the reduced-track-density variants, three READ EXTENDED STATUS and two RUN SELF TEST. `ap_qic.h` calls its twelve "the whole command set, no gaps", which is true of `[SC499]`'s list and not of this one |
| 5–23 | §1 scope, §2 definitions, §3 interface, §4 commands | **UNREAD** | |
| 24 | §4.3.19–4.3.22, extended status 1–3 and self test | `none` **(read out of order, en route to §5)** | READ EXTENDED STATUS 1 (`1100 0001`) transfers status bytes 6–11, tabulated per command: good-block counts for WRITE and READ, last file mark number for WFM and RFM, remaining data blocks in buffer for WRITE. RUN SELF TEST 1 (`1100 0010`) forbids writing in the recording area and 2 (`1100 1010`) permits it; the result code is vendor-unique except `0001 0001`, "selftest OK", and `0000 0000`, "may not have been performed" |
| 25 | §4.3.23, §5.0, §5.1, §5.2 opening | **confirms every bit name and position** | READ EXTENDED STATUS 3 (`1110 0000`) transfers 64 vendor-unique bytes. §5.1's summary matches `ap_qic.h` name for name and position for position, including the two that are **`RES - Reserved`**: byte 1 bit 2 "reserved for bus parity error" and bit 1 "reserved for end of recorded media". Reserved is a stronger statement than "a fault this core cannot produce" — no drive sets them. Bytes 2–3 `DEC`, bytes 4–5 `URC`, as modelled |
| 26 | §5.2, status byte 1 | **`NDT`, and `ILL`'s six causes** | `POR` reset by a Read Status Sequence; `BOM` "set whenever the cartridge is logically at beginning of tape, track 0", the **only** bit in the byte that neither sets EXCEPTION nor is reset by a status read — this core derives it from the position, which is that behaviour. `MBD` needs a retry count. **`NDT`** is the defect: "set when an unrecoverable data error occurs due to lack of recorded data". `ILL`'s six causes are the list `ap_qic.h` already carries |
| 27 | §5.2, status byte 0 | **the two counters must be cleared** | `FIL`, `BNL` ("Block in error Not Located"), `UDA`, `EOM` — the last "will not be reset by a Read Status Sequence", which this core gets right by deriving it from the position. `WRP` and `CNI` are operator-correctable conditions, not latches, which is what made `WRP` implementable earlier. **`USL` is not "no SELECT yet"**: "set if the selected drive is not physically connected or is not receiving power" — see the open question below. And the two trailing paragraphs, each ending "These bytes shall be cleared by a Read Status Sequence" |
| 28 | §5.3 exception summary, §5.4 items 1–6 | **the table Domain/OS decodes** | Fourteen rows of byte pairs. Rows 1–14 line up one for one with `002398-04` p. 4-14's module `28` codes: "No cartridge" ↔ `(00280010)`, "No drive" ↔ `(00280011)`, "Write Protected" ↔ `(00280012)`, "End of Media" ↔ `(00280013)`, "Read or Write abort" ↔ `(00280014)`, and so on through "Marginal block detected" ↔ `(0028001C)`. Row 8 is what fixed `ap_qic`. Row 1 also settles `USL`: "No cartridge" prints it as a hard `0`, so an empty drive is *not* an unselected one |
| 29 | §5.4 items 7–14 | **`ILL` has a seventh cause here** | Item 8: "READ ERROR, NO DATA - No recorded data found on tape. CONTINUABLE." Item 12 lists **seven** ILL causes where §5.2 bit 6 lists six — it adds "Attempt to BOT, INITIALIZE CARTRIDGE, or ERASE simultaneously" and rewords two others. Each row is marked FATAL or CONTINUABLE, a classification this core does not carry |

## Open questions this walk has raised

- **`USL`.** `ap_qic` sets it when no SELECT has been issued; §5.2 defines it as
  the selected drive being absent or unpowered, and §5.3 row 1 prints it `0` for
  a present-but-empty drive. The two readings agree on every row this core is
  tested against, because an unselected drive and an absent one are the same
  thing in a one-drive model — but they are not the same sentence. `002398-04`
  p. 12-5's own summary is the third opinion and has not been re-read against
  this.
- **FATAL vs CONTINUABLE.** §5.4 classifies all fourteen exceptions and neither
  `ap_qic` nor `ap_sc499` carries the distinction. Whether the SC-499 acts on it
  is a question for `[SC499]`, not for this document.
- **The eight timing diagrams of §3.6** are the drive-side counterpart of
  `ap_sc499`'s Figures 1-6 to 1-10, and are unread.
