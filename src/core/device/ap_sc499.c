#include "device/ap_sc499.h"

#include <string.h>

ap_sc499_entry_t ap_sc499_command_entry(const ap_sc499_t *tape) {
  /* Exception first: Figure 1-8 is the case where EXCEPTION is up, and
   * §1.13.2's own rule is that READY cannot be, so the two cannot both select. */
  if (tape->exception) {
    return AP_SC499_ENTRY_EXCEPTION;
  }
  /* Then the bus: Figure 1-9 is the device still holding it after a read or a
   * status block, and it must hand it back before the command proceeds. */
  if (tape->direction) {
    return AP_SC499_ENTRY_DIRECTION;
  }
  return AP_SC499_ENTRY_READY;
}

ap_time_t ap_sc499_handshake_duration(ap_sc499_entry_t entry) {
  switch (entry) {
  case AP_SC499_ENTRY_EXCEPTION:
    /* Figure 1-8: EXCEPTION goes down at T3 and READY comes up at T4, "10 us <"
     * later. The only bound here that is a *minimum* -- the device must wait at
     * least that long -- so taking it is taking the fastest legal handshake
     * rather than the slowest, and the direction of the error is opposite to
     * every other figure's. */
    return AP_SC499_T_EXCEPTION_TO_READY;
  case AP_SC499_ENTRY_DIRECTION:
    /* Figure 1-9: the device releases the bus (T3->T4, "< 150 us") and then
     * asserts READY (T4->T6, "< 500 us"). Two intervals in sequence, so the
     * whole is their sum rather than the larger of them. */
    return AP_SC499_T_DIRECTION_RELEASE + AP_SC499_T_DIRECTION_TO_READY;
  case AP_SC499_ENTRY_READY:
    break;
  }
  /* Figure 1-7: T4->T5, the command's own execution, "< 500 ms". */
  return AP_SC499_T_COMMAND_EXECUTION;
}

void ap_sc499_command_accepted(ap_sc499_t *tape) {
  tape->entry = ap_sc499_command_entry(tape);

  /* The device is no longer ready the instant it takes the command. Taken early
   * rather than at the "< 1 us" bound; the header says why. */
  tape->ready = false;
  tape->executing = true;
  tape->ready_at = tape->now + ap_sc499_handshake_duration(tape->entry);
}

bool ap_sc499_executing(const ap_sc499_t *tape) { return tape->executing; }

void ap_sc499_advance(ap_sc499_t *tape, ap_time_t now) {
  if (now > tape->now) {
    tape->now = now;
  }
  if (!tape->executing || tape->now < tape->ready_at) {
    return;
  }

  /* Figure 1-8, T3: "Device Deasserts EXCEPTION" -- so a command is what lifts
   * an exception, and the lifting lands with the completion rather than with
   * the acceptance. A driver that reads status in between sees the exception
   * still up, which is the truth: the device has not finished with it. */
  tape->exception = false;
  /* Figure 1-9, T4: "Device Deasserts DIRECTION", handing the bus back. */
  tape->direction = false;
  /* And in all three figures the device ends by asserting READY: 1-7's T5,
   * 1-8's T4, 1-9's T6. */
  tape->ready = true;
  tape->executing = false;
}

void ap_sc499_set_exception(ap_sc499_t *tape, bool asserted) {
  tape->exception = asserted;
  if (asserted) {
    /* Figure 1-6's rule, enforced where it cannot be forgotten. */
    tape->ready = false;
  }
}

bool ap_sc499_readable(unsigned reg) {
  unsigned r = reg & (AP_SC499_REGISTERS - 1u);
  return r == AP_SC499_DATA || r == AP_SC499_CONTROL_STATUS;
}

void ap_sc499_reset(ap_sc499_t *tape) {
  memset(tape, 0, sizeof *tape);
  /* `[SC499]` on RSTDMA, which "performs the same functions" as power-on reset:
   * it "initializes the DMA sequencer, clears all Control Register bits to 0,
   * and sets DONE to 1". */
  /* **Not ready**, and this is the correction rather than a change of mind.
   * The measured `40` was read here as "the idle controller asserts Ready",
   * which required RDY to be active high. The page image says it is active
   * *low*, and Linux and the oracle both agree -- so `40`, with bit 6 set,
   * means the opposite: a controller that has just been reset is not yet ready.
   * `memset` above already leaves it false; it is stated rather than left
   * implicit because the previous line said the reverse.
   *
   * DONE **is** set, which reverses the other half of the old reading. The
   * guide says RSTDMA "initializes the DMA sequencer, clears all Control
   * Register bits to 0, and sets DONE to 1", and that power-on reset performs
   * the same functions. That was disbelieved because the bit numbers were
   * thought unknown -- "DONE may simply not be the bit this core calls it" --
   * and they are now known from three agreeing sources: bit 4, active high. So
   * the sentence means what it says.
   *
   * The oracle differs: `sc499.cpp` sets `m_status = SC499_STAT_RDY` at reset
   * and nothing else, so it reads `40` where this core now reads `50`. That is
   * a deliberate divergence with the manual cited, not an oversight -- the
   * guide states it twice and MAME's line is a single hand-set assignment with
   * a commented-out neighbour. `PROJECT_STATUS.md` records it as such. */
  tape->done = true;
}

/* The interrupt flag is *derived*, not latched: "Interrupt Request Flag. ORing
 * of RDY AND EXC, and DONE if DNIEN is set."
 *
 * That sentence is ambiguous in English -- "RDY AND EXC" reads equally as a list
 * of two sources or as a conjunction -- and this core read it as a conjunction
 * on the strength of a measurement it was misreading. **It is a list.**
 *
 *     IRQ = RDY OR EXC OR (DONE AND DNIEN)
 *
 * The old argument was: the oracle reads `40` at reset, so Ready is asserted
 * and the flag is clear, so a disjunction would have set bit 7 too. Every step
 * after the first is wrong, because RDY is **active low** -- `40` has bit 6
 * *set*, which means Ready is *not* asserted. A freshly reset controller
 * asserts neither source, so a disjunction is clear at reset exactly as
 * measured. The measurement that was thought to rule the disjunction out is
 * consistent with it.
 *
 * Two sources confirm the list reading directly: the oracle's own comment
 * calls bit 7 the "('or' of rdy and exc)", and its code raises the interrupt
 * when it asserts READY *or* when it asserts EXCEPTION, at separate places and
 * with neither conditioned on the other.
 *
 * The conjunction was not merely a different guess: it made an interrupt
 * impossible to raise in the one state a drive spends its life in. READY
 * asserted with no exception is a completed command, which is precisely when a
 * driver expects to be interrupted, and a conjunction stays silent for it. */
static bool interrupt_flag(const ap_sc499_t *tape) {
  if (tape->ready || tape->exception) {
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
    /* The two active-low bits start set and are *cleared* by their condition,
     * which is the whole reason this is not a chain of five identical ORs. */
    uint8_t status = AP_SC499_ST_ACTIVE_LOW;
    if (interrupt_flag(tape)) {
      status |= AP_SC499_ST_IRQ;
    }
    if (tape->ready) {
      status &= (uint8_t)~AP_SC499_ST_RDY;
    }
    if (tape->exception) {
      status &= (uint8_t)~AP_SC499_ST_EXC;
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
