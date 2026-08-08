#include "device/ap_i8259.h"

#include <string.h>

/* ICW1 is distinguished by A0 = 0 with D4 set: `[8259]`, "Whenever a command is
 * issued with A0 = 0 and D4 = 1, this is interpreted as Initialization Command
 * Word 1 (ICW1)." That test comes before every other decode, which is what lets
 * software restart a part mid-sequence. */
#define ICW1_MARKER 0x10u
#define ICW1_IC4 0x01u
#define ICW1_SNGL 0x02u
#define ICW1_LTIM 0x08u

#define ICW4_UPM 0x01u
#define ICW4_AEOI 0x02u
#define ICW4_MS 0x04u
#define ICW4_BUF 0x08u
#define ICW4_SFNM 0x10u

/* OCW3 is distinguished from OCW2 by D3: the OCW3 row of `[8259]` Figure 8 has
 * bit 3 set and bit 4 clear, OCW2 has both clear. */
#define OCW3_MARKER 0x08u
#define OCW3_RIS 0x01u
#define OCW3_RR 0x02u
#define OCW3_POLL 0x04u
#define OCW3_SMM 0x20u
#define OCW3_ESMM 0x40u

#define OCW2_LEVEL 0x07u
#define OCW2_EOI 0x20u
#define OCW2_SL 0x40u
#define OCW2_R 0x80u

void ap_i8259_init(ap_i8259_t *pic) {
  memset(pic, 0, sizeof *pic);
  /* An uninitialised part masks everything. `[8259]` specifies the IMR cleared
   * by ICW1, not by power-on, and a part that answered interrupts before it had
   * been given a vector base would answer them with vector zero. */
  pic->imr = 0xFFu;
  pic->init_state = AP_I8259_INIT_ICW2;
  /* Not ready: nothing may be acknowledged until ICW1 has actually arrived.
   * `init_state` alone cannot say "never initialised", so the vector base being
   * unset is carried by `x86_mode` staying false -- see
   * `ap_i8259_vectoring_supported`. */
}

/* `[8259]` ICW1's automatic effects, listed a-f. */
static void begin_initialization(ap_i8259_t *pic, uint8_t value) {
  pic->level_triggered = (value & ICW1_LTIM) != 0u;
  pic->single = (value & ICW1_SNGL) != 0u;
  pic->expect_icw4 = (value & ICW1_IC4) != 0u;

  /* (a) "The edge sense circuit is reset, which means that following
   * initialization, an interrupt request (IR) input must make a low-to-high
   * transition to generate an interrupt." Modelled by dropping the latched
   * requests: a line already high has no edge left to give. */
  pic->irr = 0u;
  /* (b) "The Interrupt Mask Register is cleared." */
  pic->imr = 0u;
  /* (c) "IR7 input is assigned priority 7" -- so IR0 is highest. */
  pic->highest_priority = 0u;
  /* (d) "The slave mode address is set to 7." */
  pic->cascade = 7u;
  /* (e) "Special Mask Mode is cleared and Status Read is set to IRR." */
  pic->special_mask = false;
  pic->read_isr = false;
  /* (f) "If IC4 = 0, then all functions selected in ICW4 are set to zero.
   * (Non-Buffered mode, no Auto-EOI, MCS-80, 85 system)." */
  if ((value & ICW1_IC4) == 0u) {
    pic->auto_eoi = false;
    pic->x86_mode = false;
    pic->buffered = false;
    pic->master = false;
    pic->special_fully_nested = false;
  }

  pic->isr = 0u;
  pic->auto_rotate = false;
  pic->poll_pending = false;
  pic->acknowledging = false;
  pic->init_state = AP_I8259_INIT_ICW2;
}

/* Which ICW comes next, from ICW1's own SNGL and IC4 bits. */
static ap_i8259_init_t after_icw2(const ap_i8259_t *pic, bool ic4) {
  if (!pic->single) {
    return AP_I8259_INIT_ICW3;
  }
  return ic4 ? AP_I8259_INIT_ICW4 : AP_I8259_INIT_READY;
}

