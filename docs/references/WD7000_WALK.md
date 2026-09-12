# `96-000494` Rev. X3 walk — coverage record

*WD7000-ASC Engineering Specification*, Western Digital, 96-000494 Rev. X3,
August 1988. **141 printed pages, 140 in the PDF.**
`docs/references/westernDigital/96-000494X3_WD7000-ASC_Engineering_Spec_Aug88.pdf`.

## Why this document is on the shelf at all

The DS5500's SCSI controller had no name and no address anywhere on the Apollo
shelf: `019411-A00`'s text layer has **zero** occurrences of "SCSI" and its
Figure 1-5 labels a block "Disk or SCSI/Disk Controller" with no part number;
`[GPIO]` Table 3-1 allocates no SCSI address; `019411-A00` Table 2-5 names none.

The machine's own software did. `tools/awd_read.py` extracted
`/install/ri.apollo.os.v.10.4/sau14/scsi14.drvr` off the DN3500 volume, and it
says **"Western Digital WD7000-ASC SCSI Host Adapter"** outright. Its reset
routine writes to `050002` and polls `050000` masked `F0` — ISA `200`, which is
where `[GPIO]` Table 3-1 puts the **Tape Controller** and where `AP_TAPE_ADDR`
already is. Detail in `docs/PROJECT_STATUS.md`, *The DS5500's SCSI controller is
a WD7000-ASC at ISA `200`*.

So this is the part's own manual for a controller this core does not model at
all, which makes it a document to be **derived in full** rather than queried.

## Method

- **Native resolution is 400 ppi**, `pdfimages -list` confirmed, **1-bit
  JBIG2**, with an Adobe Paper Capture OCR text layer of about 260,000
  characters. The OCR is readable for prose and **unusable for the tables**,
  which is most of what this document is: it turns `0` into `O`, `D0` into `DO`
  and `ICMB` into `lCMB`, and every register table here is made of exactly those
  characters. Pages carrying a table or a figure are read as **600-dpi images**.
- Printed page number and PDF page number differ by one: printed *p. n* is PDF
  page *n − 1* (the footer of PDF page 86 reads "87").
- The DS5500 is an **AT-bus** machine and the ASC is an AT card, so the host
  half of this document applies directly. The floppy controller on the same
  board does not: the DS5500's floppy is the OMTI's, at ISA `3F0`.

## Coverage

**IN PROGRESS.** Pages read so far are listed; the rest is owed.

| PDF | Doc | Section | Yield | What it contained |
| --- | --- | --- | --- | --- |
| 86 | 87 | **§7.4.1 ASC I/O Address Space (W3)** | **the address, and why `200` is possible** | "Address lines SA7-3 are selectable, SA9-8 are fixed at a high level ... The ASC is **currently selected to decode 320-323** (SA9-SA0), the LSB 3 bits are used to select onboard ASC registers." So the factory base is `320` and W3 moves it; the Apollo board is at `200`, which the driver's own constants give. **SA9-8 "fixed at a high level" is a claim about the stock board that the Apollo one contradicts** — recorded, not reconciled, and §7.4.1's own jumper table is on the page after |
| 98 | 99 | **Appendix A.1, Table A-1: ASC I/O Port Definition** | **the host register set** | Four addresses. `+0` read **ASC Status** D7-D0, write **Command Register** byte; `+1` read **Host Interrupt Status** byte, write **ASC Interrupt Acknowledge** (a strobe); `+2` read *reserved*, write **Host Control register** D3-D0; `+3` reserved both ways. This is what identifies `scsi14.drvr`'s reset routine: it writes `3, 0, 2, 0` to base+2 and polls base+0 masked `F0` |
| 116 | 117 | **Appendix A.9, Table A-12: I/O Address Space** | **the ASC's own internal map, not the host's** | The 8085-side addresses: `80` SBIC Address reg R/W, `81` SBIC Task File regs R/W, `40` Time On Bus (125 ns per count) W, `41` Time Off Bus W, `42`/`43`/`44` DMA Address high/mid/low W, `45` FIFO Control (D7 Start FIFO Transfer, D6 End if Empty/Full, D5 spare, D4 Transfer Last Byte Left in FIFO, D3 FIFO Reset- written 0 then 1, D2 Set FIFO Direction 1 = ASC to AT, D1 Start Arbitration when Empty/Full, D0 Transfer On an Odd Address), `46` Read or Write FIFO Data Byte R/W, `47` spare. **Not host-visible**: an emulation models the behaviour these produce, not the addresses |
