/* Apollo Token Ring MAC, symbol level. See `ap_ring_mac.h` for the citations
 * and for why the bit patterns came from page images rather than a text
 * extraction. */

#include "ring/ap_ring_mac.h"

/* The fixed part of an out-of-band character: leading zero then six ones, in
 * the top seven bits of a nine-bit symbol. The mask covers bits 8..2 and the
 * value has bit 8 *clear* -- `0 111111 xx` is `0x0FC`, not `0x1FC`, and writing
 * the mask as the value is the transposition that made every character fail
 * validation the first time this ran. */
#define OOB_PREFIX 0x0FCu
#define OOB_PREFIX_MASK 0x1FCu

bool ap_ring_oob_well_formed(uint16_t symbol) {
  if (symbol > 0x1FFu) {
    return false; /* more than nine bits is not a symbol at all */
  }
  /* Bit 8 must be zero and bits 7-2 must all be one. Checking the shape rather
   * than the four values: a receiver detects the violation first and reads the
   * type bits second, so a symbol with the right frame and any type bits is
   * well formed even where this enum has no name for it. */
  return (symbol & OOB_PREFIX_MASK) == OOB_PREFIX && (symbol & 0x100u) == 0u;
}

uint8_t ap_ring_oob_type(uint16_t symbol) { return (uint8_t)(symbol & 0x3u); }

uint16_t ap_ring_token_claim(uint16_t free_token) {
  /* "by changing the state of the character's last bit" -- set, not toggled.
   * Claiming an already-claimed token is not a thing a node does, and a toggle
   * would quietly turn one back into a free token if it ever happened. */
  return (uint16_t)(free_token | 1u);
}

/* --------------------------------------------------------------------------
 * Writing
 * ------------------------------------------------------------------------ */

void ap_ring_bitwriter_init(ap_ring_bitwriter_t *w, uint8_t *bytes,
                            size_t capacity) {
  w->bytes = bytes;
  w->capacity = capacity;
  w->bit_count = 0u;
  w->ones_run = 0u;
  w->overflow = false;
  for (size_t i = 0; i < capacity; i++) {
    bytes[i] = 0u;
  }
}

void ap_ring_write_raw_bit(ap_ring_bitwriter_t *w, bool bit) {
  const size_t index = w->bit_count >> 3;
  if (index >= w->capacity) {
    w->overflow = true;
    return;
  }
  /* Most-significant bit of each byte first, which is the order every figure in
   * `[MAC]` §2.2.1 specifies for the wire. */
  const unsigned shift = 7u - (unsigned)(w->bit_count & 7u);
  if (bit) {
    w->bytes[index] |= (uint8_t)(1u << shift);
  }
  w->bit_count++;
  w->ones_run = bit ? w->ones_run + 1u : 0u;
}

void ap_ring_write_data_bit(ap_ring_bitwriter_t *w, bool bit) {
  ap_ring_write_raw_bit(w, bit);
  /* Checked *after* the bit, so the stuffed zero follows the fifth one rather
   * than preceding it. Writing it before would put the zero after the fourth
   * and produce a stream no receiver could undo. */
  if (w->ones_run >= AP_RING_STUFF_AFTER_ONES) {
    ap_ring_write_raw_bit(w, false);
  }
}

void ap_ring_write_data_bits(ap_ring_bitwriter_t *w, uint32_t value,
                             unsigned bits) {
  for (unsigned i = 0; i < bits; i++) {
    const unsigned shift = bits - 1u - i;
    ap_ring_write_data_bit(w, ((value >> shift) & 1u) != 0u);
  }
}

void ap_ring_write_oob(ap_ring_bitwriter_t *w, uint16_t symbol) {
  for (unsigned i = 0; i < AP_RING_OOB_BITS; i++) {
    const unsigned shift = AP_RING_OOB_BITS - 1u - i;
    ap_ring_write_raw_bit(w, ((symbol >> shift) & 1u) != 0u);
  }
  /* The character ends with its type bits, which may be ones -- the claimed
   * token ends `11`. Leaving the run standing would make the next data bit
   * stuff against a count that belongs to a control character, so the run is
   * cleared. The leading zero of the *next* out-of-band character is what
   * `[MAC]` §2.2.1 says breaks the string, and it is only needed because the
   * data path may have left ones standing -- not because the control path
   * does. */
  w->ones_run = 0u;
}

