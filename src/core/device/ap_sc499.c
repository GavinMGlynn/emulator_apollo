#include "device/ap_sc499.h"

#include <string.h>

bool ap_sc499_readable(unsigned reg) {
  unsigned r = reg & (AP_SC499_REGISTERS - 1u);
  return r == AP_SC499_DATA || r == AP_SC499_CONTROL_STATUS;
}

void ap_sc499_reset(ap_sc499_t *tape) {
  memset(tape, 0, sizeof *tape);
  /* `[SC499]` on RSTDMA, which "performs the same functions" as power-on reset:
   * it "initializes the DMA sequencer, clears all Control Register bits to 0,
   * and sets DONE to 1". */
  /* And Ready, which is what the oracle's idle controller was measured
   * asserting -- status `40` at reset.
   *
   * DONE is left *clear*, against the guide, which says RSTDMA "sets DONE to 1"
   * and that power-on reset performs the same functions. The measurement is
   * unambiguous -- `40` has bit 4 clear -- and is preferred here because the
   * guide's scan lost the status register's bit numbers entirely, so "DONE" may
   * simply not be the bit this core calls it. Recorded rather than reconciled:
   * a status read after a real transfer would settle which. */
  tape->ready = true;
}

/* The interrupt flag is *derived*, not latched: "Interrupt Request Flag. ORing
 * of RDY AND EXC, and DONE if DNIEN is set."
 *
 * That sentence is ambiguous in English -- "RDY AND EXC" reads equally as a list
 * of two sources or as a conjunction -- and the measurement settles it. The
 * oracle's controller reads `40` at reset: Ready set, and the flag at bit 7
 * *clear*. A disjunction would have made the flag follow Ready and set bit 7
 * too. So it is a conjunction:
 *
 *     IRQ = (RDY AND EXC) OR (DONE AND DNIEN)
 *
 * Read as a list it would have interrupted on every idle controller, which is
 * the kind of wrong that looks like a working driver until the interrupt storm
 * is traced. */
static bool interrupt_flag(const ap_sc499_t *tape) {
  if (tape->ready && tape->exception) {
    return true;
  }
  return tape->done && (tape->control & AP_SC499_CTL_DNIEN) != 0u;
}

bool ap_sc499_irq(const ap_sc499_t *tape) {
  /* "The IRQ line is tri-stated when IEN is cleared. This allows other IBM PC
   * options the use of that interrupt line when the tape controller is not
   * using it." So a masked controller is absent from the line rather than
   * holding it inactive -- which is why the guide warns that the 8259 "should be
   * programmed to respond to the tape controller's IRQ only after IRQ has been
   * enabled by setting IEN". */
  if ((tape->control & AP_SC499_CTL_IEN) == 0u) {
    return false;
  }
  return interrupt_flag(tape);
}

uint8_t ap_sc499_read(ap_sc499_t *tape, unsigned reg) {
  switch ((ap_sc499_reg_t)(reg & (AP_SC499_REGISTERS - 1u))) {
  case AP_SC499_DATA:
    return tape->data;
  case AP_SC499_CONTROL_STATUS: {
    uint8_t status = 0u;
    if (interrupt_flag(tape)) {
      status |= AP_SC499_ST_IRQ;
    }
    if (tape->ready) {
      status |= AP_SC499_ST_RDY;
    }
    if (tape->exception) {
      status |= AP_SC499_ST_EXC;
    }
    if (tape->done) {
      status |= AP_SC499_ST_DONE;
    }
    if (tape->direction) {
      status |= AP_SC499_ST_DIR;
    }
    return status;
  }
  case AP_SC499_DMAGO:
  case AP_SC499_RSTDMA:
    /* Write-only command addresses. A read sweep of the real part found these
     * two returning nothing, which is what identified them as write-only before
     * the manual said so. */
    return 0u;
  }
  return 0u;
}

void ap_sc499_write(ap_sc499_t *tape, unsigned reg, uint8_t value) {
  switch ((ap_sc499_reg_t)(reg & (AP_SC499_REGISTERS - 1u))) {
  case AP_SC499_DATA:
    tape->data = value;
    return;
  case AP_SC499_CONTROL_STATUS:
    tape->control = value;
    if ((value & AP_SC499_CTL_RESET) != 0u) {
      /* "Reset controller microprocessor". The control register itself is what
       * carries the bit, so the reset must not clear the byte that requested
       * it -- a driver holding the bit high is holding the part in reset. */
      uint8_t held = value;
      ap_sc499_reset(tape);
      tape->control = held;
    }
    return;
  case AP_SC499_DMAGO:
    /* "Any write to this register will cause DMAGO to be active" -- the value is
     * not a parameter, and a model that stored it would invent a register the
     * part does not have. */
    tape->dma_active = true;
    tape->done = false;
    return;
  case AP_SC499_RSTDMA:
    /* Defined as equal to power-on reset. */
    ap_sc499_reset(tape);
    return;
  }
}
