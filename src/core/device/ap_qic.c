#include "device/ap_qic.h"

#include <string.h>

void ap_qic_init(ap_qic_t *qic) {
  /* First use: an empty drive. Separate from the reset because the two differ
   * in exactly one respect -- whether there is media to keep -- and only one of
   * them can be called on memory that has never held a drive. */
  memset(qic, 0, sizeof *qic);
  qic->power_on = true;
  /* `QIC-02 Rev D` §4.2.1: a power-on "initializes operating parameters and
   * defaults to drive 0 for subsequent commands", so a drive that has just come
   * up is selected and not waiting to be. See `ap_qic_t::selected`. */
  qic->selected = true;
  /* The one field the zero is wrong for. `008778-03` Table 8-1's Tape Format
   * jumper ships IN, which is QIC-24; see the header. A `memset` alone would
   * put a first-use drive in QIC-11 and a reset one in QIC-24, which is the
   * kind of difference between two initialisers that this file's reset comment
   * already warns about. */
  qic->q24_format = true;
}

void ap_qic_reset(ap_qic_t *qic) {
  /* A reset does not eject the cartridge -- it is a command to the drive, not to
   * the operator. It does unlock: `[SC499]` §1.13.1 has RESET among the things
   * that unlock, "Execution of the SELECT command or RESET unlocks the
   * cartridge". It does **not** deselect; see below.
   *
   * **Every field is written and none is read**, which is not a style choice.
   * This used to save `image`, `loaded` and `cartridge`, `memset` the struct,
   * and put the three back -- so a reset called on a drive that had never been
   * initialised read uninitialised memory and preserved it, producing a drive
   * that claimed to hold a cartridge made of stack residue. It survived every
   * debug build, where the stack happened to be zero, and failed only at `-O3`
   * in CI. A save-and-restore reset cannot be safe on first use; a reset that
   * assigns everything it does not deliberately keep can be. */
  /* **Not `false`.** §3.5's pin 32 has RESET "cause device initialization to be
   * performed, **default selection to device 0**", and §4.2.1 repeats it of the
   * reset pulse terminating. A drive that came out of reset deselected refused
   * every command until a SELECT arrived; a real one obeys a READ issued
   * straight afterwards. */
  qic->selected = true;
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
  /* And `FIL`, in §5.2's own words for byte 0 bit 0: "The bit is reset by a
   * Read Status Sequence." A mark reported twice would have a host believe it
   * had reached the end of two files. */
  qic->file_mark = false;

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

/* Whether an opcode is a SELECT of either kind, mask and all. `QIC-02 Rev D`
 * §4.2.2 is `0000 DRIVE` and §4.3.1 is `0001 DRIVE`, so the whole of `00` and
 * `1F` is the SELECT space and the nibble below is the drive. */
static bool is_select(uint8_t command, bool *lock) {
  const uint8_t opcode = (uint8_t)(command & AP_QIC_SELECT_OPCODE_MASK);
  if (opcode != AP_QIC_SELECT_OPCODE && opcode != AP_QIC_SELECT_LOCK_OPCODE) {
    return false;
  }
  *lock = opcode == AP_QIC_SELECT_LOCK_OPCODE;
  return true;
}

/* "The select command selects one of up to four drives" -- so exactly one bit,
 * and §5.2 cause (a) makes "no drives or more than one drive indicated" the
 * illegal-command condition rather than an unimplemented opcode. */
static bool one_drive(uint8_t drives) {
  return drives != 0u && (drives & (uint8_t)(drives - 1u)) == 0u;
}

bool ap_qic_command_known(uint8_t command) {
  bool lock = false;
  if (is_select(command, &lock)) {
    /* Every value of the nibble is a *recognised* SELECT, including the ones
     * that are illegal: a drive that answers `ILL` to `0000 0011` has decoded
     * the command, and one that answers `ILL` to `0101 0101` has not. Only the
     * first is a command this core models. */
    return true;
  }
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
   * That is one of three causes this model can now distinguish -- the other two
   * are the SELECT rules below, reachable since the drive mask was decoded --
   * and it is the one `002398-04` p. 12-5's "Illegal command" row reports. The
   * remaining three, and why each is out of reach, are set out at
   * `ap_qic_t::illegal_command`. */
  if (!ap_qic_command_known(command)) {
    qic->illegal_command = true;
    return false;
  }
  {
    /* The SELECT family, decoded from the nibble rather than matched whole. */
    bool lock = false;
    if (is_select(command, &lock)) {
      const uint8_t drives = (uint8_t)(command & AP_QIC_SELECT_DRIVE_MASK);
      if (!one_drive(drives)) {
        /* §5.2 cause (a), "SELECT command issued with no drives or more than one
         * drive indicated". The selection does not change: the drive rejected
         * the command rather than acting on half of it. */
        qic->illegal_command = true;
        return false;
      }
      const bool ours = drives == AP_QIC_THIS_DRIVE;
      if (qic->selected && !ours && qic->position != 0u) {
        /* §5.2 cause (e): "a drive is deselected by another SELECT command when
         * the cartridge in the currently selected drive is not at beginning of
         * tape, track 0". §5.4 item 12(b) says it the other way round -- "attempt
         * to change drive selection when tape has been moved away from BOT by a
         * read or write operation" -- and both describe this. The tape is left
         * where it is; only the report is added. */
        qic->illegal_command = true;
        return false;
      }
      qic->selected = ours;
      if (ours) {
        /* §4.3.1: "Execution of the SELECT command (0000 drive) or RESET unlocks
         * the cartridge." The lock is on *this* cartridge, so a SELECT naming
         * another drive leaves it alone -- the two are not independent switches,
         * but they are per-drive. */
        qic->soft_lock = lock;
      }
      return true;
    }
  }
  switch ((ap_qic_command_t)command) {
  case AP_QIC_CMD_SELECT:
  case AP_QIC_CMD_SELECT_LOCK:
    /* Handled above, by the mask decode that covers the whole family. Listed so
     * the switch stays exhaustive over `ap_qic_command_t`. */
    return false;
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
     * the image changes; the position does.
     *
     * **This is `QIC-02 Rev D` §4.2.5's `INITIALIZATION` command under
     * `[SC499]`'s name**, which §2's definition settles -- "cartridge
     * initialization - an operation which restores normal tension by wind and
     * rewind of the cartridge" -- and which **Apollo says in its own
     * parenthesis**: `002398-04` p. 10-9 lists the DN5xx tape controller's
     * command `17` as "**Initialize (retension) tape**". Two vendors, three
     * documents, one operation. */
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
    /* **Implemented, and the refusal it replaces was founded on a measurement
     * nobody made.** "A raw block image carries no marks to find" -- it carries
     * three on the boot cartridge and up to forty-one on the others, each one
     * whole block of `DEAFFAED`. `image/ap_ct.h` has the evidence.
     *
     * `QIC-02 Rev D` §4.2.9 and §3.6.8: the drive "reads data blocks until file
     * mark block found", then asserts EXCEPTION. So the command *moves the
     * tape* and delivers nothing, and what it leaves behind is `FIL` and a
     * position past the mark. */
    if (!qic->selected || !qic->loaded) {
      return false;
    }
    qic->reading = false;
    qic->writing = false;
    while (qic->position < ap_ct_blocks(&qic->image)) {
      const bool mark = ap_ct_block_is_file_mark(&qic->image, qic->position);
      qic->position++;
      if (mark) {
        qic->file_mark = true;
        return true;
      }
    }
    /* Off the end without finding one, which §5.4 item 8 is the report for:
     * "READ ERROR, NO DATA - No recorded data found on tape." The command was
     * accepted and executed; what it found is in the status block. */
    qic->no_data = true;
    return true;
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
  case AP_QIC_CMD_WRITE_FILE_MARK: {
    /* **Implemented**, for the same reason READ FILE MARK is: the format has
     * marks, and one is a whole block of `DEAFFAED`. A read-only cartridge
     * refuses, exactly as WRITE does -- the media, not the command, is what
     * cannot take it.
     *
     * §3.6.7's own figure is why the mark is written and nothing else is:
     * "CONTROLLER WRITES **INTERNALLY GENERATED** FILE MARK ON TAPE", so the
     * host supplies no content and there is no data phase to arm. */
    if (!qic->selected || !qic->loaded || !qic->image.writable) {
      return false;
    }
    uint8_t mark[AP_CT_BLOCK_SIZE];
    for (unsigned i = 0; i < AP_CT_BLOCK_SIZE; i++) {
      mark[i] = (uint8_t)(AP_CT_FILE_MARK_WORD >> (8u * (3u - (i & 3u))));
    }
    if (!ap_ct_write_block(&qic->image, qic->position, mark)) {
      return false;
    }
    qic->position++;
    qic->reading = false;
    qic->writing = false;
    return true;
  }
  }
  /* A code outside `[SC499]` §1.13's set entirely. The set has no holes left in
   * it, so reaching here means the host sent something the drive never had. */
  return false;
}

bool ap_qic_read_exhausted(const ap_qic_t *qic) {
  if (!qic->reading || !qic->loaded || !qic->selected) {
    return false;
  }
  return ap_ct_block_is_file_mark(&qic->image, qic->position) ||
         qic->position >= ap_ct_blocks(&qic->image);
}

bool ap_qic_at_file_mark(const ap_qic_t *qic) {
  if (!qic->reading || !qic->loaded || !qic->selected) {
    return false;
  }
  return ap_ct_block_is_file_mark(&qic->image, qic->position);
}

void ap_qic_end_read(ap_qic_t *qic) {
  if (!ap_qic_read_exhausted(qic)) {
    return;
  }
  if (ap_ct_block_is_file_mark(&qic->image, qic->position)) {
    /* Past it, so the *next* READ starts at the next file. That is what makes a
     * multi-file tape readable one file at a time. */
    qic->file_mark = true;
    qic->position++;
  } else {
    /* Off the end. The position does not advance, so a driver that keeps
     * reading keeps failing rather than wrapping to the beginning. */
    qic->no_data = true;
  }
  qic->reading = false;
}

bool ap_qic_read_block(ap_qic_t *qic, uint8_t *out) {
  if (!qic->reading || !qic->loaded || !qic->selected) {
    return false;
  }
  /* **A READ ends at a file mark**, which is `QIC-02 Rev D` §3.6.6's T38,
   * "CONTROLLER SETS EXCEPTION", and §5.2 byte 0 bit 0, "FIL - File Mark
   * Detected bit is set when a File Mark is detected during a Read Data or Read
   * File Mark Sequence".
   *
   * The mark is not delivered as data: it is a structure, not a block of the
   * file, and a host given it would put 512 bytes of `DEAFFAED` into whatever
   * it was loading. The position moves past it so the *next* READ starts at the
   * next file, which is what makes a multi-file tape readable one file at a
   * time.
   *
   * Returning false is what the caller turns into EXCEPTION -- `ap_tape_read`
   * already does that for the end of the tape, and the two are the same signal
   * to a host, distinguished by the status block: `FIL` here, `NDT` there.
   *
   * **This is what the SR10.4 boot cartridge was failing on.** With no marks,
   * a READ never ended: the firmware read the 16-block boot image and ran
   * straight on through the mark at block 16, the ANSI label group, and the
   * whole 104,815-block data file. `FINDINGS.md` C266. */
  /* Both endings go through `ap_qic_end_read`, which is also what the *board*
   * calls when the tape reaches one with no host asking -- see
   * `ap_tape_advance`. The two arms differ only in what they latch: `FIL` for a
   * mark, and §5.4 item 8's `NDT`, "READ ERROR, NO DATA - No recorded data
   * found on tape", for the end of the medium. See `ap_qic_t::no_data` for why
   * that is the one read fault this model can report without inventing one. */
  if (ap_qic_read_exhausted(qic)) {
    ap_qic_end_read(qic);
    return false;
  }
  if (!ap_ct_read_block(&qic->image, qic->position, out)) {
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
  if (!qic->selected) {
    /* **The selected drive is not this one, so it is not there at all.** §5.2
     * defines `USL` as the selected drive being "not physically connected or
     * ... not receiving power", and §5.3 row 2 gives the whole byte for it:
     * "No drive", byte 0 `11110000`. `CNI` and `WRP` are printed as hard ones
     * rather than don't-cares -- an absent drive answers every condition line
     * the same way -- so the row is taken as printed rather than assembled from
     * what *this* drive happens to hold. Row 1, "No cartridge", is the
     * present-but-empty case and prints `USL` as a hard zero, which is what
     * keeps the two rows distinguishable.
     *
     * Nothing could reach this before: the model came up deselected and had no
     * way to select a drive other than its own. Both are fixed above. */
    exs |= AP_QIC_EXS_NO_CARTRIDGE | AP_QIC_EXS_UNSELECTED |
           AP_QIC_EXS_WRITE_PROTECTED;
    goto latches;
  }
  if (!qic->loaded) {
    exs |= AP_QIC_EXS_NO_CARTRIDGE;
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
latches:
  /* The three controller-level latches, which do not belong to the medium and
   * so survive an absent drive: §5.3 rows 12 and 13 print byte 0 as `XXXX0000`
   * beside `ILL` and `POR`, which is the summary saying exactly that. */
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
  if (qic->file_mark) {
    /* §5.3's "Filemark read" row, byte 0 `100X0001` and byte 1 `00000000`: the
     * mark travels **alone**, unlike `NDT`. It is not an error -- it is the
     * structure the tape is made of -- and the row prints no byte-1 bit beside
     * it. */
    exs |= AP_QIC_EXS_FILE_MARK;
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

  /* **Six bytes, most significant first, and this core sent all three fields
   * backwards.** The line this replaces read "Three 16-bit fields, least
   * significant byte first" and cited nothing; it was an assumption about a
   * layout two documents number explicitly.
   *
   * `QIC-02 Rev D` §5.1's STATUS BYTE SUMMARY numbers the bits of BYTE 0 --
   * `ST0 CNI USL WRP EOM UDA BNL FIL` -- and of BYTE 1 -- `ST1 ILL NDT MBD BOM
   * RES RES POR`. `ap_qic_exception_word` composes them as `(byte0 << 8) |
   * byte1`, which is why sending the low half first sent **byte 1 as byte 0**:
   * a host reading the first byte of a just-reset drive got `89` -- `ST1 | BOM
   * | POR` -- and decoded it against byte 0's bits as `ST0 | EOM | FIL`.
   *
   * `002398-04` p. 12-5 says the same of the counters in Apollo's own words,
   * one line each and leaving nothing to infer:
   *
   *     Tape Status Byte 2 = high byte of data error counter
   *     Tape Status Byte 3 = low byte of data error counter
   *     Tape Status Byte 4 = high byte of underrun counter
   *     Tape Status Byte 5 = low byte of underrun counter
   *
   * QIC-02 itself says only that "Bytes 2 and 3 contain the data error counter"
   * without saying which end, so the counters' order rests on Apollo's page
   * alone -- and it is unobservable here either way, since nothing in this model
   * increments either counter. It is corrected with the two that are
   * observable because a layout half right is a layout nobody can check. */
  out[0] = (uint8_t)(exs >> 8);
  out[1] = (uint8_t)(exs & 0xFFu);
  out[2] = (uint8_t)(qic->data_errors >> 8);
  out[3] = (uint8_t)(qic->data_errors & 0xFFu);
  out[4] = (uint8_t)(qic->underruns >> 8);
  out[5] = (uint8_t)(qic->underruns & 0xFFu);

  /* Reading the status is what clears the condition it reports. A drive whose
   * power-on flag survived being read would report a reset that had already
   * been acknowledged, forever. */
  qic->power_on = false;
  /* §5.2 again: `ILL` "is reset by a Read Status Sequence", like every byte-1
   * bit except `BOM`. `NDT` is in that same sentence, and the byte-0 bits it
   * brings with it -- `UDA` and `BNL` -- are each reset by a status read too. */
  qic->illegal_command = false;
  qic->no_data = false;
  /* And `FIL`, in §5.2's own words for byte 0 bit 0: "The bit is reset by a
   * Read Status Sequence." A mark reported twice would have a host believe it
   * had reached the end of two files. */
  qic->file_mark = false;
  /* And so are the two counters. §5.2 says it once for each: of `DEC`, "These
   * bytes shall be cleared by a Read Status Sequence", and of `URC` the same
   * sentence again. They read as zero here either way; clearing them is what
   * keeps that a fact about the model rather than an accident. */
  qic->data_errors = 0u;
  qic->underruns = 0u;
  qic->status_pending = false;
  return true;
}
