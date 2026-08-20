/* QIC-02 cartridge tape drive.
 *
 * The command layer behind the SC-499 controller: `[SC499]` §1.13, "The SC-499
 * controller is designed to accept the QIC-02 command set." The controller's
 * registers are `device/ap_sc499.h`; this is what its commands mean.
 *
 * ## The cartridge type is told, not derived
 *
 * `[SC499]` §1.13: the controller "shall discriminate between DC300XL and DC600A
 * cartridges by measurement of BOT to LOAD POINT distance". It identifies the
 * cartridge from tape *geometry* -- and a `.ct` image is a raw block image with
 * no geometry at all (`FINDINGS.md` C24). There is nothing in the file to
 * measure.
 *
 * So `ap_qic_load` takes the type from its caller. That is a fact about this
 * emulation's inputs rather than about the hardware, and naming it here is
 * cheaper than a format-detection routine failing mysteriously later.
 *
 * ## The two "unknown" opcodes were in the same manual all along
 *
 * ERASE and SELECT Q11 FORMAT were left out with the note that "the scan lost
 * their opcodes to handwritten annotation" (`FINDINGS.md` C25). That was read
 * off §1.13's **summary table**, which is exactly where a previous owner's pen
 * sits: `H'22'` and `H'26'` are struck through.
 *
 * §1.13.1's numbered descriptions, two pages further on, give the same codes in
 * binary and are untouched -- "5) ERASE COMMAND (0010 0010)" and "11) SELECT
 * Q11 FORMAT COMMAND (0010 0110)". The surrounding entries corroborate them:
 * BOT is `0010 0001`, RETENSION `0010 0100` and SELECT Q24 `0010 0111`, which
 * are the three codes this file already had from the same series.
 *
 * So both are known, and both are here. The lesson is the cheap one: a table
 * that cannot be read is not the same as a fact that cannot be recovered, and
 * the second place to look was in the same file.
 *
 * ERASE is *recognised and refused*, for the reason WRITE is -- see below. It
 * is a write to the whole cartridge, and the cartridges this core opens are
 * read-only.
 *
 * ## Writing
 *
 * WRITE places a block, on a cartridge loaded writable. A read-only one refuses
 * -- the distinction `ap_ct_t` now carries -- because a write reported as
 * successful on media that cannot take it would let an installation appear to
 * succeed, which is what refusing outright used to guard against.
 *
 * WRITE FILE MARK and ERASE are still refused, and for a reason that has not
 * changed: a `.ct` is a raw block image with no file marks in it, so there is
 * nothing to write one *into*, and ERASE is a whole-cartridge operation whose
 * effect a distribution image should not silently take.
 */

#ifndef APOLLO_DEVICE_AP_QIC_H
#define APOLLO_DEVICE_AP_QIC_H

#include <stdbool.h>
#include <stdint.h>

#include "image/ap_ct.h"

/* ## SELECT's low nibble is a **drive mask**, not part of the opcode
 *
 * `QIC-02 Rev D` §4.2.2 titles the command `SELECT COMMAND (0000 DRIVE)` and
 * §4.1's summary spells the space out: `0000 0001` SELECT DRIVE 1, `0000 0010`
 * DRIVE 2, `0000 0100` DRIVE 3, `0000 1000` DRIVE 4, and every other value of
 * the nibble -- including `0000 0000` -- marked `V(n)`, vendor unique. `0001
 * DRIVE` is the same four with the cartridge lock. "The select command selects
 * one of up to four drives."
 *
 * `[SC499]` gives only drive 1's two codes, which is why this file had them as
 * whole opcodes and read the rest of the nibble as unimplemented commands. Three
 * consequences, all of them wrong:
 *
 *   - A host selecting drive 2, 3 or 4 got `ILL` for an *unimplemented command*.
 *     It is a perfectly legal SELECT of a drive that is not there, and §5.4 item
 *     2 says the error surfaces later, when a motion command is issued, as "NO
 *     DRIVE".
 *   - `USL` was unreachable. §5.2 defines it as the selected drive being "not
 *     physically connected or ... not receiving power" -- which is precisely
 *     what a one-drive machine is when the host selects drive 2. The bit was
 *     right; nothing could put the model into the state.
 *   - Two of `ILL`'s six causes were recorded as out of reach and are not. See
 *     `ap_qic_t::illegal_command`.
 *
 * `AP_QIC_CMD_SELECT` and `AP_QIC_CMD_SELECT_LOCK` below remain **this drive's**
 * codes -- the SC-499 carries one QIC drive and it answers to `0000 0001`. */
