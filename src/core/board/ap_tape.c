#include "board/ap_tape.h"

#include <string.h>

void ap_tape_init(ap_tape_t *tape) {
  memset(tape, 0, sizeof *tape);
  ap_sc499_reset(&tape->controller);
  /* `[SC499]` §1.8.1's power-on confidence test, which Apollo straps on --
   * `[08845]` Table 2.0's `KK`, "IN = TEST AT POWER-ON OR RESET". The "or
   * reset" half has been in `ap_sc499_write` all along; this is the half a cold
   * start needs, and without it a card that has only been powered on asserts
   * neither READY nor EXCEPTION, which is the state `[SC499]` Figure 1-24's
   * DONE routine loops in for ever. */
  ap_sc499_power_on_test(&tape->controller);
  ap_qic_init(&tape->drive);
  /* The same arming the reset gives, for the same reason: a drive that has just
   * come up is at BOT. The two must not differ. */
  tape->first_block_pending = true;
}

void ap_tape_reset(ap_tape_t *tape) {
  ap_sc499_reset(&tape->controller);
  /* The same test, for the same reason: this is the machine's own reset, which
   * `[SC499]` §1.12 makes RESET DRV, "the power-on reset from the IBM PC power
   * supply", and `[08845]`'s `KK` covers with "or reset". */
  ap_sc499_power_on_test(&tape->controller);
  ap_qic_reset(&tape->drive);
  memset(tape->block, 0, sizeof tape->block);
  tape->offset = 0u;
  tape->block_valid = false;
  memset(tape->status_block, 0, sizeof tape->status_block);
  tape->status_offset = 0u;
  tape->status_valid = false;
  tape->next_byte_at = 0u;
  /* Armed by the reset, which is where the tape is at BOT. */
  tape->first_block_pending = true;

  /* **CLOSED 2026-09-09. The controller does assert EXCEPTION at reset, and
   * the call above is where.** This comment stood as an open question, and the
   * question was well posed -- it refused to infer hardware behaviour from
   * MAME's commented-out `| SC499_STAT_EXC`, which is the right refusal. What
   * it lacked was the page that answers it, and the citation it reasoned from
   * was the wrong one: it argued from **RSTDMA**, which "initialises the DMA
   * sequencer, clears the control register, sets DONE" and says nothing about
   * EXCEPTION, where the event here is a *power-on*.
   *
   * `[SC499]` §1.8.1 covers power-on directly: the confidence test checks
   * microprocessor RAM, the LSI controller, the 16K RAM and the data separator,
   * and reports success "by the assertion of **`EXC-` within five seconds**".
   * `[08845]` Table 2.0's `KK` row says Apollo runs it -- "IN = TEST AT
   * POWER-ON OR RESET", asterisked, against OUT = TEST DISABLED. And
   * `[SC499]` Figure 1-23's RESET routine ends by calling HOST DONE, whose
   * Figure 1-24 loops on READY-or-EXCEPTION for ever, so a card that comes up
   * asserting neither hangs its own driver.
   *
   * The oracle turns out to agree, which is worth recording only because it was
   * the evidence deliberately refused: its commented-out line would have
   * asserted EXC at reset, and the document now says to. The refusal was still
   * correct -- the reason is the document, and the oracle is the fourth
   * source. `FINDINGS.md` C275. */
}

/* Whether the data path needs a block it does not have in hand. Shared by
 * `ensure_block`, which fetches one, and by the DMA request line, which must not
 * ask for a bus cycle to fetch a block that is not there. */
static bool needs_block(const ap_tape_t *tape) {
  return !(tape->block_valid && tape->offset < AP_CT_BLOCK_SIZE);
}

