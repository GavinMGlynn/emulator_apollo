#include "board/ap_ioprot.h"

#include <string.h>

void ap_ioprot_init(ap_ioprot_t *map, bool present) {
  memset(map->bytes, 0, sizeof map->bytes);
  map->size = present ? AP_IOPROT_BYTES : 0u;
}

/* The offset is masked rather than bounds-checked because the board's placement
 * is exactly `AP_IOPROT_BYTES` wide, so nothing outside the range reaches here;
 * the mask is what makes that a property of this file and not an assumption
 * about the caller. */
uint8_t ap_ioprot_read(const ap_ioprot_t *map, uint32_t address) {
  if (!ap_ioprot_present(map)) {
    return 0u;
  }
  return map->bytes[(address - AP_IOPROT_BASE) & (AP_IOPROT_BYTES - 1u)];
}

void ap_ioprot_write(ap_ioprot_t *map, uint32_t address, uint8_t value) {
  if (!ap_ioprot_present(map)) {
    return;
  }
  map->bytes[(address - AP_IOPROT_BASE) & (AP_IOPROT_BYTES - 1u)] = value;
}
