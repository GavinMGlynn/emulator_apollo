#include "device/ap_mc6840.h"

#include <string.h>


void ap_mc6840_reset(ap_mc6840_t *ptm) {
  memset(ptm, 0, sizeof *ptm);
  for (unsigned i = 0; i < AP_MC6840_TIMERS; i++) {
    /* §4.1: "If the latches are not written, they default to $FFFF." */
    ptm->timer[i].latch = 0xFFFFu;
    ptm->timer[i].counter = 0xFFFFu;
  }
  /* §4.1: "Upon initialization (power up or hardware reset), bit 0 of control
   * register 2 defaults to zero (control register #3 is selected address
   * zero)." */
  ptm->control2 = 0u;
}

static bool timers_preset(const ap_mc6840_t *ptm) {
  /* §3.6.1, control register 1 bit 0: "0 ALL TIMERS OPERATE, 1 ALL TIMERS
   * PRESET". This is the software reset, and it holds every timer, not just
   * timer 1 -- the bit lives in CR1 but acts on the part. */
  return (ptm->timer[0].control & AP_MC6840_CR1_TIMERS_PRESET) != 0u;
}

ap_mc6840_mode_t ap_mc6840_mode(const ap_mc6840_t *ptm, unsigned index) {
  if (index >= AP_MC6840_TIMERS) {
    return AP_MC6840_MODE_CONTINUOUS;
  }
  uint8_t control = ptm->timer[index].control;
  if ((control & AP_MC6840_CR_MEASUREMENT) != 0u) {
    /* §3.9 and §3.10: bit 4 chooses which quantity is measured. */
    return (control & AP_MC6840_CR_BIT4) != 0u
               ? AP_MC6840_MODE_PULSE_WIDTH_MEASUREMENT
               : AP_MC6840_MODE_PERIOD_MEASUREMENT;
  }
  /* §3.7 and §3.8: bit 5 chooses continuous or single shot. Bit 4 here is a
   * different question entirely -- whether a latch write reinitialises -- and
   * conflating the two is the error the header records. */
  return (control & AP_MC6840_CR_BIT5) != 0u ? AP_MC6840_MODE_SINGLE_SHOT
                                             : AP_MC6840_MODE_CONTINUOUS;
}

bool ap_mc6840_mode_supported(const ap_mc6840_t *ptm, unsigned index) {
  if (index >= AP_MC6840_TIMERS) {
    return false;
  }
  /* All four. The two measurement modes were declined while this board had
   * nothing wired to a gate pin; they are `[6840]` §3.9 and §3.10 and are
   * modelled now, so the answer is no longer about the board. */
  (void)ap_mc6840_mode(ptm, index);
  return true;
}

/* §3.7.1 and §3.7.2: bit 2 chooses whether the latch is one sixteen-bit word or
 * two eight-bit ones. */
static bool dual_8bit(const ap_mc6840_t *ptm, unsigned index) {
  return (ptm->timer[index].control & AP_MC6840_CR_DUAL_8BIT) != 0u;
}

bool ap_mc6840_output(const ap_mc6840_t *ptm, unsigned index) {
  if (index >= AP_MC6840_TIMERS) {
    return false;
  }
  /* §3.6.1 bit 7: "If the output is masked it will always be electrically
   * low." §3.8.2 adds that for single shot the bit is not optional: "Bit 7, the
   * output enable bit must be high in the Single Shot Mode. This enables the
   * output for the 'Single Shot'." */
  if ((ptm->timer[index].control & AP_MC6840_CR_OUTPUT_ENABLE) == 0u) {
    return false;
  }
  if (ap_mc6840_mode(ptm, index) == AP_MC6840_MODE_SINGLE_SHOT) {
    return !ptm->timer[index].single_shot_fired;
  }
  return ptm->timer[index].output;
}

/* §3.7: "Counter Enable (as refers to all Continuous Mode conditions): Reset
 * clear[,] Gate pin is low". */
