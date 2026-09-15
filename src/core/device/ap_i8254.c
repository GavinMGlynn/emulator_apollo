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

/* ## Counting down, in either of Figure 7's two number systems
 *
 * "BCD: 0 Binary Counter 16-bits, 1 Binary Coded Decimal (BCD) Counter (4
 * Decades)", and p. 6-161: "The Counter does not stop when it reaches zero. In
 * Modes 0, 1, 4, and 5 the Counter 'wraps around' to the highest count, either
 * FFFF hex for binary counting or 9999 for BCD counting". So a BCD counter is
 * four decimal digits held in the same sixteen bits, and it wraps at 10^4. */
static bool counts_bcd(const ap_i8254_counter_t *c) {
  return (c->control & AP_I8254_CW_BCD) != 0u;
}

static unsigned bcd_to_decimal(uint16_t word) {
  return ((word >> 12) & 0xFu) * 1000u + ((word >> 8) & 0xFu) * 100u +
         ((word >> 4) & 0xFu) * 10u + (word & 0xFu);
}

static uint16_t decimal_to_bcd(unsigned value) {
  return (uint16_t)(((value / 1000u) % 10u) << 12 |
                    ((value / 100u) % 10u) << 8 | ((value / 10u) % 10u) << 4 |
                    (value % 10u));
}

static void count_down(ap_i8254_counter_t *c, unsigned by) {
  if (!counts_bcd(c)) {
    c->counter = (uint16_t)(c->counter - by);
    return;
  }
  const unsigned value = bcd_to_decimal(c->counter);
  c->counter = decimal_to_bcd((value + 10000u - by) % 10000u);
}

/* "New count is loaded into CE (CR -> CE): NULL COUNT = 0", Figure 12, and
 * p. 6-161: "New counts are loaded and Counters are decremented on the falling
 * edge of CLK". The only place the flag clears. */
static void transfer(ap_i8254_counter_t *c) {
  c->counter = c->latch;
  c->null_count = false;
  c->load_pending = false;
  c->counting = true;
  c->expired = false;
}

/* Mode 3's load, p. 6-159: "Even counts: ... The initial count is loaded on one
 * CLK pulse and then is decremented by two on succeeding CLK pulses" and "Odd
 * counts: ... The initial count minus one (an even number) is loaded on one CLK
 * pulse". The parity of a BCD count is its low digit's, which is bit 0 either
 * way. */
static bool square_count_is_odd(const ap_i8254_counter_t *c) {
  return (c->latch & 1u) != 0u;
}

