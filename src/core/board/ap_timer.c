#include "board/ap_timer.h"

#include <string.h>

bool ap_timer_reset(ap_timer_t *timer) {
  memset(timer, 0, sizeof *timer);
  ap_mc6840_reset(&timer->ptm);

  static const uint32_t rates[AP_MC6840_TIMERS] = {
      AP_TIMER1_HZ,
      AP_TIMER2_HZ,
      AP_TIMER3_HZ,
  };
  for (unsigned i = 0; i < AP_MC6840_TIMERS; i++) {
    /* `ap_clock_init` "returns false (leaving *clk zeroed) when hz is zero or is
     * not exactly divided by the time base -- the caller must treat that as a
     * configuration error, not round it away." Propagated rather than ignored:
     * a timer running at a rounded rate drifts, and drift in the machine's
     * highest-priority interrupt is the hardest kind to attribute. */
    if (!ap_clock_init(&timer->clock[i], rates[i])) {
      memset(timer, 0, sizeof *timer);
      return false;
    }
  }
  return true;
}

bool ap_timer_decode(uint32_t address, ap_mc6840_rs_t *rs) {
  if ((address & ~(AP_TIMER_RANGE - 1u)) != AP_TIMER_ADDR) {
    return false;
  }
  /* Odd bytes only. See the header: the region reads `00` on every even byte
   * and the part's eight registers on the odd ones. */
  if ((address & 1u) == 0u) {
    return false;
  }
  *rs = (ap_mc6840_rs_t)((address >> 1) & 7u);
  return true;
}

uint8_t ap_timer_read(ap_timer_t *timer, uint32_t address) {
  ap_mc6840_rs_t rs;
  if (!ap_timer_decode(address, &rs)) {
    /* An even byte inside the range: the other lane, which the dump shows as
     * zero rather than as an alias of a register. */
    return 0u;
  }
  return ap_mc6840_read(&timer->ptm, rs);
}

void ap_timer_write(ap_timer_t *timer, uint32_t address, uint8_t value) {
  ap_mc6840_rs_t rs;
  if (!ap_timer_decode(address, &rs)) {
    return;
  }
  ap_mc6840_write(&timer->ptm, rs, value);
}

void ap_timer_advance(ap_timer_t *timer, ap_time_t now) {
  for (unsigned i = 0; i < AP_MC6840_TIMERS; i++) {
    ap_time_t from = timer->clocked_to[i];
    if (now <= from) {
      /* Already there, or asked to go backwards. Neither is an error a device
       * should act on, and wrapping the subtraction below would issue about
       * 2^64 pulses. */
      continue;
    }
    /* A division per call, avoided when it can only yield zero.
     *
     * `ap_clock_cycles_in` is `duration / period`, so the result is zero
     * exactly when the duration is shorter than one period -- and a compare is
     * a great deal cheaper than a 64-bit divide on a path taken once per
     * emulated instruction. Provably identical: with zero cycles the loop below
     * does not run and the cursor advances by `ap_clock_duration(clk, 0)`,
     * which is zero, so the whole iteration writes nothing. */
    const ap_time_t delta = now - from;
    if (delta < timer->clock[i].period) {
      continue;
    }
    uint64_t pulses = ap_clock_cycles_in(&timer->clock[i], delta);
    for (uint64_t p = 0; p < pulses; p++) {
      ap_mc6840_clock_external(&timer->ptm, i);
    }
    /* Advance by whole pulses only, so the remainder carries into the next
     * call. Setting the cursor to `now` instead would throw away a fraction of
     * a period on every advance and run the timer slow by an amount that
     * depends on how often it is polled -- the classic way a device's rate
     * becomes a function of the scheduler. */
    timer->clocked_to[i] =
        from + ap_clock_duration(&timer->clock[i], pulses);
  }
}

ap_time_t ap_timer_interrupt_next_change(const ap_timer_t *timer) {
  ap_time_t next = AP_TIME_NEVER;
  for (unsigned i = 0; i < AP_MC6840_TIMERS; i++) {
    if (timer->clock[i].period == 0u) {
      continue;
    }
    const ap_time_t at = timer->clocked_to[i] + timer->clock[i].period;
    if (at < next) {
      next = at;
    }
  }
  return next;
}

bool ap_timer_irq(const ap_timer_t *timer) { return ap_mc6840_irq(&timer->ptm); }