static bool counting(const ap_mc6840_t *ptm, unsigned index) {
  if (timers_preset(ptm)) {
    return false;
  }
  if (ptm->timer[index].gate) {
    return false;
  }
  /* §3.9 and §3.10 state the same four-part Counter Enable for both
   * measurement modes: "The gate pin goes low AND No write to the Counter
   * Latches AND Reset is cleared AND There is no individual interrupt flag
   * asserted."
   *
   * The first and third are the two tests above, which the counting modes
   * share. The fourth is extra, and it is what stops a measurement running on
   * past the interrupt that ended it -- the counting modes have no such rule
   * and keep going. The second is the latch write, which reloads and clears
   * `measuring` where it happens. */
  const ap_mc6840_mode_t mode = ap_mc6840_mode(ptm, index);
  if (mode == AP_MC6840_MODE_PERIOD_MEASUREMENT ||
      mode == AP_MC6840_MODE_PULSE_WIDTH_MEASUREMENT) {
    return !ptm->timer[index].interrupt_flag && ptm->timer[index].measuring;
  }
  return true;
}

/* §3.7: "Write to Counter Latches -- Counter Latch Initialization." A latch
 * write reloads the counter, which is what makes a newly programmed period
 * take effect at once rather than after the current one runs out. */
static void reload(ap_mc6840_timer_t *timer) {
  timer->counter = timer->latch;
  timer->lsb_counter = (uint8_t)(timer->latch & 0xFFu);
  timer->prescale_count = 0u;
  /* "Each initialization causes a single shot (even during a single shot) if
   * the counter is enabled" -- so the output is armed again by every
   * initialisation, not only by the first. */
  timer->single_shot_fired = false;
  /* §3.10's note starts the output low for a measurement, and every
   * initialisation restarts that. `timed_out` belongs to the measurement the
   * reload begins, not the one it ended. */
  timer->output = false;
  timer->timed_out = false;
}

void ap_mc6840_set_gate(ap_mc6840_t *ptm, unsigned index, bool high) {
  if (index >= AP_MC6840_TIMERS) {
    return;
  }
  bool was_high = ptm->timer[index].gate;
  ptm->timer[index].gate = high;

  ap_mc6840_timer_t *timer = &ptm->timer[index];
  const ap_mc6840_mode_t mode = ap_mc6840_mode(ptm, index);
  const bool measurement = mode == AP_MC6840_MODE_PERIOD_MEASUREMENT ||
                           mode == AP_MC6840_MODE_PULSE_WIDTH_MEASUREMENT;
  /* Bit 5 selects which way the comparison runs: §3.9's two cases are
   * "interrupt will be generated if the ... duration ... is less than the Time
   * Out" for a clear bit and "greater than" for a set one. */
  const bool interrupt_when_longer = (timer->control & AP_MC6840_CR_BIT5) != 0u;

  if (measurement && !was_high && high) {
    /* The **rising** edge, and it ends a pulse-width measurement only.
     * §3.10: "The pulse on the gate pin is defined as the period from the
     * negative transition causing initialization to the first positive
     * transition of the gate", so the down time is measured between the two
     * and this edge is where it is known. A period measurement ignores this
     * edge entirely -- §3.9 measures falling edge to falling edge. */
    if (mode == AP_MC6840_MODE_PULSE_WIDTH_MEASUREMENT && timer->measuring) {
      if (!timer->timed_out && !interrupt_when_longer) {
        /* The pulse ended before the Time Out: shorter, and this is the case
         * that asks for an interrupt. When the Time Out came first the flag was
         * set there, if the other case was programmed. */
        timer->interrupt_flag = true;
      }
      timer->measuring = false;
    }
    return;
  }

  if (was_high && !high) {
    /* §3.7.1's counter initialization for continuous mode is "Reset OR Gate pin
     * goes low", and §3.11 lists "The gate (GX) goes low. (IF CR x 3 = 0)"
     * among the things that clear an interrupt. Both are edge-triggered, which
     * is why the pin is stored rather than an enable flag. */
    if (measurement) {
      /* A falling edge ends a *period* measurement and begins the next one.
       * §3.9 measures "the period of a digital signal" between falling edges,
       * so the edge that closes one measurement opens the following one. */
      if (mode == AP_MC6840_MODE_PERIOD_MEASUREMENT && timer->measuring) {
        if (!timer->timed_out && !interrupt_when_longer) {
          timer->interrupt_flag = true;
        }
        timer->measuring = false;
      }
      /* Counter Initialization, both sections: "The gate pin goes low AND There
       * is no individual interrupt flag asserted". §3.9's first case adds "(A
       * Reset Counter Enable OR a Time Out has occurred)" with the footnote
       * "This prevents initialization on the trailing edge of a previous period
       * measurement" -- which is exactly the state `measuring` carries, so a
       * measurement still running is not restarted by its own trailing edge. */
      if (!timer->interrupt_flag && !timer->measuring) {
        reload(timer);
        timer->measuring = true;
        timer->timed_out = false;
      }
      return;
    }
    reload(&ptm->timer[index]);
    if ((ptm->timer[index].control & 0x08u) == 0u) {
      ptm->timer[index].interrupt_flag = false;
    }
  }
}

