#include "device/ap_qic.h"

#include <string.h>

void ap_qic_init(ap_qic_t *qic) {
  /* First use: an empty drive. Separate from the reset because the two differ
   * in exactly one respect -- whether there is media to keep -- and only one of
   * them can be called on memory that has never held a drive. */
  memset(qic, 0, sizeof *qic);
  qic->power_on = true;
  /* The one field the zero is wrong for. `008778-03` Table 8-1's Tape Format
   * jumper ships IN, which is QIC-24; see the header. A `memset` alone would
   * put a first-use drive in QIC-11 and a reset one in QIC-24, which is the
   * kind of difference between two initialisers that this file's reset comment
   * already warns about. */
  qic->q24_format = true;
}

void ap_qic_reset(ap_qic_t *qic) {
  /* A reset does not eject the cartridge -- it is a command to the drive, not to
   * the operator. But it does deselect and unlock: `[SC499]` §1.13.1 has RESET
   * among the things that unlock, "Execution of the SELECT command or RESET
   * unlocks the cartridge".
   *
   * **Every field is written and none is read**, which is not a style choice.
   * This used to save `image`, `loaded` and `cartridge`, `memset` the struct,
   * and put the three back -- so a reset called on a drive that had never been
   * initialised read uninitialised memory and preserved it, producing a drive
   * that claimed to hold a cartridge made of stack residue. It survived every
   * debug build, where the stack happened to be zero, and failed only at `-O3`
   * in CI. A save-and-restore reset cannot be safe on first use; a reset that
   * assigns everything it does not deliberately keep can be. */
  qic->selected = false;
  qic->soft_lock = false;
  /* `008778-03` Table 8-1's jumper CC, which is fitted for QIC-24. See the
   * header: this was the zero rather than a documented default. */
  qic->q24_format = true;
  qic->position = 0u;
  qic->reading = false;
  qic->writing = false;
  qic->status_pending = false;
  qic->data_errors = 0u;
  qic->underruns = 0u;
  /* Assigned rather than left standing, per this function's own rule two
   * paragraphs up: a reset reinitialises the controller, and an illegal-command
   * latch that outlived one would report a command the drive no longer
   * remembers being given. */
  qic->illegal_command = false;
  /* Same rule, same reason: `NDT` reports the read that has just failed, and a
   * reset means there is no such read to report. */
  qic->no_data = false;

  /* `SC499_ST1_POR`, "power on/reset occurred". Set by the reset and cleared
   * only by the status read that reports it. */
  qic->power_on = true;
}

bool ap_qic_load(ap_qic_t *qic, uint8_t *data, size_t size,
                 ap_qic_cartridge_t cartridge, bool writable) {
  if (cartridge == AP_QIC_CARTRIDGE_NONE) {
    return false;
  }
  if (!ap_ct_open(&qic->image, data, size, writable)) {
    return false;
  }
  qic->loaded = true;
  qic->cartridge = cartridge;
  qic->position = 0u;
  qic->reading = false;
  qic->writing = false;
  return true;
}

void ap_qic_eject(ap_qic_t *qic) {
  if (qic->soft_lock) {
    /* The soft lock is a lock on the *cartridge*, so it holds against ejection.
     * That is the only thing the lock does that a caller can observe, and a
     * model that ignored it would make the command inert. */
    return;
  }
  memset(&qic->image, 0, sizeof qic->image);
  qic->loaded = false;
  qic->cartridge = AP_QIC_CARTRIDGE_NONE;
  qic->position = 0u;
  qic->reading = false;
  qic->writing = false;
}

bool ap_qic_command_known(uint8_t command) {
  switch ((ap_qic_command_t)command) {
  case AP_QIC_CMD_SELECT:
  case AP_QIC_CMD_SELECT_LOCK:
  case AP_QIC_CMD_BOT:
  case AP_QIC_CMD_RETENSION:
  case AP_QIC_CMD_ERASE:
  case AP_QIC_CMD_SELECT_Q11:
  case AP_QIC_CMD_SELECT_Q24:
  case AP_QIC_CMD_WRITE:
  case AP_QIC_CMD_WRITE_FILE_MARK:
  case AP_QIC_CMD_READ:
  case AP_QIC_CMD_READ_FILE_MARK:
  case AP_QIC_CMD_READ_STATUS:
    return true;
  }
  return false;
}