bool ap_i8259_set_request(ap_i8259_t *pic, unsigned line, bool asserted) {
  if (line >= AP_I8259_LEVELS) {
    return false;
  }
  uint8_t bit = (uint8_t)(1u << line);
  bool was_high = (pic->pins & bit) != 0u;

  /* Whether the wire moved. The caller uses it to skip work that only a change
   * can make necessary; the part itself still does exactly what it did.
   *
   * `pins` is the right thing to compare and `irr` is not. In edge mode a
   * re-assert of a line already high leaves both alone, and in level mode it
   * would re-set an `irr` bit an acknowledge had cleared -- so the two registers
   * disagree about whether anything happened, and only the pin answers the
   * question the caller is asking. */
  const bool moved = asserted != was_high;

  if (asserted) {
    pic->pins |= bit;
  } else {
    pic->pins = (uint8_t)(pic->pins & ~bit);
  }

  if (pic->level_triggered) {
    /* `[8259]`: with LTIM = 1 "the 8259A will operate in the level interrupt
     * mode. Edge detect logic on the interrupt inputs will be disabled." The
     * request simply follows the wire. */
    if (asserted) {
      pic->irr |= bit;
    } else {
      pic->irr = (uint8_t)(pic->irr & ~bit);
    }
    return moved;
  }

  /* Edge mode. "If LTIM = 0, an interrupt request will be recognized by a low
   * to high transition on an IR input. The IR input can remain high without
   * generating another interrupt." */
  if (asserted && !was_high) {
    pic->irr |= bit;
  }
  if (!asserted) {
    /* Dropping the line withdraws a request that has not yet been
     * acknowledged. This is what produces `[8259]`'s spurious level 7: a
     * request "too short in duration" leaves the part with an interrupt to
     * acknowledge and nothing set in the IRR by the time INTA arrives. */
    pic->irr = (uint8_t)(pic->irr & ~bit);
  }
  return moved;
}

/* In level mode the IRR is not a latch. Clearing a bit at acknowledge is
 * momentary: `[8259]` disables the edge detect logic entirely with LTIM = 1, so
 * a pin still high immediately asks again. Fully-nested masking is what stops
 * it retriggering before the EOI, not the IRR bit.
 *
 * Edge mode is the opposite and must not do this -- there, "The IR input can
 * remain high without generating another interrupt", and re-latching from the
 * pin would turn every held line into a repeating interrupt. */
static void refresh_level_requests(ap_i8259_t *pic) {
  if (pic->level_triggered) {
    pic->irr |= pic->pins;
  }
}

/* Walk the levels in priority order, highest first. */
static int resolve(const ap_i8259_t *pic) {
  uint8_t requests = (uint8_t)(pic->irr & ~pic->imr);

  /* `[8259]`: "In the special Mask Mode, when a mask bit is set in OCW1, it
   * inhibits further interrupts at that level and enables interrupts from all
   * other levels (lower as well as higher) that are not masked." So a masked
   * level's in-service bit stops blocking anything. */
  uint8_t blocking = pic->isr;
  if (pic->special_mask) {
    blocking = (uint8_t)(blocking & ~pic->imr);
  }

  for (unsigned i = 0; i < AP_I8259_LEVELS; i++) {
    unsigned level = (pic->highest_priority + i) % AP_I8259_LEVELS;
    uint8_t bit = (uint8_t)(1u << level);

    if ((blocking & bit) != 0u) {
      /* "While the IS bit is set, all further interrupts of the same or lower
       * priority are inhibited". Reaching an in-service level before any
       * request means every remaining request is of lower priority.
       *
       * Except in the special fully nested mode, which exists precisely so a
       * master does not shut out a cascaded slave's higher-priority request
       * while that slave's level is in service. */
      if (pic->special_fully_nested && (requests & bit) != 0u) {
        return (int)level;
      }
      return -1;
    }
    if ((requests & bit) != 0u) {
      return (int)level;
    }
  }
  return -1;
}

int ap_i8259_poll_level(const ap_i8259_t *pic) { return resolve(pic); }

bool ap_i8259_interrupt_pending(const ap_i8259_t *pic) {
  return resolve(pic) >= 0;
}

