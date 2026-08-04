#include "board/ap_tape.h"

#include <string.h>

void ap_tape_reset(ap_tape_t *tape) {
  ap_sc499_reset(&tape->controller);
  ap_qic_reset(&tape->drive);
  memset(tape->block, 0, sizeof tape->block);
  tape->offset = 0u;
  tape->block_valid = false;

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

bool ap_tape_load(ap_tape_t *tape, const uint8_t *data, size_t size,
                  ap_qic_cartridge_t cartridge) {
  return ap_qic_load(&tape->drive, data, size, cartridge);
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

void ap_tape_write(ap_tape_t *tape, uint32_t address, uint8_t value) {
  unsigned reg;
  if (!ap_tape_decode(address, &reg)) {
    return;
  }
  if (reg == AP_SC499_DATA &&
      (tape->controller.control & AP_SC499_CTL_REQUEST) != 0u) {
    /* Control bit 6 is "Request to LSI chip", so a data-register write with it
     * set is a command rather than data. A command the drive refuses raises
     * Exception instead of failing silently -- the status register is the only
     * channel the controller has for saying no. */
    if (!ap_qic_command(&tape->drive, value)) {
      ap_sc499_set_exception(&tape->controller, true);
    } else {
      /* Whichever of the three figures applies, its effects are the device's
       * to apply, not the board's. */
      ap_sc499_command_accepted(&tape->controller);
      /* A new command invalidates whatever block was part-read. */
      tape->block_valid = false;
      tape->offset = 0u;
    }
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
