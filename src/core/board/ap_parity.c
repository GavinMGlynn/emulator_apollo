#include "board/ap_parity.h"

#include <string.h>

void ap_parity_init(ap_parity_t *parity) { memset(parity, 0, sizeof *parity); }

bool ap_parity_attach(ap_parity_t *parity, uint8_t *bad, uint32_t bytes,
                      uint32_t ram_bytes) {
  const uint32_t needed = (ram_bytes + 7u) / 8u;
  if (bad == NULL || bytes < needed) {
    return false;
  }
  parity->bad = bad;
  parity->bad_bytes = bytes;
  parity->ram_bytes = ram_bytes;
  memset(bad, 0, bytes);
  return true;
}

uint16_t ap_parity_lane_bit(uint32_t offset) {
  /* `PROVISIONAL`, and the header says why: no image in hand drives one lane
   * on its own, so this is a convention. Big-endian, so lane 0 is the longword's
   * most significant byte and takes status bit 4. */
  return (uint16_t)(0x0010u << (offset & 3u));
}

static bool locate(const ap_parity_t *parity, uint32_t offset, uint32_t *index,
                   uint8_t *mask) {
  if (parity->bad == NULL || offset >= parity->ram_bytes) {
    return false;
  }
  *index = offset >> 3;
  *mask = (uint8_t)(1u << (offset & 7u));
  return true;
}

void ap_parity_write(ap_parity_t *parity, uint32_t offset, uint16_t lanes) {
  const bool forcing = (lanes & ap_parity_lane_bit(offset)) != 0u;
  if (forcing) {
    parity->forced_writes++;
  }
  uint32_t index;
  uint8_t mask;
  if (!locate(parity, offset, &index, &mask)) {
    if (forcing) {
      /* No parity RAM fitted, or past the memory that is. Counted so a run can
       * say the diagnostic was asked for something this board cannot do,
       * rather than reporting a self-test that passed for the wrong reason. */
      parity->unstorable_writes++;
    }
    return;
  }
  if (forcing) {
    parity->bad[index] |= mask;
  } else {
    /* An ordinary write regenerates parity, and correct parity is the absence
     * of the flag. This is the whole of the clearing behaviour: the firmware
     * clears its forced byte by writing it again at `00749A`, with nothing in
     * the control register saying so. */
    parity->bad[index] &= (uint8_t)~mask;
  }
}

bool ap_parity_check(ap_parity_t *parity, uint32_t offset) {
  uint32_t index;
  uint8_t mask;
  if (!locate(parity, offset, &index, &mask)) {
    return false;
  }
  if ((parity->bad[index] & mask) == 0u) {
    return false;
  }
  if (parity->errors == 0u) {
    parity->first_error_offset = offset;
  }
  parity->errors++;
  return true;
}