void ap_mc6840_clock(ap_mc6840_t *ptm, unsigned index) {
  if (index >= AP_MC6840_TIMERS || !counting(ptm, index)) {
    return;
  }
  ap_mc6840_timer_t *timer = &ptm->timer[index];

  /* §3.6.1: the prescaler is "available only in control register #3", and it
   * "divides either the internal or external clock". Control register 3 is
   * timer 3's, so only that timer has one. */
  if (index == 2u && (timer->control & AP_MC6840_CR3_PRESCALE) != 0u) {
    timer->prescale_count = (uint8_t)((timer->prescale_count + 1u) & 7u);
    if (timer->prescale_count != 0u) {
      return;
    }
  }

  bool timed_out;
  if (dual_8bit(ptm, index)) {
    /* §3.7.2: "This latch count down option treats the 16-bit word in the latch
     * as two separate 8-bit wide words", and the period is "the count in the LSB
     * latch +1, times the count in the MSB latch +1, times the period of the
     * clock". Two nested counters, not one wider one -- a 16-bit countdown with
     * the same latch would give a completely different period. */
    uint8_t lsb_latch = (uint8_t)(timer->latch & 0xFFu);
    if (timer->lsb_counter != 0u) {
      timer->lsb_counter--;
      return;
    }
    uint8_t msb = (uint8_t)(timer->counter >> 8);
    if (msb != 0u) {
      timer->counter = (uint16_t)(((unsigned)(msb - 1u) << 8) | lsb_latch);
      timer->lsb_counter = lsb_latch;
      return;
    }
    timed_out = true;
  } else {
    /* §3.7: "Time Out -- occurance one count after the contents of a timer
     * equals $0000." So a latch of N gives a period of N+1 clocks, which is
     * §3.7.1's "A = the total 16-bit count in the latch +1, times the period of
     * the clock". Reloading when the counter *reaches* zero instead would make
     * every period one clock short -- a 0.4% error at a latch of 250 and a
     * 100% error at a latch of zero. */
    if (timer->counter != 0u) {
      timer->counter--;
      return;
    }
    timed_out = true;
  }

  if (!timed_out) {
    return;
  }
  const ap_mc6840_mode_t timed_mode = ap_mc6840_mode(ptm, index);
  if (timed_mode == AP_MC6840_MODE_PERIOD_MEASUREMENT ||
      timed_mode == AP_MC6840_MODE_PULSE_WIDTH_MEASUREMENT) {
    /* The Time Out arrived before the gate edge that would have ended the
     * measurement, so the measured duration is the *longer* of the two -- and
     * §3.9's second case is the one that asks for an interrupt on that. The
     * first case wants the opposite and says nothing here; its flag is set at
     * the closing gate edge instead. */
    timer->timed_out = true;
    if ((timer->control & AP_MC6840_CR_BIT5) != 0u) {
      timer->interrupt_flag = true;
    }
    /* §3.10's note, which covers both measurement modes: "During the first Time
     * Out the output will be low. If the first Time Out is completed, the
     * output will go high at the Time Out completion. If further Time Outs are
     * allowed to be completed, the output will change state at the completion
     * of each Time Out." A toggle satisfies all three from a low start, which
     * is where `reload` leaves it. */
    timer->output = !timer->output;
    timer->counter = timer->latch;
    timer->lsb_counter = (uint8_t)(timer->latch & 0xFFu);
    return;
  }

  timer->interrupt_flag = true;

  if (timed_mode == AP_MC6840_MODE_SINGLE_SHOT) {
    /* §3.8: "Internally, the count recycling is continuous as if in the
     * Continuous Mode. Only one pulse is evident on the output pin for each
     * Counter Initialization." So the interrupts keep coming and the output
     * falls once and stays down until the next initialisation. */
    timer->output = false;
    timer->single_shot_fired = true;
  } else {
    timer->output = !timer->output;
  }

  timer->counter = timer->latch;
  timer->lsb_counter = (uint8_t)(timer->latch & 0xFFu);
}

