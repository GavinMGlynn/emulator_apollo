#include "device/ap_ring_ctl.h"

#include <string.h>

void ap_ring_ctl_reset(ap_ring_ctl_t *ctl, bool present) {
  if (ctl == NULL) {
    return;
  }
  memset(ctl, 0, sizeof *ctl);
  ctl->present = present;

  ap_i8254_reset(&ctl->a1.timer_a);
  ap_i8254_reset(&ctl->a1.timer_b);
  ap_i8254_reset(&ctl->a2.timer_a);
  ap_i8254_reset(&ctl->a2.timer_b);

  if (!present) {
    return;
  }
  /* Finding 39: init accepts `$36` or `$37` and nothing else. `[ROM3500]` is
   * the Apollo 10666 board, so this unit answers as one; a model that answered
   * `$37` would be equally consistent with the ROM, and the choice between them
   * is not evidenced -- recorded here rather than hidden, since the firmware
   * only ever compares. */
  ctl->a1.id = AP_RING_CTL_ID_6;
  ctl->a2.id = AP_RING_CTL_ID_6;
  /* Finding 40: bit 15 is what init reads to decide the slot is populated. */
  /* **The idle value the firmware's own self-test asserts.** Subtest 01 reads
   * `+400`, masks with `$F806` and requires the result to *equal* `$F806`
   * (`$A6E`: `move.w (a1),d1 / and.w d4,d1 / cmp.w d1,d2` with `d2 = d4 =
   * $F806`), failing to `loc_08D2` with code `E0000001` otherwise. So bits 15,
   * 14, 13, 12, 11, 2 and 1 all read set on a healthy board that has just been
   * reset.
   *
   * This is the firmware specifying its own hardware, which is the strongest
   * source this controller has -- there is no register manual for the AT board,
   * five documentary and cross-reading attempts failed to settle these bits,
   * and the ROM asserts them directly. It is *not* fitting the model to the
   * test: the assertion is what a working board reads, and the later subtests
   * constrain the same register further rather than agreeing with this one by
   * construction. `AP_RING_CTL_STATUS_PRESENT` is bit 15 of it, finding 40. */
  ctl->a1.status = AP_RING_CTL_STATUS_IDLE;
  ctl->a2.status = AP_RING_CTL_STATUS_IDLE;
}

bool ap_ring_ctl_decode(uint32_t address, unsigned *unit, bool *second_window,
                        uint32_t *offset) {
  static const struct {
    uint32_t base;
    unsigned unit;
    bool second;
  } windows[] = {
      {AP_RING_CTL_UNIT0_A1, 0u, false},
      {AP_RING_CTL_UNIT0_A2, 0u, true},
      {AP_RING_CTL_UNIT1_A1, 1u, false},
      {AP_RING_CTL_UNIT1_A2, 1u, true},
  };
  for (unsigned i = 0; i < sizeof windows / sizeof windows[0]; i++) {
    if (address >= windows[i].base &&
        address < windows[i].base + AP_RING_CTL_WINDOW) {
      if (unit != NULL) {
        *unit = windows[i].unit;
      }
      if (second_window != NULL) {
        *second_window = windows[i].second;
      }
      if (offset != NULL) {
        *offset = address - windows[i].base;
      }
      return true;
    }
  }
  return false;
}

static ap_ring_ctl_window_t *window_of(ap_ring_ctl_t *ctl, bool second) {
  return second ? &ctl->a2 : &ctl->a1;
}

/* A timer bank's four slots are the 8254's three counters and its control
 * word, at `+0`, `+2`, `+4` and `+6` -- finding 41. The part is byte-wide, and
 * the firmware's LSB-then-MSB helper writes bytes, so the slot maps straight
 * onto a register number. */
static ap_i8254_reg_t timer_reg(uint32_t offset) {
  return (ap_i8254_reg_t)((offset & AP_RING_CTL_SLOT_MASK) >> 1);
}

