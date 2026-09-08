#include "board/ap_tape.h"

#include <string.h>

void ap_tape_init(ap_tape_t *tape) {
  memset(tape, 0, sizeof *tape);
  ap_sc499_reset(&tape->controller);
  ap_qic_init(&tape->drive);
}

void ap_tape_reset(ap_tape_t *tape) {
  ap_sc499_reset(&tape->controller);
  ap_qic_reset(&tape->drive);
  memset(tape->block, 0, sizeof tape->block);
  tape->offset = 0u;
  tape->block_valid = false;
  memset(tape->status_block, 0, sizeof tape->status_block);
  tape->status_offset = 0u;
  tape->status_valid = false;

  /* **Open: whether the controller asserts EXCEPTION at reset.** The drive
   * does hold a condition -- `ap_qic_reset` sets "power on/reset occurred",
   * which a READ STATUS reports and clears -- and the oracle's controller comes
   * up with EXC asserted, since `sc499.cpp` sets `m_status = SC499_STAT_RDY`
   * with `| SC499_STAT_EXC` commented out and EXC is asserted *low*, so leaving
   * the term out leaves the bit at zero.
   *
   * It is deliberately **not** modelled here. `[SC499]` describes what RSTDMA
   * does -- initialise the DMA sequencer, clear the control register, set DONE
   * -- and says nothing about EXCEPTION. Raising it on the strength of a
   * commented-out line in the oracle would be inferring hardware behaviour from
   * someone else's source, which is the one route this project does not take.
   * It also has a visible consequence rather than a quiet one: EXC feeds the
   * interrupt flag, so asserting it at reset makes an idle controller report a
   * pending interrupt. Settled by a driver that reads status after a reset. */
}

void ap_tape_advance(ap_tape_t *tape, ap_time_t now) {
  ap_sc499_advance(&tape->controller, now);
  /* **A command that will deliver to the host ends with the bus turned round,
   * and this core turned it back.**
   *
   * `ap_sc499`'s completion deasserts DIRECTION on Figure 1-9's T4, "Device
   * Deasserts DIRECTION, handing the bus back" -- and Figure **1-9** is the
   * command transfer entered *while the device already holds the bus*, whose T4
   * hands it back so that new command can proceed. Applying it to every
   * command's completion takes the bus away from the command that was about to
   * use it.
   *
   * §1.13.2's data-transfer figure has the other order, and DIRECTION is its
   * first step:
   *
   *     T1 - Device Changes Bus DIRECTION
   *     T2 - Bus Data Valid                0 us. < T1 -> T2
   *     T3 - Device Asserts READY          0 us. < T2 -> T3
   *     T4 - Controller Asserts REQUEST    0 us. < T3 -> T4
   *
   * So a READ or a READ STATUS finishes with DIRECTION *asserted*, which is how
   * a host knows it may read. Re-asserted here rather than by suppressing the
   * completion's clear, because the clear is right for the figure it cites and
   * the two conditions are different: one is "a command took the bus back", the
   * other "a command is about to deliver".
   *
   * Measured (`FINDINGS.md` C263): after READ STATUS the SR10.4 boot firmware
   * polls the status register **4,097 times** at one PC -- a 4096-iteration
   * timeout and its exit -- reading `37` every time, which is READY asserted,
   * no exception, DONE set and **DIRECTION clear**. */
  if (tape->drive.status_pending || tape->drive.reading) {
    tape->controller.direction = true;
  }
}

bool ap_tape_load(ap_tape_t *tape, uint8_t *data, size_t size,
                  ap_qic_cartridge_t cartridge, bool writable) {
  return ap_qic_load(&tape->drive, data, size, cartridge, writable);
}

/* Fetch the next block if the current one is spent. The drive deals in blocks
 * and the controller in bytes, so the boundary has to live somewhere; putting
 * it here keeps the drive's interface honest about what a tape transfers. */
static bool ensure_block(ap_tape_t *tape) {
  if (tape->block_valid && tape->offset < AP_CT_BLOCK_SIZE) {
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

uint8_t ap_tape_read(ap_tape_t *tape, uint32_t address) {
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
   * Taken **before** the data branch, because the two are exclusive: a READ
   * STATUS is not a READ, so `drive.reading` is false throughout and the data
   * branch would decline anyway -- but ordering it first says which of the two
   * a pending status belongs to rather than leaving it to that accident.
   *
   * The block is fetched once, on the first byte, because `ap_qic_read_status`
   * clears `status_pending` and the drive's latched conditions with it: calling
   * it per byte would hand out the first byte six times and acknowledge the
   * exception five times over. */
  if (ap_tape_decode(address, &reg) && reg == AP_SC499_DATA &&
      (tape->status_valid || tape->drive.status_pending)) {
    if (!tape->status_valid) {
      if (!ap_qic_read_status(&tape->drive, tape->status_block)) {
        return 0xFFu;
      }
      tape->status_valid = true;
      tape->status_offset = 0u;
    }
    /* Figure 1-6's opening step for any device-to-host transfer. */
    tape->controller.direction = true;
    const uint8_t byte = tape->status_block[tape->status_offset++];
    if (tape->status_offset >= AP_QIC_STATUS_BYTES) {
      tape->status_valid = false;
      tape->status_offset = 0u;
    }
    return byte;
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
    if (!ensure_block(tape)) {
      ap_sc499_set_exception(&tape->controller, true);
      return 0xFFu;
    }
    /* Figures 1-6 and 1-10 both open with "Device Changes DIRECTION": the
     * device takes the bus to deliver data, and holds it until a command makes
     * it hand back -- which is the condition Figure 1-9 exists for. */
    tape->controller.direction = true;
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
}

void ap_tape_write(ap_tape_t *tape, uint32_t address, uint8_t value) {
  unsigned reg;
  if (!ap_tape_decode(address, &reg)) {
    return;
  }
  if (reg == AP_SC499_DATA &&
      (tape->controller.control & AP_SC499_CTL_REQUEST) != 0u) {
    /* Control bit 6 is "Request to LSI chip", so a data-register write with it
     * already set is a command rather than data. */
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
    ap_sc499_write(&tape->controller, reg, value);
    if (!was_requesting &&
        (tape->controller.control & AP_SC499_CTL_REQUEST) != 0u) {
      /* T2, with the byte T1 left in the data register. */
      issue_command(tape, tape->controller.data);
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
  /* Bytes left in the block in hand, or another block to fetch. The request is
   * a level and stays up across the whole of it. */
  return (tape->block_valid && tape->offset < AP_CT_BLOCK_SIZE) ||
         tape->drive.position < ap_ct_blocks(&tape->drive.image);
}

uint8_t ap_tape_dma_read(ap_tape_t *tape) {
  /* The data register, reached through `DACK` instead of through an address --
   * which is why this defers to the same path rather than reaching into the
   * block itself. Anything the programmed read does about running off the end
   * of the cartridge, this does too. */
  return ap_tape_read(tape, AP_TAPE_ADDR + AP_SC499_DATA);
}

void ap_tape_dma_write(ap_tape_t *tape, uint8_t value) {
  ap_tape_write(tape, AP_TAPE_ADDR + AP_SC499_DATA, value);
}