#define AP_QIC_SELECT_OPCODE 0x00u      /* `0000 DRIVE` */
#define AP_QIC_SELECT_LOCK_OPCODE 0x10u /* `0001 DRIVE` */
#define AP_QIC_SELECT_OPCODE_MASK 0xF0u
#define AP_QIC_SELECT_DRIVE_MASK 0x0Fu
/* Bit 0 of the nibble: the one drive an SC-499 has. */
#define AP_QIC_THIS_DRIVE 0x01u

/* `[SC499]` §1.13.1's numbered descriptions, which give every code in binary.
 * The whole command set: eleven commands, no gaps. */
typedef enum {
  AP_QIC_CMD_SELECT = 0x01u,        /* "0000 0001", soft lock off */
  AP_QIC_CMD_SELECT_LOCK = 0x11u,   /* "0001 0001", soft lock on */
  AP_QIC_CMD_BOT = 0x21u,           /* "0010 0001" */
  AP_QIC_CMD_ERASE = 0x22u,         /* "0010 0010" */
  AP_QIC_CMD_RETENSION = 0x24u,     /* "0010 0100" */
  AP_QIC_CMD_SELECT_Q11 = 0x26u,    /* "0010 0110" */
  AP_QIC_CMD_SELECT_Q24 = 0x27u,    /* "0010 0111" */
  AP_QIC_CMD_WRITE = 0x40u,
  AP_QIC_CMD_WRITE_FILE_MARK = 0x60u,
  AP_QIC_CMD_READ = 0x80u,
  AP_QIC_CMD_READ_FILE_MARK = 0xA0u,
  AP_QIC_CMD_READ_STATUS = 0xC0u,
} ap_qic_command_t;

/* The READ STATUS status block.
 *
 * **Six bytes**, and `[SC499]` says so outright: §1.13.1's READ STATUS entry is
 * "The device transfers the standard six bytes to the host." This document was
 * previously recorded as not giving the length -- the search had been aimed at
 * Figure 1-10, which shows the *protocol* and not the payload, and the sentence
 * is on the command's own page instead.
 *
 * The layout is three 16-bit fields, **least significant byte first**, and that
 * comes from two implementations rather than from the conventional QIC-02
 * layout, which `COMPLETION_PLAN.md` explicitly refused as a source:
 *
 *   - Linux `tpqic02.h`: `struct tpstatus { unsigned short exs, dec, urc; }`
 *     with `sizeof(short)==2, LSB first` -- exception flags, data error count
 *     ("nr of blocks rewritten/soft read errors"), underrun count ("nr of times
 *     streaming was interrupted").
 *   - The oracle's `sc499.cpp`, which keeps exactly these three as
 *     `m_tape_status`, `m_data_error_counter` and `m_underrun_counter`.
 *
 * The exception word's bits are the oracle's transcription of the drive's two
 * status bytes, byte 0 in the high half and byte 1 in the low. Only the ones
 * this core can genuinely produce are ever set: everything else would be
 * inventing a fault. */
#define AP_QIC_STATUS_BYTES 6u

/* ## The two bit-7s are **summary bits**, and this file had one of them backwards
 *
 * `002398-04` p. 12-4 draws byte 0's bit 7 as a literal `0` annotated "0 =>
 * Status byte 0" and byte 1's as a literal `1`, "1 => Status byte 1", which
 * reads as two constants identifying the bytes. That reading is wrong, and the
 * same handbook's own STATUS SUMMARY on p. 12-5 disproves it: bit 7 of the
 * "Status 0" column is **1** in every one of the twelve rows where byte 0
 * carries a condition, and `X` in the two where it carries none.
 *
 * `QIC-02 Rev D` §5.2 settles it outright, and it is the standard both Apollo
 * tape documents defer to -- §12.1.10 of Apollo's own `08845` and §1.13.1 of
 * `[SC499]` each say only that the device "transfers the standard six bytes":
 *
 *     BIT 7:  ST0 - Status Byte 0 bit is set if any other bit in
 *             Status Byte 0 is set.
 *     BIT 7:  ST1 - Status byte 1 bit is set if any other bit in
 *             Status byte 1 is set.
 *
 * So both are the same rule: a byte's top bit says "this byte carries
 * something", which is what lets a host see at a glance which half to decode.
 *
 * **`AP_QIC_EXS_BYTE_0` was set when byte 0 was *empty*** -- the exact
 * complement -- so a drive with a condition in byte 0 reported the byte as
 * carrying nothing, and an untroubled drive reported one that carried
 * something. It came from the oracle's transcription; the standard's sentence
 * is unambiguous and the handbook's own summary table agrees with the standard.
 *
 * `AP_QIC_EXS_BYTE_1` was already right, which is why the pair looked
 * deliberate rather than transposed.
 *
 * Using QIC-02 here does not reopen what `COMPLETION_PLAN.md` refused: that
 * refusal was of the standard's *transfer order* for the six-byte block, which
 * this controller delivers as three 16-bit fields LSB-first. What the bits
 * **mean** is the standard's, and §5.1's summary already matches every other
 * constant below name for name. */