uint8_t ap_ring_ctl_read8(ap_ring_ctl_t *ctl, bool second_window,
                          uint32_t offset) {
  if (ctl == NULL) {
    return 0xFFu;
  }
  ap_ring_ctl_window_t *w = window_of(ctl, second_window);

  /* The board's registers are byte-wide and sit at *even* offsets: findings 12
   * and 14 give a stride of two across four slots, and finding 41's `btst #14`
   * on a word read of `+802` puts the register byte in the word's high half,
   * which is the even address on a big-endian bus. So an odd byte is not a
   * register at all -- it is the other half of the lane, which nothing drives.
   */
  const bool odd = (offset & 1u) != 0u;

  switch (offset & AP_RING_CTL_BANK_MASK) {
  case AP_RING_CTL_BANK_ID:
    /* Finding 39 reads `+000` as a byte. An unpopulated slot leaves the bus to
     * the pull-ups, which is `FF` on this machine -- the same reasoning the AT
     * window at large uses -- and `FF` is neither `$36` nor `$37`, so an absent
     * board fails the ID check exactly as it should. */
    if ((offset & AP_RING_CTL_SLOT_MASK) == 0u) {
      if (odd) {
        /* **The ID's low lane, and the firmware constrains one bit of it.**
         * Finding 39 reads `+000` as a *byte*, so the odd half never mattered
         * and answered with the pull-ups. Subtest 03 reads the same address as
         * a **word** -- `move.w (a4),d1 / andi.w #$8,d1` -- and requires the
         * result to be zero, which `FF` in this lane cannot give.
         *
         * So bit 3 reads clear on a healthy board. The rest of the lane is
         * unconstrained by anything measured, and zero is the least invented
         * answer: it asserts nothing the firmware did not, where `F7` would
         * claim six pull-ups this project has never seen. */
        return ctl->present ? 0x00u : 0xFFu;
      }
      return ctl->present ? w->id : 0xFFu;
    }
    if (odd) {
      return 0xFFu;
    }
    return (uint8_t)(ap_ring_ctl_read16(ctl, second_window, offset) >> 8);

  case AP_RING_CTL_BANK_STATUS: {
    /* The data port is the one slot in this bank with a side effect, so it is
     * the one that must be read once per word rather than once per byte. */
    if (second_window && (offset & AP_RING_CTL_SLOT_MASK) == 6u) {
      if (!odd) {
        w->port_latch = ap_ring_ctl_read16(ctl, second_window, offset & ~1u);
        return (uint8_t)(w->port_latch >> 8);
      }
      return (uint8_t)(w->port_latch & 0xFFu);
    }
    const uint16_t value = ap_ring_ctl_read16(ctl, second_window,
                                              offset & ~1u);
    return (offset & 1u) != 0u ? (uint8_t)(value & 0xFFu)
                               : (uint8_t)(value >> 8);
  }

  case AP_RING_CTL_BANK_TIMER_A:
    /* An 8254 read has side effects -- it unlatches, and it advances the
     * LSB/MSB cursor -- so the odd half of a word access must *not* reach the
     * part. A model that read the device for both halves would consume two
     * bytes of a two-byte count per `move.w` and hand the firmware the LSB and
     * MSB of the same read in the wrong halves. */
    return odd ? 0xFFu : ap_i8254_read(&w->timer_a, timer_reg(offset));
  case AP_RING_CTL_BANK_TIMER_B:
    return odd ? 0xFFu : ap_i8254_read(&w->timer_b, timer_reg(offset));
  default:
    break;
  }
  return 0xFFu;
}