bool ap_qic_command(ap_qic_t *qic, uint8_t command) {
  /* `QIC-02 Rev D` §5.2, status byte 1 bit 6: "**ILL** - Illegal Command bit is
   * set if any of the following occurs ... **f. Any unimplemented command is
   * issued.** The bit is reset by a Read Status Sequence."
   *
   * That is the one of the standard's six causes this model can distinguish
   * from the others without inventing state, and it is the one `002398-04`
   * p. 12-5's "Illegal command" row reports. The other five, and why each is
   * out of reach, are set out at `ap_qic_t::illegal_command`. */
  if (!ap_qic_command_known(command)) {
    qic->illegal_command = true;
    return false;
  }
  switch ((ap_qic_command_t)command) {
  case AP_QIC_CMD_SELECT:
    /* "The SELECT command selects the tape drive ... Execution of the SELECT
     * command or RESET unlocks the cartridge." So a plain SELECT clears a lock
     * the other variant set -- the two are not independent switches. */
    qic->selected = true;
    qic->soft_lock = false;
    return true;
  case AP_QIC_CMD_SELECT_LOCK:
    qic->selected = true;
    qic->soft_lock = true;
    return true;
  case AP_QIC_CMD_BOT:
    /* "positions the tape in the cartridge in the selected device to BOT". */
    if (!qic->selected) {
      return false;
    }
    qic->position = 0u;
    qic->reading = false;
    return true;
  case AP_QIC_CMD_RETENSION:
    /* Runs the tape end to end and returns it to the beginning. Nothing about
     * the image changes; the position does. */
    if (!qic->selected) {
      return false;
    }
    qic->position = 0u;
    qic->reading = false;
    return true;
  case AP_QIC_CMD_SELECT_Q11:
    /* §1.13.1 item 11: "The SELECT Q11 format command selects the Q11 format as
     * the current format." Item 12 says the same of Q24, so the pair is one
     * switch with two settings rather than two independent flags. */
    if (!qic->selected) {
      return false;
    }
    qic->q24_format = false;
    return true;
  case AP_QIC_CMD_SELECT_Q24:
    if (!qic->selected) {
      return false;
    }
    qic->q24_format = true;
    return true;
  case AP_QIC_CMD_ERASE:
    /* §1.13.1 item 5: "completely erases the tape in the selected drive ...
     * moves the tape to BOT, activates the erase head and moves to EOT".
     *
     * Recognised and refused, exactly as WRITE is. The cartridges this core
     * opens are read-only distribution images, and there is no write-back path;
     * an erase reported as successful would leave a driver believing a tape it
     * is about to write is blank. Refusing is the answer that is true.
     *
     * This is the command whose opcode was recorded as unrecoverable. It was in
     * the same manual two pages further on -- see `ap_qic.h`. */
    return false;
  case AP_QIC_CMD_READ:
    if (!qic->selected || !qic->loaded) {
      return false;
    }
    qic->reading = true;
    return true;
  case AP_QIC_CMD_READ_STATUS:
    /* Always answerable, selected or not: a status read is how a driver finds
     * out that the drive is *not* ready. The command arms the data phase; the
     * six bytes come from `ap_qic_read_status`. */
    qic->status_pending = true;
    return true;
  case AP_QIC_CMD_READ_FILE_MARK:
    /* Recognised, and refused: a file mark is a structure within the tape
     * format, and a raw block image carries no marks to find. Answering
     * "found one" or "reached the end" would both be inventions. */
    return false;
  case AP_QIC_CMD_WRITE:
    /* §1.13.1: "When the WRITE command is issued the device requests and
     * transfers data." A cartridge loaded writable takes it; a read-only one
     * refuses, which is the honest answer and the one a driver can act on. */
    if (!qic->selected || !qic->loaded || !qic->image.writable) {
      return false;
    }
    qic->writing = true;
    qic->reading = false;
    return true;
  case AP_QIC_CMD_WRITE_FILE_MARK:
    /* Still refused, and the reason has not changed: a `.ct` is a raw block
     * image with no file marks in it, so there is nothing to write one into.
     * Answering "written" would be inventing a structure the format lacks. */
    return false;
  }
  /* A code outside `[SC499]` §1.13's set entirely. The set has no holes left in
   * it, so reaching here means the host sent something the drive never had. */
  return false;
}

bool ap_qic_read_block(ap_qic_t *qic, uint8_t *out) {
  if (!qic->reading || !qic->loaded || !qic->selected) {
    return false;
  }
  if (!ap_ct_read_block(&qic->image, qic->position, out)) {
    /* Past the end of the tape. The position does not advance, so a driver that
     * keeps reading keeps failing rather than wrapping to the beginning.
     *
     * And the drive now **says why**. `QIC-02 Rev D` §5.4 item 8: "READ ERROR,
     * NO DATA - No recorded data found on tape." See `ap_qic_t::no_data` for
     * why this is the one read fault the model can report without inventing
     * one, and for the three Domain/OS status codes that decode it. */
    qic->no_data = true;
    return false;
  }
  qic->position++;
  return true;
}

bool ap_qic_write_block(ap_qic_t *qic, const uint8_t *in) {
  /* The mirror of the read above, and gated the same way: a command must have
   * armed it, the drive must be selected and hold media. `ap_ct_write_block`
   * enforces the cartridge's own read-only flag, so a writable *drive* holding
   * a read-only image still refuses. */
  if (!qic->writing || !qic->loaded || !qic->selected) {
    return false;
  }
  if (!ap_ct_write_block(&qic->image, qic->position, in)) {
    return false;
  }
  qic->position++;
  return true;
}