void ap_tape_advance(ap_tape_t *tape, ap_time_t now) {
  /* Nothing here but the controller's own clock. **The bus direction used to be
   * put back here**, on every advance, because `ap_sc499`'s completion cleared
   * it on every command and a command about to deliver needs it -- see
   * `FINDINGS.md` C263. That was the right fact wired in the wrong place: the
   * completion now clears DIRECTION only for the figure that says to (1-9,
   * a command issued while the device still holds the bus), and the delivery
   * that needs the bus takes it where `QIC-02` §3.6.1 puts it, at T8. */
  ap_sc499_advance(&tape->controller, now);

  /* **A READ ends when the tape reaches the end of the file, not when a host
   * asks for a byte it cannot have.** `QIC-02 Rev D` §3.6.6's T38 asserts
   * EXCEPTION at the file mark, and the drive asserts it because the tape
   * passed one.
   *
   * Modelled as a failed read, the ending only happened when something demanded
   * data -- and under DMA a demand costs a bus cycle and delivers a byte. The
   * SR10.4 boot's seventeenth transfer moved one invented `FF` into the host's
   * buffer before the mark stopped it (`dma1 ch1 count 01FE (base 01FF)`), and
   * MD reported `002398-04` p. 4-17's `36`, "bad block transferred", which is
   * exactly what a short block is. A card cannot transfer a byte the drive
   * never sent. `FINDINGS.md` C267.
   *
   * So the ending lands here, where the machine's clock reaches it: the drive
   * latches `FIL` or `NDT`, the controller raises EXCEPTION, and the DMA
   * sequencer has nothing left in flight. */
  if (needs_block(tape) && ap_qic_read_exhausted(&tape->drive)) {
    ap_qic_end_read(&tape->drive);
    ap_sc499_set_exception(&tape->controller, true);
    ap_sc499_dma_ended(&tape->controller);
  }

  /* **And a DMAGO the drive cannot answer ends at once**, which is the same
   * sentence again: DONE is "from DMA logic" and a sequencer with no transfer
   * in front of it has nothing in flight.
   *
   * Without this the ending above is undone by the host's next move. A driver
   * that reads a file by repeating §1.11's steps 2 to 5 issues one DMAGO per
   * block and only discovers the end when one of them comes back short -- so
   * there is always one DMAGO *after* the last block, and it lowers DONE.
   * Nothing would raise it again, and `002398-04` p. 4-17's `FF`, "timeout
   * waiting for controller done", is what a host prints then. */
  if (tape->controller.dma_active && !tape->drive.reading &&
      !tape->drive.writing) {
    ap_sc499_dma_ended(&tape->controller);
  }
}

bool ap_tape_load(ap_tape_t *tape, uint8_t *data, size_t size,
                  ap_qic_cartridge_t cartridge, bool writable) {
  return ap_qic_load(&tape->drive, data, size, cartridge, writable);
}

bool ap_tape_eject(ap_tape_t *tape) {
  ap_qic_eject(&tape->drive);
  if (tape->drive.loaded) {
    /* The soft lock held. Nothing else changes: the block the controller is
     * part-way through still belongs to the cartridge that is still in. */
    return false;
  }
  memset(tape->block, 0, sizeof tape->block);
  tape->offset = 0u;
  tape->block_valid = false;
  memset(tape->status_block, 0, sizeof tape->status_block);
  tape->status_offset = 0u;
  tape->status_valid = false;
  tape->first_block_pending = true;
  return true;
}

/* Fetch the next block if the current one is spent. The drive deals in blocks
 * and the controller in bytes, so the boundary has to live somewhere; putting
 * it here keeps the drive's interface honest about what a tape transfers. */
static bool ensure_block(ap_tape_t *tape) {
  if (!needs_block(tape)) {
    return true;
  }
  /* **The first block is handed over twice**, from the buffer rather than from
   * the tape: the host asks for it twice and this is where a card with a buffer
   * would answer the second time. See `ap_tape_t::first_block_pending` for the
   * measurement, for the second implementation that needed the same thing, and
   * for the fact that neither it nor any document says why. */
  if (tape->first_block_pending && tape->block_valid) {
    tape->first_block_pending = false;
    tape->offset = 0u;
    ap_sc499_block_boundary(&tape->controller);
    return true;
  }
  if (!ap_qic_read_block(&tape->drive, tape->block)) {
    tape->block_valid = false;
    return false;
  }
  tape->offset = 0u;
  tape->block_valid = true;
  /* A new data block has begun, so READY drops and returns when the device is
   * ready for the next -- `[SC499]` §1.13.1 and Figure 1-5's T4/T15. This used
   * to fetch transparently, handing a host an unbroken byte stream across a
   * boundary the hardware marks, so a driver waiting on the edge waited for
   * ever. */
  ap_sc499_block_boundary(&tape->controller);
  return true;
}