void ap_ring_ctl_write8(ap_ring_ctl_t *ctl, bool second_window, uint32_t offset,
                        uint8_t value) {
  if (ctl == NULL) {
    return;
  }
  ap_ring_ctl_window_t *w = window_of(ctl, second_window);
  /* The same even-address lane as the read side. */
  const bool odd = (offset & 1u) != 0u;

  switch (offset & AP_RING_CTL_BANK_MASK) {
  case AP_RING_CTL_BANK_ID:
    /* The ID at slot 0 absorbs its clear -- it is what the board answers with
     * and is not host-writable. The three word registers beside it take the
     * even byte, as every other byte-wide lane on this board does. */
    if (!odd && (offset & AP_RING_CTL_SLOT_MASK) != 0u) {
      ap_ring_ctl_write16(ctl, second_window, offset,
                          (uint16_t)((uint16_t)value << 8));
    }
    return;
  case AP_RING_CTL_BANK_STATUS: {
    /* The data port again, and the write side is the worse of the two: the
     * read-modify-write below would *read* the port -- advancing its pointer --
     * and then write it, twice over for one `move.w`, so a single word cost
     * four advances. The even half holds its byte and the odd half commits the
     * pair, which is what a 16-bit port on a byte bus does. */
    if (second_window && (offset & AP_RING_CTL_SLOT_MASK) == 6u) {
      if (!odd) {
        w->port_write_high = value;
        return;
      }
      ap_ring_ctl_write16(
          ctl, second_window, offset & ~1u,
          (uint16_t)(((uint16_t)w->port_write_high << 8) | value));
      return;
    }
    const uint16_t held = ap_ring_ctl_read16(ctl, second_window, offset & ~1u);
    const uint16_t merged =
        (offset & 1u) != 0u
            ? (uint16_t)((held & 0xFF00u) | value)
            : (uint16_t)((held & 0x00FFu) | (uint16_t)(value << 8));
    ap_ring_ctl_write16(ctl, second_window, offset & ~1u, merged);
    return;
  }
  case AP_RING_CTL_BANK_TIMER_A:
    if (!odd) {
      ap_i8254_write(&w->timer_a, timer_reg(offset), value);
    }
    return;
  case AP_RING_CTL_BANK_TIMER_B:
    if (!odd) {
      ap_i8254_write(&w->timer_b, timer_reg(offset), value);
    }
    return;
  default:
    break;
  }
}

uint16_t ap_ring_ctl_read16(ap_ring_ctl_t *ctl, bool second_window,
                            uint32_t offset) {
  if (ctl == NULL) {
    return 0xFFFFu;
  }
  ap_ring_ctl_window_t *w = window_of(ctl, second_window);

  if ((offset & AP_RING_CTL_BANK_MASK) == AP_RING_CTL_BANK_ID) {
    switch (offset & AP_RING_CTL_SLOT_MASK) {
    case 0u:
      {
        const uint16_t byte = ctl->present ? w->id : 0xFFu;
        /* The odd half of the lane is undriven, as everywhere else on this
         * board. Finding 15's `movea.l (a2),a0` reads a long here and this is
         * what its second byte would be. */
        return (uint16_t)((uint16_t)(byte << 8) | 0x00FFu);
      }
    case 2u:
      return w->slot_002;
    case 4u:
      return w->slot_004;
    default:
      /* `+006` is the buffer pointer. The firmware only ever writes it, so a
       * read-back is the least-surprising answer rather than an evidenced
       * one. */
      return w->pointer;
    }
  }

  if ((offset & AP_RING_CTL_BANK_MASK) == AP_RING_CTL_BANK_STATUS) {
    switch (offset & AP_RING_CTL_SLOT_MASK) {
    case 0u:
      /* Finding 40's presence gate. With no board the bit is clear, and init
       * returns success having touched nothing else -- an empty slot is not an
       * error. */
      return ctl->present ? w->status : 0u;
    case 2u:
      /* **The command byte is the high lane; the low lane is status.**
       * Finding 48 established that `+402` and `+404` are byte-wide command
       * registers that "carry status as well as command", and subtest 13 says
       * what the status half reads: the firmware writes `#$2` to `+402` and
       * then requires `(+402) & $F0 == $F0`, which a stored `0200` cannot give.
       * So bits 7-4 of the low lane read set on a healthy board.
       *
       * Only those four bits are asserted by anything measured, so only those
       * are answered -- the same restraint as the ID lane in finding 62. */
      return (uint16_t)((w->command_402 & 0xFF00u) |
                        AP_RING_CTL_COMMAND_STATUS_IDLE);
    case 4u:
      /* The same shape one register along: subtest 15 requires
       * `(+404) & $F8 == $E0` after the firmware has written only the command
       * lane, so bits 7-5 read set and bits 4-3 clear. `+402`'s four bits and
       * these three are the whole of what the ROM asserts about either status
       * half; nothing else is answered. */
      return (uint16_t)((w->command_404 & 0xFF00u) |
                        AP_RING_CTL_COMMAND2_STATUS_IDLE);
    default:
      /* `+406`. On the `a2` window this is the buffer's data port -- finding
       * 46a's read-ahead latch, which answers with the word the *previous*
       * access fetched and then fetches the next. On `a1` it is storage,
       * because finding 50a shows the firmware never reads it. */
      if (!second_window) {
        return w->slot_406;
      }
      {
        const uint16_t answered = w->read_ahead;
        w->read_ahead = w->pointer < AP_RING_CTL_BUFFER_WORDS
                            ? ctl->buffer[w->pointer]
                            : 0xFFFFu;
        w->pointer++;
        return answered;
      }
    }
  }

  /* Everywhere else a word is the two bytes, big-endian as the bus is. */
  const uint8_t high = ap_ring_ctl_read8(ctl, second_window, offset);
  const uint8_t low = ap_ring_ctl_read8(ctl, second_window, offset | 1u);
  return (uint16_t)(((uint16_t)high << 8) | low);
}

