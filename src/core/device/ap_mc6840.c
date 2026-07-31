#include "device/ap_mc6840.h"

#include <string.h>

/* `[6840]` §3.7.1's 16-bit continuous mode: control bits 5,4,3 = 0,1,0 with
 * bit 2 clear. The manual gives it as "X X 0 1 0 0 X X". */
#define MODE_CONTINUOUS_16BIT 0x10u

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

bool ap_mc6840_mode_supported(const ap_mc6840_t *ptm, unsigned index) {
  if (index >= AP_MC6840_TIMERS) {
    return false;
  }
  uint8_t control = ptm->timer[index].control;
  if ((control & AP_MC6840_CR_DUAL_8BIT) != 0u) {
    return false;
  }
  return (control & AP_MC6840_CR_MODE_MASK) == MODE_CONTINUOUS_16BIT;
}

bool ap_mc6840_output(const ap_mc6840_t *ptm, unsigned index) {
  if (index >= AP_MC6840_TIMERS) {
    return false;
  }
  /* §3.6.1 bit 7: "If the output is masked it will always be electrically
   * low." */
  if ((ptm->timer[index].control & AP_MC6840_CR_OUTPUT_ENABLE) == 0u) {
    return false;
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
  return ap_mc6840_mode_supported(ptm, index);
}

/* §3.7: "Write to Counter Latches -- Counter Latch Initialization." A latch
 * write reloads the counter, which is what makes a newly programmed period
 * take effect at once rather than after the current one runs out. */
static void reload(ap_mc6840_timer_t *timer) {
  timer->counter = timer->latch;
  timer->prescale_count = 0u;
}

void ap_mc6840_set_gate(ap_mc6840_t *ptm, unsigned index, bool high) {
  if (index >= AP_MC6840_TIMERS) {
    return;
  }
  bool was_high = ptm->timer[index].gate;
  ptm->timer[index].gate = high;

  if (was_high && !high) {
    /* §3.7.1's counter initialization for continuous mode is "Reset OR Gate pin
     * goes low", and §3.11 lists "The gate (GX) goes low. (IF CR x 3 = 0)"
     * among the things that clear an interrupt. Both are edge-triggered, which
     * is why the pin is stored rather than an enable flag. */
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

  if (timer->counter == 0u) {
    /* §3.7: "Time Out -- occurance one count after the contents of a timer
     * equals $0000." So a latch of N gives a period of N+1 clocks, which is
     * §3.7.1's "A = the total 16-bit count in the latch +1, times the period of
     * the clock". Reloading when the counter *reaches* zero instead would make
     * every period one clock short -- a 0.4% error at a latch of 250 and a
     * 100% error at a latch of zero. */
    timer->interrupt_flag = true;
    timer->output = !timer->output;
    timer->counter = timer->latch;
    return;
  }
  timer->counter--;
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
  for (unsigned i = 0; i < AP_MC6840_TIMERS; i++) {
    if (ptm->timer[i].interrupt_flag) {
      status |= (uint8_t)(1u << i);
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
    reload(&ptm->timer[index]);
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