bool ap_tape_decode(uint32_t address, unsigned *reg) {
  if ((address & ~(AP_TAPE_RANGE - 1u)) != AP_TAPE_ADDR) {
    return false;
  }
  uint32_t offset = (address - AP_TAPE_ADDR) & 7u;
  /* Four registers in each eight-byte block; the upper four read `FF` in the
   * measured dump and are not the part. */
  if (offset >= AP_SC499_REGISTERS) {
    return false;
  }
  *reg = offset;
  return true;
}

/* The data path both entry points share. `via_dack` tells them apart, and they
 * are different pins on the card: the DMA sequencer reaches this register
 * through `DACK`, the processor through an address. Until 2026-09-09 they were
 * one call and indistinguishable. */
static uint8_t tape_read_impl(ap_tape_t *tape, uint32_t address,
                              bool via_dack) {
  unsigned reg;
  /* **READ STATUS's six bytes come out of the same register a data block
   * does**, and until 2026-09-09 they came out of nowhere.
   *
   * `[SC499]` §1.13.1: after a READ STATUS "the device transfers the standard
   * six bytes to the host". `ap_qic_read_status` composes them and clears the
   * conditions they report -- and its only caller was `qic_suite`, so on a real
   * machine the block was never delivered. The SR10.4 boot cartridge's firmware
   * issues READ STATUS (`C0`) in answer to the power-on exception, read six
   * bytes of something else, and reported `Tape C0  000000  00  C`
   * (`FINDINGS.md` C261).
   *
   * **Reading the register does not advance the block, and this is the
   * correction to what that fix first did.** `QIC-02` §3.6.1 numbers the whole
   * exchange and `[SC499]` Figure 1-25 draws the driver that walks it: `READY?`
   * -> `READ DATA BUS` -> `ASSERT REQ` -> `READY?` -> `DROP REQ` -> `ALL 6
   * BYTES?`. The read is a *sample* of a byte the device is holding on the bus,
   * and it is the host's REQUEST that says the byte has been taken. A host that
   * reads twice reads the same byte twice, which is what a bus does.
   *
   * Taken **before** the data branch, because the two are exclusive: a READ
   * STATUS is not a READ, so `drive.reading` is false throughout and the data
   * branch would decline anyway -- but ordering it first says which of the two
   * a pending status belongs to rather than leaving it to that accident. */
  if (ap_tape_decode(address, &reg) && reg == AP_SC499_DATA &&
      tape->status_valid) {
    if (tape->status_offset >= AP_QIC_STATUS_BYTES) {
      /* T19, "BUS DATA INVALID": the last byte has been taken and the device
       * has stopped driving, but the bus has not been turned round yet -- that
       * is T21, and it waits for the host to release REQUEST. Undriven reads as
       * `FF`, the same answer the board gives for every line nothing holds. */
      return 0xFFu;
    }
    return tape->status_block[tape->status_offset];
  }
  if (ap_tape_decode(address, &reg) && reg == AP_SC499_DATA &&
      tape->drive.reading) {
    /* The data register delivers the drive's bytes -- but only while a READ is
     * actually in progress. The register is the *controller's*; the drive fills
     * it during a transfer and at no other time.
     *
     * Getting that wrong is what the measured dump caught: routing every read
     * of this port to the drive made an idle controller answer `FF` where the
     * real one reads `00`, so the placement dump stopped reproducing. The
     * hardware's own idle value is the check on where the boundary sits.
     *
     * Exception is asserted when the tape runs out, which is how a driver
     * learns it has ended: `[SC499]`'s status carries EXC "from LSI chip", and
     * running off the end of a cartridge is such a condition. */
    /* **Tape data leaves this card through `DACK` and through nothing else.**
     *
     * A *programmed* read of `BASE+0` used to hand over a tape byte here
     * whenever a READ was armed, and that is what desynchronised the stream:
     * measured on the SR10.4 cartridge boot, **94 bytes** went out this way
     * against 35,662,848 through `DACK`, and 94 is exactly the drift that left
     * every block header 77 bytes out of place and made the kernel print
     * `E0007`.
     *
     * **The oracle is what settled it**, the documents having genuinely run
     * out: `[SC499]` Figures 1-12 and 1-14 have the host poll *status* and
     * never data across a transfer, so this driver's pattern is outside the
     * flow the card documents. MAME's `sc499_device` keeps a `m_data` register
     * loaded **only** by the six status-block bytes -- `m_data = m_tape_status
     * >> 8`, and so on -- and its `read_data_port()` returns that register and
     * touches the block index not at all, where `dack_r()` is the one path that
     * advances it. Read, not copied.
     *
     * The status block is delivered above, which is that register's real
     * traffic. What is left here is a read with nothing armed to answer it, and
     * `00` is the measured idle value this port returns -- the same measurement
     * the comment above records against an idle controller reading `FF`.
     *
     * *Two narrower remedies were tried first and both were refuted by the
     * boot*: pacing this path at the drive's byte rate (drift 77 -> 13, then
     * the host stalls) and suppressing it only while `dma_active` (no change at
     * all, which is how the reads were shown to fall *between* transfers). */
    if (!via_dack) {
      return 0x00u;
    }
    if (!ensure_block(tape)) {
      ap_sc499_set_exception(&tape->controller, true);
      /* **And the DMA transfer is over**, which is the other half of the same
       * event and was missing until 2026-09-09.
       *
       * `[SC499]` §1.9 calls DONE "Done, **from DMA logic**" and §1.11 makes
       * DMAGO the start of a transfer, so DONE up is a sequencer with nothing
       * in flight -- and a drive that has stopped feeding it has left it with
       * nothing in flight. The 8237's terminal count is the *other* way a
       * transfer ends, not the only one.
       *
       * It cannot be otherwise: a READ ends at a file mark, a mark falls where
       * the tape's structure puts it, and so the last DMAGO of every file is
       * short. A card that only raised DONE at the host's byte count could
       * never let a host read a file to its end -- and `002398-04` p. 4-17's
       * `FF`, "timeout waiting for controller done", is the code a host prints
       * when it does not. Measured: with the file mark stopping the read at
       * block 16 the transfer stalled one byte into its seventeenth block,
       * `count 01FE (base 01FF)`, with EXCEPTION up and DONE still clear
       * (`FINDINGS.md` C266). */
      ap_sc499_dma_ended(&tape->controller);
      return 0xFFu;
    }
    /* Figures 1-6 and 1-10 both open with "Device Changes DIRECTION": the
     * device takes the bus to deliver data, and holds it until a command makes
     * it hand back -- which is the condition Figure 1-9 exists for. */
    tape->controller.direction = true;
    /* The next byte is one byte-time away, whichever path took this one: the
     * rate belongs to the drive and not to how the host asked. */
    tape->next_byte_at = tape->controller.now + AP_SC499_T_BYTE;
    return tape->block[tape->offset++];
  }
  if (!ap_tape_decode(address, &reg) || !ap_sc499_readable(reg)) {
    /* Nothing drives the bus: either the address is undecoded, or it is one of
     * the two write-only DMA commands. The measured dump reads `FF` for both,
     * so the board supplies the floating value -- the part cannot, because the
     * part is precisely what is not answering. */
    return 0xFFu;
  }
  return ap_sc499_read(&tape->controller, reg);
}

