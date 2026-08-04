#include "device/ap_bt458.h"

#include <string.h>

void ap_bt458_reset(ap_bt458_t *lut) { memset(lut, 0, sizeof *lut); }

unsigned ap_bt458_component(const ap_bt458_t *lut) { return lut->component; }

bool ap_bt458_palette(const ap_bt458_t *lut, unsigned index, uint8_t rgb[3]) {
  if (index >= AP_BT458_PALETTE_ENTRIES || rgb == nullptr) {
    return false;
  }
  memcpy(rgb, lut->palette[index], 3u);
  return true;
}

/* The address advance a completed colour causes, which differs between the two
 * colour spaces. Palette RAM wraps at `$FF`; the overlays run off their end into
 * the control registers rather than back to overlay 0. */
static void advance(ap_bt458_t *lut, ap_bt458_select_t select) {
  if (select == AP_BT458_OVERLAY && lut->address == 0x03u) {
    /* "the address register increments to $04 following a blue read or write
     * cycle to overlay register 3" -- and $04 is the read mask. */
    lut->address = AP_BT458_READ_MASK;
    return;
  }
  /* "resets to $00 after a blue read or write cycle to location $FF", which for
   * an eight-bit register is what incrementing does anyway. Written as the
   * increment rather than as a special case, because it is one. */
  lut->address++;
}

/* Move one byte of a colour, in whichever space the selector names. Returns
 * the byte for a read; the value is ignored on a read and stored on a write. */
static uint8_t colour_cycle(ap_bt458_t *lut, ap_bt458_select_t select,
                            uint8_t value, bool writing) {
  uint8_t(*table)[3] = select == AP_BT458_OVERLAY ? lut->overlay : lut->palette;
  const unsigned limit = select == AP_BT458_OVERLAY
                             ? AP_BT458_OVERLAY_ENTRIES
                             : AP_BT458_PALETTE_ENTRIES;
  /* The overlay space is four entries addressed by the same eight-bit register,
   * so an address beyond them selects nothing there. Masked rather than
   * refused: the part has no way to say no. */
  const unsigned index = lut->address % limit;
  uint8_t out = 0u;

  if (writing) {
    /* Held, not stored: the three bytes are "concatenated into a 24-bit word
     * and written to the location" on the blue cycle, so a colour that never
     * reaches blue never lands. */
    lut->pending[lut->component] = value;
  } else {
    out = table[index][lut->component];
  }

  lut->component++;
  if (lut->component < 3u) {
    return out;
  }
  /* Blue. Commit and advance. */
  lut->component = 0u;
  if (writing) {
    memcpy(table[index], lut->pending, 3u);
  }
  advance(lut, select);
  return out;
}

void ap_bt458_write(ap_bt458_t *lut, ap_bt458_select_t select, uint8_t value) {
  switch (select) {
  case AP_BT458_ADDRESS:
    lut->address = value;
    /* "They are reset to zero when the MPU reads or writes to the address
     * register" -- so setting an address abandons a half-written colour rather
     * than continuing it, which is how a driver resynchronises. */
    lut->component = 0u;
    return;
  case AP_BT458_PALETTE:
  case AP_BT458_OVERLAY:
    (void)colour_cycle(lut, select, value, true);
    return;
  case AP_BT458_CONTROL:
    /* Single bytes, selected by the address register rather than cycled: a
     * mask has no red, green and blue. The address does not advance, and
     * nothing in Table 1 says it should. */
    switch (lut->address) {
    case AP_BT458_READ_MASK:
      lut->read_mask = value;
      return;
    case AP_BT458_BLINK_MASK:
      lut->blink_mask = value;
      return;
    case AP_BT458_COMMAND:
      lut->command = value;
      return;
    case AP_BT458_TEST:
      lut->test = value;
      return;
    default:
      /* An address outside $04-$07 with this selector reaches no register.
       * Table 1 defines four and the part has no fifth to write. */
      return;
    }
  }
}

uint8_t ap_bt458_read(ap_bt458_t *lut, ap_bt458_select_t select) {
  switch (select) {
  case AP_BT458_ADDRESS:
    lut->component = 0u;
    return lut->address;
  case AP_BT458_PALETTE:
  case AP_BT458_OVERLAY:
    return colour_cycle(lut, select, 0u, false);
  case AP_BT458_CONTROL:
    switch (lut->address) {
    case AP_BT458_READ_MASK:
      return lut->read_mask;
    case AP_BT458_BLINK_MASK:
      return lut->blink_mask;
    case AP_BT458_COMMAND:
      return lut->command;
    case AP_BT458_TEST:
      return lut->test;
    default:
      return 0u;
    }
  }
  return 0u;
}