void ap_ring_ctl_write16(ap_ring_ctl_t *ctl, bool second_window,
                         uint32_t offset, uint16_t value) {
  if (ctl == NULL) {
    return;
  }
  ap_ring_ctl_window_t *w = window_of(ctl, second_window);

  if ((offset & AP_RING_CTL_BANK_MASK) == AP_RING_CTL_BANK_ID) {
    switch (offset & AP_RING_CTL_SLOT_MASK) {
    case 0u:
      return;
    case 2u:
      w->slot_002 = value;
      return;
    case 4u:
      w->slot_004 = value;
      return;
    default:
      /* Finding 46: the pointer `+406` advances from. Writing it does **not**
       * prefetch -- if it did, the firmware's discarded first read would return
       * word 0 and every word after it would be one place early, which is the
       * off-by-one 46a exists to prevent. */
      w->pointer = value;
      return;
    }
  }

  if ((offset & AP_RING_CTL_BANK_MASK) == AP_RING_CTL_BANK_STATUS) {
    switch (offset & AP_RING_CTL_SLOT_MASK) {
    case 0u:
      /* Bit 15 is the board's answer about itself, not the host's to set: init
       * *clears* `+400` and then later writes `$800`, and a model that let the
       * clear take the presence bit with it would report the board absent from
       * that moment on. So the host's bits are kept and the gate is held.
       *
       * Which bits below 15 are writable is not established -- finding 40 names
       * only bit 11 as ever written -- so all of them are stored. */
      w->status = (uint16_t)((value & ~AP_RING_CTL_STATUS_PRESENT) |
                             (ctl->present ? AP_RING_CTL_STATUS_PRESENT : 0u));
      return;
    case 2u:
      w->command_402 = value;
      return;
    case 4u:
      w->command_404 = value;
      return;
    default:
      if (!second_window) {
        w->slot_406 = value;
        return;
      }
      /* The write side of the port, advancing the same pointer the read side
       * does -- which is what lets the firmware fill from `+006 = 0` and then
       * read back from `+006 = 0`. */
      if (w->pointer < AP_RING_CTL_BUFFER_WORDS) {
        ctl->buffer[w->pointer] = value;
      }
      w->pointer++;
      return;
    }
  }

  ap_ring_ctl_write8(ctl, second_window, offset, (uint8_t)(value >> 8));
  ap_ring_ctl_write8(ctl, second_window, offset | 1u, (uint8_t)(value & 0xFFu));
}

void ap_ring_ctl_clock(ap_ring_ctl_t *ctl, bool second_window) {
  if (ctl == NULL) {
    return;
  }
  ap_ring_ctl_window_t *w = window_of(ctl, second_window);
  ap_i8254_clock(&w->timer_a);
  ap_i8254_clock(&w->timer_b);
}