/* The mirror of `ensure_block`. The controller hands over bytes and the drive
 * takes blocks, so the boundary has to live on this side in both directions:
 * bytes accumulate until there are `AP_CT_BLOCK_SIZE` of them and the block
 * then goes to the cartridge.
 *
 * §1.13.1's WRITE entry gives the same granularity its READ entry does -- "The
 * READY line is activated when the device is ready for a data block transfer"
 * -- so the boundary is marked here exactly as the read path marks it, and a
 * driver waiting on the edge between blocks sees one.
 *
 * `block` and `offset` are shared with the read direction, which is safe
 * because the drive is reading or writing and never both: `ap_qic_command`
 * clears one when it arms the other, and a new command resets them anyway.
 *
 * A short final block is **not** written. A `.ct` is a whole number of 512-byte
 * blocks (`ap_ct_open`, finding C24), so there is nowhere to put a partial one,
 * and inventing padding would put bytes on the tape the host never sent. */
static void accept_byte(ap_tape_t *tape, uint8_t value) {
  if (!tape->block_valid) {
    tape->offset = 0u;
    tape->block_valid = true;
  }
  tape->block[tape->offset++] = value;
  if (tape->offset < AP_CT_BLOCK_SIZE) {
    return;
  }
  if (!ap_qic_write_block(&tape->drive, tape->block)) {
    /* A read-only cartridge, or the end of the tape. Either is a condition the
     * status register is the only channel for, exactly as a spent read is. */
    ap_sc499_set_exception(&tape->controller, true);
  }
  tape->offset = 0u;
  tape->block_valid = false;
  ap_sc499_block_boundary(&tape->controller);
}

