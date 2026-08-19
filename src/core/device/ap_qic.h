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

/* Status byte 0, the high half of the exception word. */
#define AP_QIC_EXS_BYTE_0 0x8000u    /* "0 => status byte 0" */
#define AP_QIC_EXS_NO_CARTRIDGE 0x4000u
#define AP_QIC_EXS_UNSELECTED 0x2000u
#define AP_QIC_EXS_WRITE_PROTECTED 0x1000u
#define AP_QIC_EXS_END_OF_MEDIA 0x0800u
#define AP_QIC_EXS_DATA_ERROR 0x0400u   /* unrecoverable */
#define AP_QIC_EXS_NO_BLOCK 0x0200u     /* bad block not located */
#define AP_QIC_EXS_FILE_MARK 0x0100u

/* Status byte 1, the low half. */
#define AP_QIC_EXS_BYTE_1 0x0080u /* "1 => status byte 1" */
#define AP_QIC_EXS_ILLEGAL 0x0040u
#define AP_QIC_EXS_NO_DATA 0x0020u
#define AP_QIC_EXS_MARGINAL 0x0010u
#define AP_QIC_EXS_BEGINNING_OF_MEDIA 0x0008u
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
   * RESET" -- state, not a momentary action. */
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
  /* Counts the block reader maintains, reported in the status block. Both are
   * genuinely zero here rather than unmodelled: this core rewrites no block and
   * never interrupts streaming, so a nonzero count would be an invention. */
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