/* Rotate so that `level` becomes lowest priority, and therefore `level + 1`
 * becomes highest. `[8259]`: "if IR5 is programmed as the bottom priority
 * device, then IR6 will have the highest one." */
static void rotate_to_bottom(ap_i8259_t *pic, unsigned level) {
  pic->highest_priority = (uint8_t)((level + 1u) % AP_I8259_LEVELS);
}

/* Non-specific EOI: "the 8259A will automatically reset the highest IS bit of
 * those that are set, since in the fully nested mode the highest IS level was
 * necessarily the last level acknowledged and serviced."
 *
 * Highest *priority*, which under rotation is not the lowest-numbered. */
static int highest_in_service(const ap_i8259_t *pic) {
  uint8_t candidates = pic->isr;
  /* "an IS bit that is masked by an IMR bit will not be cleared by a
   * non-specific EOI if the 8259A is in the Special Mask Mode." */
  if (pic->special_mask) {
    candidates = (uint8_t)(candidates & ~pic->imr);
  }
  for (unsigned i = 0; i < AP_I8259_LEVELS; i++) {
    unsigned level = (pic->highest_priority + i) % AP_I8259_LEVELS;
    if ((candidates & (1u << level)) != 0u) {
      return (int)level;
    }
  }
  return -1;
}

static void write_ocw2(ap_i8259_t *pic, uint8_t value) {
  bool rotate = (value & OCW2_R) != 0u;
  bool specific = (value & OCW2_SL) != 0u;
  bool eoi = (value & OCW2_EOI) != 0u;
  unsigned level = value & OCW2_LEVEL;

  if (eoi) {
    int target = specific ? (int)level : highest_in_service(pic);
    if (target >= 0) {
      pic->isr = (uint8_t)(pic->isr & ~(1u << (unsigned)target));
      if (rotate) {
        rotate_to_bottom(pic, (unsigned)target);
      }
    }
    return;
  }

  if (rotate && specific) {
    /* Set priority: "R = 1, SL = 1, L0-L2 is the binary priority level code of
     * the bottom priority device". Independent of EOI, and the datasheet says
     * so outright. */
    rotate_to_bottom(pic, level);
    return;
  }
  if (rotate) {
    /* Rotate in automatic EOI mode, set. */
    pic->auto_rotate = true;
    return;
  }
  if (!specific) {
    /* Rotate in automatic EOI mode, clear. */
    pic->auto_rotate = false;
    return;
  }
  /* R = 0, SL = 1, EOI = 0. The one combination `[8259]`'s prose never names,
   * and the figure that would have named it did not survive the scan. It is
   * taken as a no-operation *by elimination* -- the other seven are each
   * stated, and this is what is left over. Recorded rather than silently
   * dropped, because "by elimination" is weaker evidence than the rest of this
   * file rests on. */
}

static void write_ocw3(ap_i8259_t *pic, uint8_t value) {
  if ((value & OCW3_ESMM) != 0u) {
    /* "When this bit is set to 1 it enables the SMM bit to set or reset the
     * Special Mask Mode. When ESMM = 0 the SMM bit becomes a don't care." */
    pic->special_mask = (value & OCW3_SMM) != 0u;
  }
  if ((value & OCW3_POLL) != 0u) {
    /* "Polling overrides status read when P = 1, RR = 1 in OCW3." */
    pic->poll_pending = true;
    return;
  }
  if ((value & OCW3_RR) != 0u) {
    pic->read_isr = (value & OCW3_RIS) != 0u;
  }
}