bool ap_mc6840_uses_internal_clock(const ap_mc6840_t *ptm, unsigned index) {
  if (ptm == NULL || index >= AP_MC6840_TIMERS) {
    return false;
  }
  /* Bit 1: "0 external (Cx), 1 internal (E)", as the header's table has it. */
  return (ptm->timer[index].control & AP_MC6840_CR_INTERNAL_CLOCK) != 0u;
}
bool ap_mc6840_irq(const ap_mc6840_t *ptm) {
  for (unsigned i = 0; i < AP_MC6840_TIMERS; i++) {
    if (ptm->timer[i].interrupt_flag &&
        (ptm->timer[i].control & AP_MC6840_CR_IRQ_ENABLE) != 0u) {
      return true;
    }
  }
  return false;
}

static uint8_t status_of(const ap_mc6840_t *ptm) {
  uint8_t status = 0u;
  /* Named rather than shifted. `1u << i` is the same three bits and reads as an
   * anonymous bit pattern, which is how a scan for unused constants came to
   * report these flags as never set -- the *names* were unused, the behaviour
   * was there. A false positive that cost a check, and would have cost a
   * "fix". */
  static const uint8_t flag[AP_MC6840_TIMERS] = {AP_MC6840_STATUS_TIMER1,
                                                 AP_MC6840_STATUS_TIMER2,
                                                 AP_MC6840_STATUS_TIMER3};
  for (unsigned i = 0; i < AP_MC6840_TIMERS; i++) {
    if (ptm->timer[i].interrupt_flag) {
      status |= flag[i];
    }
  }
  /* §3.11: "Individual timer interrupts cannot be masked" -- so the per-timer
   * bits show through regardless of CR bit 6, and only the composite bit
   * respects it. A model that masked the individual flags would hide exactly
   * the state a polling driver reads. */
  if (ap_mc6840_irq(ptm)) {
    status |= AP_MC6840_STATUS_COMPOSITE;
  }
  return status;
}

/* Which timer a read of `rs` refers to, or AP_MC6840_TIMERS for the rest. */
static unsigned timer_of_rs(ap_mc6840_rs_t rs) {
  switch (rs) {
  case AP_MC6840_RS_TIMER1_MSB:
  case AP_MC6840_RS_TIMER1_LSB:
    return 0u;
  case AP_MC6840_RS_TIMER2_MSB:
  case AP_MC6840_RS_TIMER2_LSB:
    return 1u;
  case AP_MC6840_RS_TIMER3_MSB:
  case AP_MC6840_RS_TIMER3_LSB:
    return 2u;
  case AP_MC6840_RS_CONTROL_1_OR_3:
  case AP_MC6840_RS_CONTROL_2_OR_STATUS:
    break;
  }
  return AP_MC6840_TIMERS;
}

