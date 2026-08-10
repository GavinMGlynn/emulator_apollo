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
  ctl->a1.status = AP_RING_CTL_STATUS_PRESENT;
  ctl->a2.status = AP_RING_CTL_STATUS_PRESENT;
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
    /* Finding 39 reads this as a byte. An unpopulated slot leaves the bus to
     * the pull-ups, which is `FF` on this machine -- the same reasoning the AT
     * window at large uses -- and `FF` is neither `$36` nor `$37`, so an absent
     * board fails the ID check exactly as it should. */
    if (!odd && (offset & AP_RING_CTL_SLOT_MASK) == 0u) {
      return ctl->present ? w->id : 0xFFu;
    }
    return 0xFFu;

  case AP_RING_CTL_BANK_STATUS: {
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
    /* Init clears `(a2)` as part of the reset group. The ID is what the board
     * answers with and is not host-writable, so the clear is accepted and
     * discarded rather than storing over the identity a later read needs. */
    return;
  case AP_RING_CTL_BANK_STATUS: {
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

  if ((offset & AP_RING_CTL_BANK_MASK) == AP_RING_CTL_BANK_STATUS) {
    switch (offset & AP_RING_CTL_SLOT_MASK) {
    case 0u:
      /* Finding 40's presence gate. With no board the bit is clear, and init
       * returns success having touched nothing else -- an empty slot is not an
       * error. */
      return ctl->present ? w->status : 0u;
    case 2u:
      return w->slot_402;
    case 4u:
      return w->slot_404;
    default:
      return w->slot_406;
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
      w->slot_402 = value;
      return;
    case 4u:
      w->slot_404 = value;
      return;
    default:
      w->slot_406 = value;
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