/* Status byte 0, the high half of the exception word. */
#define AP_QIC_EXS_BYTE_0 0x8000u /* ST0: set iff any other byte-0 bit is set */
#define AP_QIC_EXS_NO_CARTRIDGE 0x4000u
#define AP_QIC_EXS_UNSELECTED 0x2000u
#define AP_QIC_EXS_WRITE_PROTECTED 0x1000u
#define AP_QIC_EXS_END_OF_MEDIA 0x0800u
#define AP_QIC_EXS_DATA_ERROR 0x0400u   /* unrecoverable */
#define AP_QIC_EXS_NO_BLOCK 0x0200u     /* bad block not located */
#define AP_QIC_EXS_FILE_MARK 0x0100u

/* Status byte 1, the low half. */
#define AP_QIC_EXS_BYTE_1 0x0080u /* ST1: set iff any other byte-1 bit is set */
#define AP_QIC_EXS_ILLEGAL 0x0040u
#define AP_QIC_EXS_NO_DATA 0x0020u
#define AP_QIC_EXS_MARGINAL 0x0010u
#define AP_QIC_EXS_BEGINNING_OF_MEDIA 0x0008u
/* Bits 2 and 1 are **`RES - Reserved`**, and that is the whole of what §5.2 says
 * about them. §5.1's summary gives their intent -- "reserved for bus parity
 * error" and "reserved for end of recorded media" -- but reserved is what they
 * are, so no QIC-02 drive sets either and neither does this one. They are named
 * here because a bit position without a name reads as a hole. */
#define AP_QIC_EXS_PARITY 0x0004u
#define AP_QIC_EXS_END_RECORDED 0x0002u
#define AP_QIC_EXS_POWER_ON 0x0001u /* "power on/reset occurred" */

/* The two cartridges `[SC499]` names. Supplied by the caller. */
typedef enum {
  AP_QIC_CARTRIDGE_NONE = 0,
  AP_QIC_CARTRIDGE_DC300XL,
  AP_QIC_CARTRIDGE_DC600A,
} ap_qic_cartridge_t;

