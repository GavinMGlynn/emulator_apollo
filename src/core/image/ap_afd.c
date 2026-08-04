#include "image/ap_afd.h"

#include <string.h>

bool ap_afd_open(ap_afd_t *image, uint8_t *data, size_t bytes, bool writable) {
  if (image == nullptr || data == nullptr) {
    return false;
  }
  if (bytes != (size_t)AP_AFD_BYTES) {
    return false;
  }
  image->data = data;
  image->bytes = bytes;
  image->writable = writable;
  return true;
}

bool ap_afd_lba(uint16_t cylinder, uint8_t head, uint8_t sector,
                uint32_t *lba) {
  if (lba == nullptr) {
    return false;
  }
  if (cylinder >= AP_AFD_CYLINDERS || head >= AP_AFD_HEADS) {
    return false;
  }
  /* One-based, per §6.2's `R`. Sector 0 does not exist and is refused rather
   * than folded onto sector 1. */
  if (sector == 0u || sector > AP_AFD_SECTORS) {
    return false;
  }
  *lba = ((uint32_t)cylinder * AP_AFD_HEADS + head) * AP_AFD_SECTORS +
         (uint32_t)(sector - 1u);
  return true;
}

/* Where a sector starts, and whether all of it is there. */
static bool span(const ap_afd_t *image, uint32_t lba, size_t *offset) {
  if (image == nullptr || image->data == nullptr) {
    return false;
  }
  const size_t start = (size_t)lba * AP_AFD_SECTOR_BYTES;
  if (start + AP_AFD_SECTOR_BYTES > image->bytes) {
    return false;
  }
  *offset = start;
  return true;
}

bool ap_afd_read(const ap_afd_t *image, uint32_t lba, uint8_t *out) {
  size_t offset = 0u;
  if (out == nullptr || !span(image, lba, &offset)) {
    return false;
  }
  memcpy(out, image->data + offset, AP_AFD_SECTOR_BYTES);
  return true;
}

bool ap_afd_write(ap_afd_t *image, uint32_t lba, const uint8_t *in) {
  size_t offset = 0u;
  if (in == nullptr || !span(image, lba, &offset)) {
    return false;
  }
  if (!image->writable) {
    return false;
  }
  memcpy(image->data + offset, in, AP_AFD_SECTOR_BYTES);
  return true;
}