uint16_t ap_qic_exception_word(const ap_qic_t *qic) {
  uint16_t exs = 0u;

  /* Only conditions this core can genuinely be in. What is left out is now a
   * short list rather than "every other flag": `MBD` needs a marginal-block
   * model and `FIL` needs file marks, which a raw `.ct` has neither of, and bits
   * 2 and 1 of byte 1 are reserved in the standard and set by nobody. `UDA` and
   * `BNL` used to be on that list and are not any more -- §5.3's summary makes
   * them part of how a no-data read is reported, which is a condition this model
   * genuinely reaches. */
  if (!qic->loaded) {
    exs |= AP_QIC_EXS_NO_CARTRIDGE;
  }
  if (!qic->selected) {
    exs |= AP_QIC_EXS_UNSELECTED;
  }
  /* **Defined and never set**, until `002398-04` p. 12-5's summary row for
   * "Write protected" was checked against this function and came back `00`.
   *
   * `QIC-02 Rev D` §5.2: "WRP - Write Protected bit is set if the cartridge
   * write protect plug is set in the file protect 'safe' position. Operator
   * must change the write protect plug position before the status bit will
   * reset." So it is a *condition* of the cartridge rather than a latched
   * event, which is exactly what this model can answer -- `ap_ct_write_block`
   * already enforces the same flag, and `WRITE` already refuses on it. The
   * drive knew and would not say. */
  if (qic->loaded && !qic->image.writable) {
    exs |= AP_QIC_EXS_WRITE_PROTECTED;
  }
  if (qic->loaded && qic->position == 0u) {
    /* Beginning of media: the head is before the first block. The oracle sets
     * exactly this on loading a cartridge. */
    exs |= AP_QIC_EXS_BEGINNING_OF_MEDIA;
  }
  if (qic->loaded && qic->position >= ap_ct_blocks(&qic->image)) {
    exs |= AP_QIC_EXS_END_OF_MEDIA;
  }
  if (qic->power_on) {
    exs |= AP_QIC_EXS_POWER_ON;
  }
  if (qic->illegal_command) {
    exs |= AP_QIC_EXS_ILLEGAL;
  }
  if (qic->no_data) {
    /* §5.3 row 8, "Read error, no data": byte 0 `100X0110`, byte 1 `10100000`.
     * `NDT` never travels alone -- it is a species of unrecoverable data error,
     * and the block in error cannot be located because there was no block. Rows
     * 9 and 10 add `EOM` and `BOM`, which the position above has already
     * supplied. */
    exs |= AP_QIC_EXS_NO_DATA | AP_QIC_EXS_DATA_ERROR | AP_QIC_EXS_NO_BLOCK;
  }

  /* The two summary bits, and they follow **one** rule rather than two.
   * `QIC-02 Rev D` §5.2: each byte's bit 7 "is set if any other bit in" that
   * byte "is set". See the header for how this file came to have byte 0's
   * inverted, and for why the handbook's own summary table is what exposed it.
   */
  if ((exs & 0x7F00u) != 0u) {
    exs |= AP_QIC_EXS_BYTE_0;
  }
  if ((exs & 0x007Fu) != 0u) {
    exs |= AP_QIC_EXS_BYTE_1;
  }
  return exs;
}

bool ap_qic_read_status(ap_qic_t *qic, uint8_t out[AP_QIC_STATUS_BYTES]) {
  if (out == nullptr || !qic->status_pending) {
    return false;
  }
  const uint16_t exs = ap_qic_exception_word(qic);

  /* Three 16-bit fields, least significant byte first. */
  out[0] = (uint8_t)(exs & 0xFFu);
  out[1] = (uint8_t)(exs >> 8);
  out[2] = (uint8_t)(qic->data_errors & 0xFFu);
  out[3] = (uint8_t)(qic->data_errors >> 8);
  out[4] = (uint8_t)(qic->underruns & 0xFFu);
  out[5] = (uint8_t)(qic->underruns >> 8);

  /* Reading the status is what clears the condition it reports. A drive whose
   * power-on flag survived being read would report a reset that had already
   * been acknowledged, forever. */
  qic->power_on = false;
  /* §5.2 again: `ILL` "is reset by a Read Status Sequence", like every byte-1
   * bit except `BOM`. `NDT` is in that same sentence, and the byte-0 bits it
   * brings with it -- `UDA` and `BNL` -- are each reset by a status read too. */
  qic->illegal_command = false;
  qic->no_data = false;
  /* And so are the two counters. §5.2 says it once for each: of `DEC`, "These
   * bytes shall be cleared by a Read Status Sequence", and of `URC` the same
   * sentence again. They read as zero here either way; clearing them is what
   * keeps that a fact about the model rather than an accident. */
  qic->data_errors = 0u;
  qic->underruns = 0u;
  qic->status_pending = false;
  return true;
}
