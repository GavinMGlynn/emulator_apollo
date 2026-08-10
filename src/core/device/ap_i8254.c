#include "device/ap_i8254.h"

#include <string.h>

unsigned ap_i8254_mode(const ap_i8254_t *pit, unsigned index) {
  if (index >= AP_I8254_COUNTERS) {
    return 0u;
  }
  const unsigned m = (pit->counter[index].control & AP_I8254_CW_MODE) >> 1;
  /* Figure 7 gives modes 6 and 7 as aliases of 2 and 3: the encoding is `X10`
   * and `X11`, so the high bit is a don't-care for those two. */
  return (m == 6u) ? 2u : (m == 7u) ? 3u : m;
}

bool ap_i8254_mode_gated(const ap_i8254_t *pit, unsigned index) {
  const unsigned m = ap_i8254_mode(pit, index);
  return m == 1u || m == 4u || m == 5u;
}

static ap_i8254_rw_t rw_of(const ap_i8254_counter_t *c) {
  return (ap_i8254_rw_t)((c->control & AP_I8254_CW_RW) >> 4);
}

void ap_i8254_reset(ap_i8254_t *pit) {
  memset(pit, 0, sizeof *pit);
  for (unsigned i = 0; i < AP_I8254_COUNTERS; i++) {
    /* The GATE pins are pulled high on a board that does not drive them, which
     * is what makes modes 0, 2 and 3 count at all. A part whose gates came up
     * low would sit still and look like a dead timer. */
    pit->counter[i].gate = true;
  }
}

/* "New count is loaded into CE (CR -> CE): NULL COUNT = 0", Figure 12. This is
 * the only place the flag clears, and the only place counting begins. */
static void load(ap_i8254_counter_t *c) {
  c->counter = c->latch;
  c->null_count = false;
  c->counting = true;
  /* Mode 0's OUT "will be initially low ... and it will go high when the
   * Counter reaches zero", and writing a count while counting restarts that. */
  if ((c->control & AP_I8254_CW_MODE) >> 1 == 0u) {
    c->out = false;
  }
}

static uint8_t status_of(const ap_i8254_counter_t *c) {
  /* Figure 11: OUT, NULL COUNT, then "the counter's programmed Mode exactly as
   * written in the last Mode Control Word" -- which is the low six bits of that
   * word, RW1 RW0 M2 M1 M0 BCD. */
  uint8_t status = (uint8_t)(c->control & 0x3Fu);
  if (c->out) {
    status |= AP_I8254_STATUS_OUT;
  }
  if (c->null_count) {
    status |= AP_I8254_STATUS_NULL_COUNT;
  }
  return status;
}

static void latch_count(ap_i8254_counter_t *c) {
  /* "If multiple status latch operations of the counter(s) are performed
   * without reading the status, all but the first are ignored" -- §the same
   * rule the count latch follows, so a second latch does not overwrite an
   * unread one. */
  if (!c->count_latched) {
    c->count_latch = c->counter;
    c->count_latched = true;
  }
}

static void latch_status(ap_i8254_counter_t *c) {
  if (!c->status_latched) {
    c->status_latch = status_of(c);
    c->status_latched = true;
  }
}

static void write_control(ap_i8254_t *pit, uint8_t value) {
  if ((value & AP_I8254_CW_SC) == AP_I8254_READ_BACK) {
    /* Figure 10. D5 and D4 are active low and independent, so one command can
     * latch both. D0 is reserved and ignored. */
    for (unsigned i = 0; i < AP_I8254_COUNTERS; i++) {
      const uint8_t select = (uint8_t)(AP_I8254_RB_COUNTER_0 << i);
      if ((value & select) == 0u) {
        continue;
      }
      if ((value & AP_I8254_RB_NOT_COUNT) == 0u) {
        latch_count(&pit->counter[i]);
      }
      if ((value & AP_I8254_RB_NOT_STATUS) == 0u) {
        latch_status(&pit->counter[i]);
      }
    }
    return;
  }

  const unsigned index = (value & AP_I8254_CW_SC) >> 6;
  ap_i8254_counter_t *c = &pit->counter[index];

  if ((value & AP_I8254_CW_RW) == 0u) {
    /* Figure 9, the counter latch command: the RW field is zero and the rest of
     * the word is don't-care. It does *not* reprogram the counter, which is the
     * distinction that makes reading "on the fly" possible. */
    latch_count(c);
    return;
  }

  c->control = value;
  /* "Write to the control word register: NULL COUNT = 1", and only for the
   * counter the word selects -- Figure 12's first footnote. Counting stops
   * until a count is loaded: a control word with no count behind it leaves the
   * counter holding whatever it held, and the driver is expected to load one. */
  c->null_count = true;
  c->counting = false;
  c->write_msb_next = false;
  c->read_msb_next = false;
  c->count_latched = false;
  c->status_latched = false;
  /* Mode 0 "will be initially low"; every other mode's OUT is initially high.
   * Figure 7's mode descriptions each open with that state. */
  c->out = ap_i8254_mode(pit, index) != 0u;
}