/* **The host has taken the byte the device was holding**: `QIC-02` §3.6.1's
 * T11, and `[SC499]` Figure 1-25's `ASSERT REQ`.
 *
 * This is the step that makes REQUEST mean two different things depending on
 * what the device is doing. With the bus turned round and a block in flight, a
 * rising REQUEST is an *acknowledge* -- and this core read every rising REQUEST
 * as a command, so the firmware's acknowledge of status byte 0 was executed as
 * a command whose opcode was whatever the data register still held, which also
 * abandoned the block it was acknowledging. */
static void status_byte_taken(ap_tape_t *tape) {
  if (tape->status_offset < AP_QIC_STATUS_BYTES) {
    tape->status_offset++;
  }
  ap_sc499_byte_taken(&tape->controller);
}

/* **The host has released REQUEST**: `QIC-02` §3.6.1 T5/T14/T20, §3.6.3 T5, and
 * `[SC499]` Figure 1-25's and 1-26's `DROP REQ`. Both flow charts wait on the
 * device's answer to it, and until now the device had none.
 *
 * Three things can be waiting on this edge, and the figure says which by where
 * the exchange has got to:
 *
 *   - a command has completed and the drive has a status block armed. §3.6.1
 *     T8/T9/T10: the device turns the bus round, puts the first of the six
 *     bytes up, and asserts READY.
 *   - a block is in flight with bytes left. T15/T16: the next byte goes up and
 *     READY returns.
 *   - the last byte has been taken. T21/T22: the bus goes back to the host's
 *     direction and READY returns, ready for the next command.
 *
 * The plain command -- nothing armed, nothing in flight -- is §3.6.3's T7/T8
 * and needs nothing from the board at all; `ap_sc499_request_released` makes
 * both edges either way. */
static void request_released(ap_tape_t *tape) {
  if (tape->status_valid) {
    if (tape->status_offset >= AP_QIC_STATUS_BYTES) {
      /* T21: "CONTROLLER CHANGES BUS DIRECTION", back towards the host. */
      tape->status_valid = false;
      tape->status_offset = 0u;
      tape->controller.direction = false;
    }
  } else if (tape->drive.status_pending &&
             !ap_sc499_executing(&tape->controller)) {
    /* T8 and T9. The fetch lands here rather than on the host's first read
     * because `ap_qic_read_status` clears `status_pending` and the drive's
     * latched conditions with it -- it is the transfer beginning, which happens
     * once, and not the reading of a byte, which may happen many times. */
    if (ap_qic_read_status(&tape->drive, tape->status_block)) {
      tape->status_valid = true;
      tape->status_offset = 0u;
      tape->controller.direction = true;
    }
  }
  ap_sc499_request_released(&tape->controller);
}

/* One command transfer's effect, whichever order the host used to start it.
 *
 * A command the drive refuses raises Exception instead of failing silently --
 * the status register is the only channel the controller has for saying no. */
static void issue_command(ap_tape_t *tape, uint8_t command) {
  if (!ap_qic_command(&tape->drive, command)) {
    ap_sc499_set_exception(&tape->controller, true);
    return;
  }
  /* Whichever of the three figures applies, its effects are the device's to
   * apply, not the board's. */
  ap_sc499_command_accepted(&tape->controller);
  /* A new command invalidates whatever block was part-read, and whatever status
   * block was part-delivered. */
  tape->block_valid = false;
  tape->offset = 0u;
  tape->status_valid = false;
  tape->status_offset = 0u;
  /* **Including the drive's arming**, which is the other half of the same
   * abandonment. `ap_qic_command` sets `status_pending` for a READ STATUS and
   * no command clears it, so a host that armed one and then issued something
   * else would have the *next* release of REQUEST open a delivery for a block
   * nobody asked for -- and every REQUEST after that read as a byte
   * acknowledge rather than a command, which is the failure `FINDINGS.md` C264
   * fixed coming back by another door.
   *
   * No figure describes a host abandoning a status sequence; `[SC499]` Figure
   * 1-25 always takes all six bytes. So this is a choice among undefined
   * behaviours, taken because the board already abandons its own half here and
   * two halves that disagree are worse than either answer. */
  if (command != AP_QIC_CMD_READ_STATUS) {
    tape->drive.status_pending = false;
  }
  /* A command that puts the tape back at BOT spends the duplicate rather than
   * re-arming it: what is doubled is the first block after a *reset*, not the
   * first block after every rewind. See `ap_tape_t::first_block_pending`. */
  if (command == AP_QIC_CMD_BOT || command == AP_QIC_CMD_RETENSION) {
    tape->first_block_pending = false;
  }
}

