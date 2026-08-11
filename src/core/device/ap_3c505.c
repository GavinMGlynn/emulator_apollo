#include "device/ap_3c505.h"

#include <stddef.h>

bool ap_3c505_decode(uint32_t base, uint32_t address, uint32_t *offset) {
  if (address < base || address >= base + AP_3C505_IO_SIZE) {
    return false;
  }
  if (offset != NULL) {
    *offset = address - base;
  }
  return true;
}