/* --------------------------------------------------------------------------
 * Reading
 * ------------------------------------------------------------------------ */

void ap_ring_bitreader_init(ap_ring_bitreader_t *r, const uint8_t *bytes,
                            size_t bit_count) {
  r->bytes = bytes;
  r->bit_count = bit_count;
  r->bit_pos = 0u;
  r->ones_run = 0u;
}

bool ap_ring_bitreader_has(const ap_ring_bitreader_t *r, size_t bits) {
  return r->bit_count - r->bit_pos >= bits;
}

bool ap_ring_read_raw_bit(ap_ring_bitreader_t *r, bool *bit) {
  if (!ap_ring_bitreader_has(r, 1u)) {
    return false;
  }
  const size_t index = r->bit_pos >> 3;
  const unsigned shift = 7u - (unsigned)(r->bit_pos & 7u);
  const bool value = ((r->bytes[index] >> shift) & 1u) != 0u;
  r->bit_pos++;
  r->ones_run = value ? r->ones_run + 1u : 0u;
  *bit = value;
  return true;
}

bool ap_ring_read_data_bit(ap_ring_bitreader_t *r, bool *bit, bool *violation) {
  *violation = false;
  bool value = false;
  if (!ap_ring_read_raw_bit(r, &value)) {
    return false;
  }
  *bit = value;
  if (r->ones_run < AP_RING_STUFF_AFTER_ONES) {
    return true;
  }
  /* Five ones have arrived, so the next bit is either the stuffed zero the
   * transmitter inserted -- discard it -- or a sixth one, which is the
   * violation that begins an out-of-band character. Reported rather than
   * refused: the caller stops taking data bits and reads a symbol. */
  bool next = false;
  if (!ap_ring_read_raw_bit(r, &next)) {
    return true; /* the stream ended on the fifth one; nothing to discard */
  }
  if (next) {
    *violation = true;
    /* Rewound, so the caller sees the whole out-of-band character including
     * the six ones it has already consumed part of. The ones-run is restored
     * with it, since a reader that reported a violation and then forgot how it
     * got there would mis-stuff the following data. */
    r->bit_pos--;
    r->ones_run = AP_RING_STUFF_AFTER_ONES;
  }
  return true;
}

bool ap_ring_read_data_bits(ap_ring_bitreader_t *r, unsigned bits,
                            uint32_t *value, bool *violation) {
  uint32_t out = 0u;
  *violation = false;
  for (unsigned i = 0; i < bits; i++) {
    bool bit = false;
    if (!ap_ring_read_data_bit(r, &bit, violation)) {
      return false;
    }
    out = (out << 1) | (bit ? 1u : 0u);
    if (*violation) {
      *value = out;
      return true;
    }
  }
  *value = out;
  return true;
}

bool ap_ring_peek_oob(const ap_ring_bitreader_t *r, uint16_t *symbol) {
  /* A copy, so the look costs the caller nothing: the reader is a value and
   * every field it carries -- position and ones-run both -- is restored by
   * simply discarding this one. */
  ap_ring_bitreader_t look = *r;
  uint16_t candidate = 0u;
  if (!ap_ring_read_oob(&look, &candidate)) {
    return false;
  }
  if (!ap_ring_oob_well_formed(candidate)) {
    return false;
  }
  *symbol = candidate;
  return true;
}

bool ap_ring_read_oob(ap_ring_bitreader_t *r, uint16_t *symbol) {
  if (!ap_ring_bitreader_has(r, AP_RING_OOB_BITS)) {
    return false;
  }
  uint16_t out = 0u;
  for (unsigned i = 0; i < AP_RING_OOB_BITS; i++) {
    bool bit = false;
    (void)ap_ring_read_raw_bit(r, &bit);
    out = (uint16_t)(((unsigned)out << 1) | (bit ? 1u : 0u));
  }
  r->ones_run = 0u;
  *symbol = out;
  return true;
}