void ap_tape_write(ap_tape_t *tape, uint32_t address, uint8_t value) {
  unsigned reg;
  if (!ap_tape_decode(address, &reg)) {
    return;
  }
  if (reg == AP_SC499_DATA && !tape->status_valid &&
      (tape->controller.control & AP_SC499_CTL_REQUEST) != 0u) {
    /* Control bit 6 is "Request to LSI chip", so a data-register write with it
     * already set is a command rather than data -- unless the device is holding
     * the bus for a block it is delivering, in which case REQUEST is the
     * acknowledge of a byte and the host has no business writing here at all. */
    issue_command(tape, value);
    return;
  }
  /* **And the other order, which is the one the firmware uses.**
   *
   * `[SC499]` §1.13.2's figures number the steps, and every one of the three
   * command transfers opens the same way -- Figure 1-8, the exception entry
   * this machine's boot takes:
   *
   *     T1 - Bus Data Valid
   *     T2 - Controller Asserts REQUEST     0 us. < T1 -> T2
   *     T3 - Device Deasserts EXCEPTION
   *
   * So the byte is on the bus **before** REQUEST rises, and requiring REQUEST
   * first meant the command was stored in the controller's data register and
   * executed by nothing. Measured on the SR10.4 boot cartridge: the firmware
   * writes `C0` (READ STATUS) to the data register at one instruction and `40`
   * (REQUEST) to the control register at the next, then reads the data register
   * thirteen times and gets its own `C0` back -- which is the `Tape C0` MD
   * reports (`FINDINGS.md` C262).
   *
   * Both orders are accepted rather than one replaced: T1 before T2 is what the
   * figures give, and a host that raised REQUEST first would still be issuing a
   * command by the branch above. */
  if (reg == AP_SC499_CONTROL_STATUS) {
    const bool was_requesting =
        (tape->controller.control & AP_SC499_CTL_REQUEST) != 0u;
    /* A controller reset abandons whatever was in flight. `ap_sc499_write` does
     * the controller's half; the block being delivered is the board's, and
     * leaving it standing would make the next rising REQUEST an acknowledge of
     * a transfer the host has just thrown away. */
    if ((value & AP_SC499_CTL_RESET) != 0u) {
      tape->status_valid = false;
      tape->status_offset = 0u;
      tape->block_valid = false;
      tape->offset = 0u;
      /* **And the drive with it.** `[SC499]` §1.12 lists the conditions that
       * reset the controller's microprocessor -- the two supply rails, and "c.
       * RSTSAC is set" -- and then says outright:
       *
       *     NOTE
       *     Microprocessor RESET will also cause a tape drive reset.
       *
       * This core reset the *controller* alone, so a host that pulsed RSTSAC
       * got a drive left exactly where it was: mid-tape, holding no power-on
       * condition, with its selection and lock untouched. Measured on the
       * SR10.4 cartridge boot, where Domain/OS resets the card and then reports
       * `bad rewind` -- the report showed the drive at block 98,263 with its
       * exception word `0000`, when a just-reset drive owes `POR` and `BOM` and
       * sits at load point (`FINDINGS.md` C270).
       *
       * `ap_qic_reset` is what §4.2.1's "the device initializes operating
       * parameters and defaults to drive 0" already means, so the fix is to
       * call it rather than to write a second one. */
      ap_qic_reset(&tape->drive);
      tape->first_block_pending = true;
      tape->next_byte_at = 0u;
    }
    ap_sc499_write(&tape->controller, reg, value);
    const bool now_requesting =
        (tape->controller.control & AP_SC499_CTL_REQUEST) != 0u;
    if (!was_requesting && now_requesting) {
      if (tape->status_valid) {
        /* T11: the host has taken the byte on the bus. */
        status_byte_taken(tape);
      } else {
        /* T2, with the byte T1 left in the data register. */
        issue_command(tape, tape->controller.data);
      }
    } else if (was_requesting && !now_requesting) {
      request_released(tape);
    }
    return;
  }
  /* Data with a WRITE armed is tape data, and the *tape* takes it -- the mirror
   * of the read at `AP_SC499_DATA`, which the controller does not see either.
   * Without this the drive was armed by `09 WRITE`, the bytes went into the
   * controller's data register, and `ap_qic_write_block` was reached by nothing:
   * a host could write a whole cartridge and none of it arrived. */
  if (reg == AP_SC499_DATA && tape->drive.writing) {
    accept_byte(tape, value);
    return;
  }
  ap_sc499_write(&tape->controller, reg, value);
}