void ap_mc6840_write(ap_mc6840_t *ptm, ap_mc6840_rs_t rs, uint8_t value) {
  switch (rs) {
  case AP_MC6840_RS_CONTROL_1_OR_3: {
    /* Figure 2-6: "CR20 = 0  Write Control Register # 3 / CR20 = 1  Write
     * Control Register #1". One address, two registers, and which one is a
     * *mode bit held in a third register* -- so a caller that wrote CR1 without
     * first setting CR2 bit 0 would silently reprogram the prescaler. */
    unsigned target = (ptm->control2 & AP_MC6840_CR2_SELECT_CR1) != 0u ? 0u : 2u;
    bool was_preset = timers_preset(ptm);
    ptm->timer[target].control = value;
    if (!was_preset && timers_preset(ptm)) {
      /* §3.11, among what clears interrupts: "Software reset (CR10 = 1)".
       * Unconditional, unlike the latch-write and gate-low clears, which are
       * both qualified by other control bits. */
      for (unsigned i = 0; i < AP_MC6840_TIMERS; i++) {
        ptm->timer[i].interrupt_flag = false;
      }
    }
    if (was_preset && !timers_preset(ptm)) {
      /* Coming out of the software reset presets every counter from its latch,
       * which is what "ALL TIMERS PRESET" was holding them at. */
      for (unsigned i = 0; i < AP_MC6840_TIMERS; i++) {
        reload(&ptm->timer[i]);
      }
    }
    return;
  }
  case AP_MC6840_RS_CONTROL_2_OR_STATUS:
    ptm->control2 = value;
    ptm->timer[1].control = value;
    return;
  case AP_MC6840_RS_TIMER1_MSB:
  case AP_MC6840_RS_TIMER2_MSB:
  case AP_MC6840_RS_TIMER3_MSB:
    /* §3.5.1: the MS byte goes to a buffer and waits for the LS byte. */
    ptm->msb_buffer = value;
    return;
  case AP_MC6840_RS_TIMER1_LSB:
  case AP_MC6840_RS_TIMER2_LSB:
  case AP_MC6840_RS_TIMER3_LSB: {
    /* §3.5.1: "When writing to the LS byte of the timer latch ... the contents
     * of the MSB buffer is internally written to the MS byte of that latch." */
    unsigned index = timer_of_rs(rs);
    ptm->timer[index].latch =
        (uint16_t)(((uint16_t)ptm->msb_buffer << 8) | value);
    /* §3.7.1 and §3.7.2 list "Write to the Counter Latches" among the counter
     * initialisation conditions only for the variants with bit 4 clear. With
     * bit 4 set, a latch write changes the period the *next* time out uses and
     * leaves the current one running. */
    if ((ptm->timer[index].control & AP_MC6840_CR_BIT4) == 0u) {
      reload(&ptm->timer[index]);
    }
    /* §3.11, among what clears interrupts: "Writing to the latches. (IF CR x 3
     * = 0 and CR x 4 = 0)". */
    if ((ptm->timer[index].control & 0x18u) == 0u) {
      ptm->timer[index].interrupt_flag = false;
    }
    return;
  }
  }
}

uint8_t ap_mc6840_read(ap_mc6840_t *ptm, ap_mc6840_rs_t rs) {
  switch (rs) {
  case AP_MC6840_RS_CONTROL_1_OR_3:
    /* Figure 2-6, R/W = 1, RS 000: "No Operation". */
    return 0u;
  case AP_MC6840_RS_CONTROL_2_OR_STATUS: {
    uint8_t status = status_of(ptm);
    /* Snapshot for §3.11's two-step clear. Taken here so that a flag raised
     * after this read cannot be cleared by the timer read that follows. */
    ptm->status_snapshot = status;
    ptm->status_was_read = true;
    return status;
  }
  case AP_MC6840_RS_TIMER1_MSB:
  case AP_MC6840_RS_TIMER2_MSB:
  case AP_MC6840_RS_TIMER3_MSB: {
    unsigned index = timer_of_rs(rs);
    uint16_t counter = ptm->timer[index].counter;
    /* §3.5.2: "the MS byte (8-bits) is read at $XXX2 and the LS byte is
     * internally written to the LSB buffer register which may be read at the
     * next memory location". Latching the low byte here is what makes a 16-bit
     * read coherent while the counter is running. */
    ptm->lsb_buffer = (uint8_t)(counter & 0xFFu);

    /* §3.11: "Read the status register (RS), then read the timer (RT) causing
     * the interrupt. (An interrupt that occurs between RS and RT will not be
     * cleared.)" */
    if (ptm->status_was_read &&
        (ptm->status_snapshot & (1u << index)) != 0u) {
      ptm->timer[index].interrupt_flag = false;
      ptm->status_was_read = false;
    }
    return (uint8_t)(counter >> 8);
  }
  case AP_MC6840_RS_TIMER1_LSB:
  case AP_MC6840_RS_TIMER2_LSB:
  case AP_MC6840_RS_TIMER3_LSB:
    /* Figure 2-6: every odd read is "Read LSB Buffer Register" -- the same
     * buffer for all three timers, not one per timer. */
    return ptm->lsb_buffer;
  }
  return 0u;
}