static void load_square(ap_i8254_counter_t *c) {
  transfer(c);
  if (square_count_is_odd(c)) {
    count_down(c, 1u);
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
   * without reading the status, all but the first are ignored" -- the same rule
   * the count latch follows, so a second latch does not overwrite an unread
   * one. */
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
  /* p. 6-161: "When a Control Word is written to a Counter, all Control Logic is
   * immediately reset and OUT goes to a known initial state; no CLK pulses are
   * required for this." Counting stops until a count is loaded, and NULL COUNT
   * is 1 (Figure 12, footnote 1: "only the counter specified by the control
   * word"). */
  c->null_count = true;
  c->counting = false;
  c->load_pending = false;
  c->trigger = false;
  c->count_written = false;
  c->expired = false;
  /* p. 6-152: "CR_M and CR_L are cleared when the Counter is programmed. In this
   * way, if the Counter has been programmed for one byte counts ... the other
   * byte will be zero." */
  c->latch = 0u;
  c->write_msb_next = false;
  c->read_msb_next = false;
  /* "Each latched Counter's OL holds its count until it is read (or the Counter
   * is reprogrammed)" (p. 6-155). */
  c->count_latched = false;
  c->status_latched = false;
  /* Mode 0 "OUT is initially low"; every other mode's OUT "will be initially
   * high" -- each mode definition opens with that state. */
  c->out = ap_i8254_mode(pit, index) != 0u;
}

/* The first byte of a two-byte count, which each mode states separately.
 * Mode 0, p. 6-157: "Writing the first byte disables counting. OUT is set low
 * immediately (no clock pulse required)". Mode 4, p. 6-159: "Writing the first
 * byte has no effect on counting." The others say nothing, and NULL COUNT does
 * not move until the second (Figure 12, footnote 2). */
static void first_byte_written(const ap_i8254_t *pit, unsigned index,
                               ap_i8254_counter_t *c) {
  if (ap_i8254_mode(pit, index) == 0u) {
    c->counting = false;
    c->out = false;
  }
}

/* A whole count is in CR. When it reaches CE is the mode's business:
 *
 *   - modes 0 and 4: "on the next CLK pulse", whether or not a count was
 *     already running ("If a new count is written to the Counter, it will be
 *     loaded on the next CLK pulse and counting will continue from the new
 *     count");
 *   - modes 2 and 3: on the next CLK pulse after the Control Word, but a count
 *     written while counting "does not affect the current counting sequence" --
 *     it is loaded at the end of the current cycle (or half-cycle), or by a
 *     trigger;
 *   - modes 1 and 5: only by a trigger.
 *
 * And mode 0's OUT "remains high until a new count or a new Mode 0 Control Word
 * is written into the Counter". */
static void count_written(const ap_i8254_t *pit, unsigned index,
                          ap_i8254_counter_t *c) {
  c->null_count = true;
  c->count_written = true;
  switch (ap_i8254_mode(pit, index)) {
  case 0u:
    c->out = false;
    c->load_pending = true;
    break;
  case 4u:
    c->load_pending = true;
    break;
  case 2u:
  case 3u:
    if (!c->counting) {
      c->load_pending = true;
    }
    break;
  default:
    break;
  }
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
  const unsigned index = (unsigned)reg;
  ap_i8254_counter_t *c = &pit->counter[index];

  switch (rw_of(c)) {
  case AP_I8254_RW_LSB:
    c->latch = (uint16_t)((c->latch & 0xFF00u) | value);
    count_written(pit, index, c);
    return;
  case AP_I8254_RW_MSB:
    c->latch = (uint16_t)((c->latch & 0x00FFu) | (uint16_t)(value << 8));
    count_written(pit, index, c);
    return;
  case AP_I8254_RW_LSB_THEN_MSB:
    if (!c->write_msb_next) {
      c->latch = (uint16_t)((c->latch & 0xFF00u) | value);
      c->write_msb_next = true;
      first_byte_written(pit, index, c);
      return;
    }
    c->latch = (uint16_t)((c->latch & 0x00FFu) | (uint16_t)(value << 8));
    c->write_msb_next = false;
    count_written(pit, index, c);
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

/* Figure 21, the gate pin operations summary, and p. 6-161's two sampling
 * rules. The level is kept for the next CLK pulse to sample; the edge sets the
 * trigger flip-flop in the modes that have one; and modes 2 and 3 act on a low
 * gate at once: "If GATE goes low during an output pulse, OUT is set high
 * immediately" (mode 2), "If GATE goes low while OUT is low, OUT is set high
 * immediately; no CLK pulse is required" (mode 3). */
void ap_i8254_set_gate(ap_i8254_t *pit, unsigned index, bool high) {
  if (pit == NULL || index >= AP_I8254_COUNTERS) {
    return;
  }
  ap_i8254_counter_t *c = &pit->counter[index];
  const bool was = c->gate;
  c->gate = high;
  const unsigned mode = ap_i8254_mode(pit, index);
  if (was && !high && (mode == 2u || mode == 3u)) {
    c->out = true;
  }
  if (!was && high &&
      (mode == 1u || mode == 2u || mode == 3u || mode == 5u)) {
    c->trigger = true;
  }
}

bool ap_i8254_out(const ap_i8254_t *pit, unsigned index) {
  if (pit == NULL || index >= AP_I8254_COUNTERS) {
    return false;
  }
  return pit->counter[index].out;
}

/* One CLK pulse to **one** counter. The 8254 has three independent CLK pins,
 * and a board is free to drive them from three different events -- which this
 * machine's ring controller does: `[EH]` p. 12-30 names `RCV_STAT`'s bits 2:0
 * "pkt exceeded max_rcv_cnt", "data rcv in progress" and "hdr rcv in progress",
 * three distinct receive conditions, one per receive counter. A model with only
 * a whole-part pulse cannot express that, and cannot express the firmware's own
 * requirement that the header counter and the packet counter reach *different*
 * totals over one transfer (`RING.md` 76a).
 *
 * One call is one whole pulse: the rising edge samples GATE and the trigger
 * flip-flop, and the falling edge loads or decrements (p. 6-161). */
void ap_i8254_clock_counter(ap_i8254_t *pit, unsigned index) {
  if (pit == NULL || index >= AP_I8254_COUNTERS) {
    return;
  }
  ap_i8254_counter_t *c = &pit->counter[index];
  const unsigned mode = ap_i8254_mode(pit, index);
  const bool triggered = c->trigger && c->count_written;
  c->trigger = false;

  switch (mode) {
  case 0u:
    /* Mode 0, interrupt on terminal count (p. 6-157, Figure 15). The load
     * pulse happens "while GATE = 0" too; only counting waits on the gate. OUT
     * goes high when the count expires and stays high while the counter wraps
     * on (`RING.md` 120a: the ring firmware reads `FE00` from a counter loaded
     * `$1FF` after it has gone through zero). */
    if (c->load_pending) {
      transfer(c);
      return;
    }
    if (!c->counting || !c->gate) {
      return;
    }
    count_down(c, 1u);
    if (c->counter == 0u && !c->expired) {
      c->expired = true;
      c->out = true;
    }
    return;

  case 1u:
    /* Mode 1, hardware retriggerable one-shot (p. 6-158, Figure 16): a trigger
     * loads the count and sets OUT low on the next CLK pulse, and OUT goes high
     * when it expires. GATE has no other effect. */
    if (triggered) {
      transfer(c);
      c->out = false;
      return;
    }
    if (!c->counting) {
      return;
    }
    count_down(c, 1u);
    if (c->counter == 0u && !c->expired) {
      c->expired = true;
      c->out = true;
    }
    return;

  case 2u:
    /* Mode 2, rate generator (p. 6-158, Figure 17): "When the initial count has
     * decremented to 1, OUT goes low for one CLK pulse. OUT then goes high
     * again, the Counter reloads the initial count and the process is
     * repeated." The reload takes whatever CR now holds, which is how a count
     * written mid-cycle takes effect at the end of it. */
    if (c->load_pending || triggered) {
      transfer(c);
      c->out = true;
      return;
    }
    if (!c->counting || !c->gate) {
      return;
    }
    if (c->counter == 1u) {
      transfer(c);
      c->out = true;
      return;
    }
    count_down(c, 1u);
    if (c->counter == 1u) {
      c->out = false;
    }
    return;

  case 3u:
    /* Mode 3, square wave (p. 6-159, Figure 18). Even counts: decrement by two,
     * and at expiry OUT changes and the count reloads. Odd counts: the count
     * minus one is loaded; "One CLK pulse *after* the count expires, OUT goes
     * low and the Counter is reloaded with the initial count minus one ...
     * When the count expires, OUT goes high again" -- so OUT is high for
     * (N+1)/2 pulses and low for (N-1)/2. */
    if (c->load_pending || triggered) {
      load_square(c);
      c->out = true;
      return;
    }
    if (!c->counting || !c->gate) {
      return;
    }
    if (square_count_is_odd(c) && c->out && c->counter == 0u) {
      load_square(c);
      c->out = false;
      return;
    }
    count_down(c, 2u);
    if (c->counter == 0u) {
      if (!square_count_is_odd(c)) {
        load_square(c);
        c->out = !c->out;
      } else if (!c->out) {
        load_square(c);
        c->out = true;
      }
    }
    return;

  case 4u:
    /* Mode 4, software triggered strobe (p. 6-159, Figure 19): "When the
     * initial count expires, OUT will go low for one CLK pulse and then go high
     * again", N+1 pulses after the count is written. */
    if (!c->out) {
      c->out = true;
    }
    if (c->load_pending) {
      transfer(c);
      return;
    }
    if (!c->counting || !c->gate) {
      return;
    }
    count_down(c, 1u);
    if (c->counter == 0u && !c->expired) {
      c->expired = true;
      c->out = false;
    }
    return;

  case 5u:
  default:
    /* Mode 5, hardware triggered strobe (p. 6-160, Figure 20): "the Counter will
     * not be loaded until the CLK pulse after a trigger", and the strobe is
     * mode 4's. Retriggerable; GATE has no other effect. */
    if (!c->out) {
      c->out = true;
    }
    if (triggered) {
      transfer(c);
      return;
    }
    if (!c->counting) {
      return;
    }
    count_down(c, 1u);
    if (c->counter == 0u && !c->expired) {
      c->expired = true;
      c->out = false;
    }
    return;
  }
}

void ap_i8254_clock(ap_i8254_t *pit) {
  if (pit == NULL) {
    return;
  }
  for (unsigned i = 0; i < AP_I8254_COUNTERS; i++) {
    ap_i8254_clock_counter(pit, i);
  }
}
