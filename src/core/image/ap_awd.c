/* An Apollo Winchester image. See ap_awd.h for the format and its sources. */

#include "image/ap_awd.h"

#include <string.h>

ap_awd_geometry_t ap_awd_geometry_for(ap_awd_drive_t drive) {
  switch (drive) {
  case AP_AWD_DRIVE_155MB:
    return (ap_awd_geometry_t){.cylinders = 1023u, .heads = 8u, .sectors = 18u};
  case AP_AWD_DRIVE_348MB:
    break;
  }
  return (ap_awd_geometry_t){.cylinders = 1223u, .heads = 15u, .sectors = 18u};
}

uint32_t ap_awd_sector_count(ap_awd_geometry_t geometry) {
  return (uint32_t)geometry.cylinders * geometry.heads * geometry.sectors;
}

bool ap_awd_open(ap_awd_t *image, uint8_t *data, size_t bytes,
                 ap_awd_geometry_t geometry, bool writable) {
  if (image == nullptr || data == nullptr) {
    return false;
  }
  /* A geometry with a zero in it describes no drive, and would make every
   * address arithmetic below divide the space into nothing. */
  if (geometry.cylinders == 0u || geometry.heads == 0u ||
      geometry.sectors == 0u) {
    return false;
  }
  /* At least one sector, or there is nothing to address. Short of the
   * geometry's full extent is allowed and normal -- see the header. */
  if (bytes < AP_AWD_SECTOR_BYTES) {
    return false;
  }
  image->data = data;
  image->bytes = bytes;
  image->geometry = geometry;
  image->writable = writable;
  return true;
}

bool ap_awd_lba(ap_awd_geometry_t geometry, uint16_t cylinder, uint8_t head,
                uint8_t sector, uint32_t *lba) {
  if (cylinder >= geometry.cylinders || head >= geometry.heads) {
    return false;
  }
  const uint32_t address =
      ((uint32_t)cylinder * geometry.heads + head) * geometry.sectors + sector;
  if (address >= ap_awd_sector_count(geometry)) {
    return false;
  }
  *lba = address;
  return true;
}

/* Where a sector begins, and whether the image reaches it. Separate from the
 * two accessors because both need exactly this question answered and a
 * difference between them would be a read and a write disagreeing about where
 * the disk ends. */
static bool span(const ap_awd_t *image, uint32_t lba, size_t *offset) {
  if (lba >= ap_awd_sector_count(image->geometry)) {
    return false;
  }
  const size_t at = (size_t)lba * AP_AWD_SECTOR_BYTES;
  if (at > image->bytes || image->bytes - at < AP_AWD_SECTOR_BYTES) {
    /* Inside the drive and past the end of the file: these images are shorter
     * than the geometry, and a short read here would hand back whatever the
     * caller's buffer held. */
    return false;
  }
  *offset = at;
  return true;
}

bool ap_awd_read(const ap_awd_t *image, uint32_t lba, uint8_t *out) {
  size_t offset = 0;
  if (out == nullptr || !span(image, lba, &offset)) {
    return false;
  }
  memcpy(out, &image->data[offset], AP_AWD_SECTOR_BYTES);
  return true;
}

bool ap_awd_write(ap_awd_t *image, uint32_t lba, const uint8_t *in) {
  size_t offset = 0;
  if (in == nullptr || !image->writable || !span(image, lba, &offset)) {
    return false;
  }
  memcpy(&image->data[offset], in, AP_AWD_SECTOR_BYTES);
  return true;
}