void ap_i8259_write(ap_i8259_t *pic, bool a0, uint8_t value) {
  if (!a0 && (value & ICW1_MARKER) != 0u) {
    begin_initialization(pic, value);
    return;
  }

  if (a0) {
    switch (pic->init_state) {
    case AP_I8259_INIT_ICW2:
      /* "In an 8086 system A15-A11 are inserted in the five most significant
       * bits of the vectoring byte ... A10-A5 are ignored". */
      pic->vector_base = (uint8_t)(value & 0xF8u);
      pic->init_state = after_icw2(pic, pic->expect_icw4);
      return;
    case AP_I8259_INIT_ICW3:
      pic->cascade = value;
      pic->init_state =
          pic->expect_icw4 ? AP_I8259_INIT_ICW4 : AP_I8259_INIT_READY;
      return;
    case AP_I8259_INIT_ICW4:
      pic->x86_mode = (value & ICW4_UPM) != 0u;
      pic->auto_eoi = (value & ICW4_AEOI) != 0u;
      pic->buffered = (value & ICW4_BUF) != 0u;
      pic->master = (value & ICW4_MS) != 0u;
      pic->special_fully_nested = (value & ICW4_SFNM) != 0u;
      pic->init_state = AP_I8259_INIT_READY;
      return;
    case AP_I8259_INIT_READY:
      /* OCW1: the mask, and the only write that uses A0 = 1 once running. */
      pic->imr = value;
      return;
    }
    return;
  }

  /* A0 = 0 and not ICW1: OCW2 or OCW3, told apart by bit 3. */
  if ((value & OCW3_MARKER) != 0u) {
    write_ocw3(pic, value);
  } else {
    write_ocw2(pic, value);
  }
}

uint8_t ap_i8259_read(ap_i8259_t *pic, bool a0) {
  if (a0) {
    /* "For reading the IMR, no OCW3 is needed. The output data bus will contain
     * the IMR whenever RD is active and A0 = 1." */
    return pic->imr;
  }

  if (pic->poll_pending) {
    /* "The 8259A treats the next RD pulse to the 8259A as an interrupt
     * acknowledge, sets the appropriate IS bit if there is a request, and reads
     * the priority level." */
    pic->poll_pending = false;
    int level = resolve(pic);
    if (level < 0) {
      /* I = 0 and, with no interrupt, nothing to report in W2-W0. */
      return 0u;
    }
    uint8_t bit = (uint8_t)(1u << (unsigned)level);
    pic->isr |= bit;
    pic->irr = (uint8_t)(pic->irr & ~bit);
    refresh_level_requests(pic);
    /* "I: Equal to 1 if there is an interrupt", "W0-W2: Binary code of the
     * highest priority level requesting service." */
    return (uint8_t)(0x80u | (unsigned)level);
  }

  return pic->read_isr ? pic->isr : pic->irr;
}

unsigned ap_i8259_acknowledge_first(ap_i8259_t *pic) {
  int level = resolve(pic);
  if (level < 0) {
    /* "If no interrupt request is present at step 4 ... the 8259A will issue an
     * interrupt level 7. Both the vectoring bytes and the CAS lines will look
     * like an interrupt level 7 was requested."
     *
     * Note what is *not* done here: no ISR bit is set. The spurious level is
     * reported and vectored, but nothing goes into service, so no EOI is owed
     * for it -- which is why a spurious interrupt that software EOIs anyway
     * corrupts the nesting of a real one. Modelling it as a normal
     * acknowledgement would hide that. */
    pic->acknowledging = true;
    pic->acknowledged_level = 7u;
    return 7u;
  }

  uint8_t bit = (uint8_t)(1u << (unsigned)level);
  /* "the highest priority ISR bit is set and the corresponding IRR bit is
   * reset." */
  pic->isr |= bit;
  pic->irr = (uint8_t)(pic->irr & ~bit);
  refresh_level_requests(pic);
  pic->acknowledging = true;
  pic->acknowledged_level = (uint8_t)level;
  return (unsigned)level;
}

uint8_t ap_i8259_acknowledge_second(ap_i8259_t *pic) {
  unsigned level = pic->acknowledged_level;
  pic->acknowledging = false;

  if (pic->auto_eoi) {
    /* "in this mode the 8259A will automatically perform a non-specific EOI
     * operation at the trailing edge of the last interrupt acknowledge pulse
     * (third pulse in MCS-80/85, second in 8086)." */
    int target = highest_in_service(pic);
    if (target >= 0) {
      pic->isr = (uint8_t)(pic->isr & ~(1u << (unsigned)target));
      if (pic->auto_rotate) {
        rotate_to_bottom(pic, (unsigned)target);
      }
    }
  }

  return (uint8_t)(pic->vector_base | level);
}

bool ap_i8259_vectoring_supported(const ap_i8259_t *pic) {
  return pic->x86_mode;
}