void ap_i8254_write(ap_i8254_t *pit, ap_i8254_reg_t reg, uint8_t value) {
  if (pit == NULL) {
    return;
  }
  if (reg == AP_I8254_CONTROL) {
    write_control(pit, value);
    return;
  }
  if ((unsigned)reg >= AP_I8254_COUNTERS) {
    return;
  }
  ap_i8254_counter_t *c = &pit->counter[reg];

  switch (rw_of(c)) {
  case AP_I8254_RW_LSB:
    c->latch = (uint16_t)((c->latch & 0xFF00u) | value);
    c->null_count = true;
    load(c);
    return;
  case AP_I8254_RW_MSB:
    c->latch = (uint16_t)((c->latch & 0x00FFu) | (uint16_t)(value << 8));
    c->null_count = true;
    load(c);
    return;
  case AP_I8254_RW_LSB_THEN_MSB:
    if (!c->write_msb_next) {
      c->latch = (uint16_t)((c->latch & 0xFF00u) | value);
      c->write_msb_next = true;
      /* "Writing the first byte disables counting" -- the counter is held until
       * the pair completes, and NULL COUNT stays set. Figure 12's footnote puts
       * the transition on the *second* byte. */
      c->counting = false;
      c->null_count = true;
      return;
    }
    c->latch = (uint16_t)((c->latch & 0x00FFu) | (uint16_t)(value << 8));
    c->write_msb_next = false;
    load(c);
    return;
  case AP_I8254_RW_LATCH:
    break;
  }
}

uint8_t ap_i8254_read(ap_i8254_t *pit, ap_i8254_reg_t reg) {
  if (pit == NULL || (unsigned)reg >= AP_I8254_COUNTERS) {
    /* The control address is write-only: `A1,A0 = 11` names the control word
     * register for a write and the part drives nothing for a read there. */
    return 0u;
  }
  ap_i8254_counter_t *c = &pit->counter[reg];

  /* "If both count and status of a counter are latched, the first read
   * operation of that counter will return the latched status, regardless of
   * which was latched first." */
  if (c->status_latched) {
    c->status_latched = false;
    return c->status_latch;
  }

  const uint16_t value = c->count_latched ? c->count_latch : c->counter;

  switch (rw_of(c)) {
  case AP_I8254_RW_LSB:
    c->count_latched = false;
    return (uint8_t)(value & 0xFFu);
  case AP_I8254_RW_MSB:
    c->count_latched = false;
    return (uint8_t)(value >> 8);
  case AP_I8254_RW_LSB_THEN_MSB:
    if (!c->read_msb_next) {
      c->read_msb_next = true;
      return (uint8_t)(value & 0xFFu);
    }
    c->read_msb_next = false;
    /* "The count is then unlatched automatically and the OL returns to
     * following the counting element" -- after the whole count is out, not
     * after its first half. */
    c->count_latched = false;
    return (uint8_t)(value >> 8);
  case AP_I8254_RW_LATCH:
    break;
  }
  return 0u;
}

void ap_i8254_set_gate(ap_i8254_t *pit, unsigned index, bool high) {
  if (pit == NULL || index >= AP_I8254_COUNTERS) {
    return;
  }
  ap_i8254_counter_t *c = &pit->counter[index];
  const bool was = c->gate;
  c->gate = high;
  /* Modes 2 and 3 reload on a rising gate edge; mode 0 merely resumes. The
   * three gate-triggered modes need the edge to *start*, which is why
   * `ap_i8254_mode_gated` exists and why this board's undriven gate leaves them
   * reported rather than approximated. */
  if (!was && high) {
    const unsigned mode = ap_i8254_mode(pit, index);
    if (mode == 2u || mode == 3u) {
      c->counter = c->latch;
    }
  }
}

bool ap_i8254_out(const ap_i8254_t *pit, unsigned index) {
  if (pit == NULL || index >= AP_I8254_COUNTERS) {
    return false;
  }
  return pit->counter[index].out;
}

void ap_i8254_clock(ap_i8254_t *pit) {
  if (pit == NULL) {
    return;
  }
  for (unsigned i = 0; i < AP_I8254_COUNTERS; i++) {
    ap_i8254_counter_t *c = &pit->counter[i];
    if (!c->counting || !c->gate) {
      /* "In Modes 0, 2, 3 and 4 the GATE input is level sensitive" -- a low
       * gate holds the count where it stands rather than resetting it. */
      continue;
    }
    const unsigned mode = ap_i8254_mode(pit, i);

    if (mode == 3u) {
      /* Mode 3, the square wave: the count is decremented by two and OUT
       * toggles at each expiry, giving a half-period of N/2 clocks. */
      c->counter = (uint16_t)(c->counter - 2u);
      if (c->counter == 0u || c->counter == 0xFFFFu) {
        c->out = !c->out;
        c->counter = c->latch;
      }
      continue;
    }

    c->counter = (uint16_t)(c->counter - 1u);
    if (c->counter != 0u) {
      continue;
    }

    if (mode == 2u) {
      /* Mode 2, the rate generator: OUT goes low for one clock at zero and the
       * counter reloads, so it is periodic without a new write. */
      c->out = false;
      c->counter = c->latch;
      continue;
    }

    /* Mode 0, and the three gate-triggered modes this board cannot distinguish:
     * "OUT will go high when the Counter reaches zero", and stays there until
     * the counter is reprogrammed. Counting continues past zero -- the counter
     * wraps -- but the terminal count happens once. */
    c->out = true;
    c->counting = false;
  }
}