typedef struct {
  ap_ct_t image;
  bool loaded;
  ap_qic_cartridge_t cartridge;

  /* "The drive shall remain selected until changed by another SELECT command or
   * RESET" -- state, not a momentary action.
   *
   * **True means *this* drive is the selected one**, and false now means a
   * different one is -- not that none is. `QIC-02 Rev D` §4.2.1: "When the
   * power-on reset times out or when the reset pulse terminates, the device
   * initializes operating parameters and **defaults to drive 0 for subsequent
   * commands**", which §3.5's pin 32 says again of the RESET line itself
   * ("causes device initialization to be performed, default selection to device
   * 0"). This model came up deselected and refused every command until a SELECT
   * arrived; a real drive obeys a READ issued straight after a reset.
   *
   * So false is now a state a host can *ask* for -- SELECT drive 2 on a machine
   * with one drive -- and it is what makes `USL` reachable. See the drive-mask
   * note above `AP_QIC_CMD_SELECT`. */
  bool selected;
  bool soft_lock;
  /* **QIC-24 out of reset, and that is Apollo's jumper rather than a default.**
   * `[SC499]` §1.13.1 defines the two SELECT FORMAT commands and says nothing
   * about which format a part powers on in; this was `false` -- QIC-11 -- for no
   * reason beyond being the zero. `008778-03` Table 8-1 settles it from the
   * board: the Tape Format jumper is location **CC**, "IN = QIC-24, OUT =
   * QIC-11", and QIC-24 carries the table's "Configuration from vendor" mark.
   * A *Domain System* controller therefore comes out of reset in QIC-24, and a
   * host that never issues a SELECT FORMAT command gets QIC-24 rather than the
   * older format. */
  bool q24_format;

  uint64_t position; /* next block to be read */
  bool reading;      /* a READ command is in progress */
  bool writing;      /* a WRITE command is in progress */

  /* A READ STATUS has been issued and its six bytes not yet taken. */
  bool status_pending;
  /* "Power on/reset occurred", which survives until a status read reports it --
   * that is how a driver distinguishes a drive it has already talked to from
   * one that has just come up. */
  bool power_on;
  /* ## `ill`, and the five causes this model cannot distinguish
   *
   * `QIC-02 Rev D` §5.2 gives status byte 1 bit 6 six causes, and this latch is
   * the sixth: "**Any unimplemented command is issued**". Like `power_on` it
   * survives until a status read reports it -- the standard says "the bit is
   * reset by a Read Status Sequence" of every byte-1 bit except `BOM`.
   *
   * The other five are recorded rather than approximated, because each needs a
   * fact this model does not carry:
   *
   *   a. "SELECT command issued with no drives or more than one drive
   *      indicated" -- **reachable, and now implemented.** This was recorded as
   *      "this drive's SELECT names no drive mask, so there is no count to be
   *      wrong", which was wrong: §4.2.2 titles the command `SELECT COMMAND
   *      (0000 DRIVE)` and the low nibble is a one-hot drive mask. `0000 0000`
   *      indicates no drives and `0000 0011` indicates two.
   *   b. "ONLINE not asserted when a WRITE, WRITE FILE MARK, READ or READ FILE
   *      MARK command is issued" -- ONLINE is a QIC-02 interface line and this
   *      model has the command layer, not the pin. §3.5 pin 28 confirms it is a
   *      pin: "host generated control signal which is activated prior to
   *      transferring a READ or WRITE command and deactivated to terminate that
   *      READ or WRITE command".
   *   c,d. a command other than the transfer's own issued "during the execution
   *      of a Write/Read Data Sequence" -- `reading` and `writing` do say when a
   *      sequence is live, but the standard's list of what is *permitted*
   *      during one is written for a device with WRITE FILE MARK and READ FILE
   *      MARK implemented, and both are refused here for want of file marks in
   *      a `.ct`. Modelling the rule with two of its four allowed commands
   *      missing would report as illegal a sequence the real drive accepts.
   *      And the sequence's *end* is a pin too: §4.2.7 and §4.2.8 both have the
   *      host terminate the transfer "by deactivating ONLINE".
   *   e. "a drive is deselected by another SELECT command when the cartridge
   *      ... is not at beginning of tape" -- **reachable, and now implemented**,
   *      for the same reason as (a). It was recorded as "one drive, so nothing
   *      deselects another"; the drive mask is what deselects it.
   *
   * So the latch is three causes of six rather than one, and the two that
   * remain are pins this model does not have rather than facts it lacks. */
  bool illegal_command;
  /* ## `no_data`: the read that found blank tape
   *
   * `QIC-02 Rev D` §5.2, status byte 1 bit 5: "**NDT** - No Data Detected bit is
   * set when an unrecoverable data error occurs due to lack of recorded data.
   * Absence of recorded data is the failure to detect a data block within a
   * controller time-out. This bit is reset by a Read Status Sequence." §5.4
   * item 8 puts it in one line: "READ ERROR, NO DATA - No recorded data found
   * on tape. CONTINUABLE."
   *
   * That is exactly and only what a read past the last block of a `.ct` is, so
   * unlike `MBD` and `FIL` it needs no fault this model cannot have. It was
   * **defined and never set**: `ap_qic_read_block` returned false and the drive
   * then reported nothing, so a driver reading to end of data was told the
   * transfer failed and given no reason for it.
   *
   * `002398-04` p. 4-14 is what exposed it, and it is the other half of the same
   * fact: Domain/OS module `28`, the cartridge tape manager, spends **three
   * distinct status codes** on this bit -- `(00280017) read no data`,
   * `(00280018) read no data and end of tape`, `(00280019) read no data and load
   * point`. The driver decodes NDT together with `EOM` and `BOM`, which is
   * §5.3's exception summary rows 8, 9 and 10 read from the software side. A
   * driver that spends three codes on a bit is a driver that expects the bit.
   *
   * **It travels with two byte-0 bits, and the summary table is why.** §5.3
   * row 8 gives "Read error, no data" as byte 0 `100X0110` and byte 1
   * `10100000`: `ST0`, `UDA` and `BNL` in the high byte, `ST1` and `NDT` in the
   * low one. §5.2's own wording agrees -- NDT is a kind of *unrecoverable data
   * error* (so `UDA`), and the controller cannot confirm which block was in
   * error because there was none (so `BNL`, "Block in error Not Located").
   * Setting NDT alone would produce a byte pair the standard's table does not
   * contain. Rows 9 and 10 then differ only in `EOM` and `BOM`, which this model
   * already derives from the position, so the three Apollo codes come out of one
   * latch. */
  bool no_data;
  /* Counts the block reader maintains, reported in the status block. Both are
   * genuinely zero here rather than unmodelled: this core rewrites no block and
   * never interrupts streaming, so a nonzero count would be an invention.
   *
   * **They are cleared by a status read, and were not.** §5.2 says it of each in
   * turn -- of `DEC`, "These bytes shall be cleared by a Read Status Sequence",
   * and of `URC` the same sentence again. A counter that accumulated across
   * reads would report every error since power-on to a driver asking about the
   * last operation. It cost nothing to observe here because both are always
   * zero; it would have been a defect the moment either moved. */
  uint16_t data_errors;
  uint16_t underruns;
} ap_qic_t;