bool ap_tape_irq(const ap_tape_t *tape) { return ap_sc499_irq(&tape->controller); }

bool ap_tape_dma_request(const ap_tape_t *tape) {
  /* Only while a read is actually in progress, which is the same boundary the
   * data register keeps: the register is the *controller's* and the drive fills
   * it during a transfer and at no other time. An idle controller that asked for
   * DMA cycles would have the channel run away with the bus. */
  if (!tape->drive.reading) {
    return false;
  }
  /* **Not gated on `dma_active`**, which is the card's DMAGO latch, and the
   * question is worth answering rather than leaving to look like an oversight.
   * `[SC499]` §1.11's five-step sequence issues the transfer command *first*
   * (step 1) and writes DMAGO third, and it tells the host to "set up the 8237
   * DMA controller's register (but leave the mask bit set)" in between --
   * clearing the mask only at step 4. A host is told to hold the channel masked
   * across that window precisely because the card is already asking, so gating
   * the request on DMAGO would make step 2's instruction pointless. */
  /* Bytes left in the block in hand, or another block to fetch. The request is
   * a level and stays up across the whole of it.
   *
   * **And it goes down at the end of the file, before the cycle rather than
   * after it.** A DRQ the drive cannot answer buys a bus cycle whose only
   * product is an invented byte in the host's buffer; the drive knows it has
   * nothing before the sequencer asks, so it says so. */
  /* **And not before the drive has the byte.** `008778-03` Table 9-1 gives the
   * drive 90,000 bytes a second, so a byte is `AP_SC499_T_BYTE` -- 11.1 us --
   * and this line used to be a level held for a whole block: the arbiter took
   * 512 bytes in 20 us with the processor stalled throughout.
   *
   * `FINDINGS.md` C268 measured what that cost. The SR10.4 boot firmware writes
   * DMAGO, then the translation-map entry six instructions later, then the 8237
   * address forty-six instructions later -- an order a drive 11.1 us from its
   * first byte can afford. Against one that had already delivered all 512,
   * every block was placed through the *previous* block's setup, two of the
   * sixteen collided, and the one overwritten was block 0, which carries the
   * boot header the firmware then reported it could not find.
   *
   * This needed `ap_machine_tick`'s stall loop to advance the board, which it
   * did not: a paced line against a frozen clock delivers one byte and spins to
   * `AP_MACHINE_STALL_LIMIT`. That is fixed and measured behaviour-neutral on
   * the reference boot. */
  if (tape->controller.now < tape->next_byte_at) {
    return false;
  }
  if (!needs_block(tape)) {
    return true;
  }
  return !ap_qic_read_exhausted(&tape->drive);
}

uint8_t ap_tape_read(ap_tape_t *tape, uint32_t address) {
  return tape_read_impl(tape, address, false);
}

uint8_t ap_tape_dma_read(ap_tape_t *tape) {
  /* The data register, reached through `DACK` instead of through an address --
   * which is why this defers to the same path rather than reaching into the
   * block itself. Anything the programmed read does about running off the end
   * of the cartridge, this does too. */
  return tape_read_impl(tape, AP_TAPE_ADDR + AP_SC499_DATA, true);
}

void ap_tape_dma_ended(ap_tape_t *tape) {
  ap_sc499_dma_ended(&tape->controller);
}

void ap_tape_dma_write(ap_tape_t *tape, uint8_t value) {
  ap_tape_write(tape, AP_TAPE_ADDR + AP_SC499_DATA, value);
}