/* First use, on memory that has never held a drive: an empty drive, no media.
 * `ap_qic_reset` preserves the cartridge and so cannot be the first call --
 * preserving a field means reading it. */
void ap_qic_init(ap_qic_t *qic);

/* The drive's RESET: deselects, unlocks, rewinds, raises the power-on
 * condition, and **keeps the cartridge**. Requires an initialised drive. */
void ap_qic_reset(ap_qic_t *qic);

/* Load a cartridge. `cartridge` is the type, which the drive cannot derive; see
 * the header. Fails if the image is not a whole number of blocks. */
[[nodiscard]] bool ap_qic_load(ap_qic_t *qic, uint8_t *data, size_t size,
                               ap_qic_cartridge_t cartridge, bool writable);
void ap_qic_eject(ap_qic_t *qic);

/* Issue a command. False for a command this core does not model or does not
 * recognise -- including the two whose opcodes the scan lost, which cannot be
 * recognise. Every code in `[SC499]`'s command set is now recognised; the ones
 * that write -- WRITE, WRITE FILE MARK and ERASE -- are recognised and refused,
 * which is a different answer from "unknown" and the one a driver can act on. */
[[nodiscard]] bool ap_qic_command(ap_qic_t *qic, uint8_t command);

/* Read the next block of the cartridge. Requires a READ command to have been
 * issued and a cartridge to be loaded and selected. */
[[nodiscard]] bool ap_qic_read_block(ap_qic_t *qic, uint8_t *out);

/* Place the next block. Requires a WRITE command and a writable cartridge. */
[[nodiscard]] bool ap_qic_write_block(ap_qic_t *qic, const uint8_t *in);

/* Whether a command code is one this core models at all. */
[[nodiscard]] bool ap_qic_command_known(uint8_t command);

/* The exception word a READ STATUS would report right now. Separate from the
 * block so a test can assert the condition without decoding six bytes, and so
 * the byte order lives in exactly one place. */
[[nodiscard]] uint16_t ap_qic_exception_word(const ap_qic_t *qic);

/* Fill the six-byte status block. Requires a preceding READ STATUS command, as
 * the drive does -- the block is the command's data phase, not a register that
 * can be read whenever.
 *
 * **Reading the status clears the exception condition**, which is the whole
 * point of the command: `[SC499]` §1.12 has the drive report end of media "by
 * means of an EXCEPTION and READ STATUS", and a drive whose exception survived
 * being read would re-report it forever. */
[[nodiscard]] bool ap_qic_read_status(ap_qic_t *qic,
                                      uint8_t out[AP_QIC_STATUS_BYTES]);

#endif /* APOLLO_DEVICE_AP_QIC_H */
